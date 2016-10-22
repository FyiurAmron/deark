// This file is part of Deark, by Jason Summers.
// This software is in the public domain. See the file COPYING for details.

// Microsoft Compound File Binary File Format
// a.k.a. "OLE Compound Document Format", and a million other names

#include <deark-config.h>
#include <deark-private.h>
#include "fmtutil.h"
DE_DECLARE_MODULE(de_module_cfb);

#define OBJTYPE_EMPTY        0x00
#define OBJTYPE_STORAGE      0x01
#define OBJTYPE_STREAM       0x02
#define OBJTYPE_ROOT_STORAGE 0x05

const char *thumbsdb_catalog_streamname = "Catalog";

struct dir_entry_info {
	de_int64 stream_size;
	int is_mini_stream;
	de_int64 normal_sec_id; // First SecID, valid if is_mini_stream==0
	de_int64 minisec_id; // First MiniSecID, valid if is_mini_stream==1
	de_ucstring *fname;
	char fname_utf8[80];
	struct de_timestamp mod_time;
};

struct catalog_entry {
	de_uint32 id;
	de_ucstring *fname;
	struct de_timestamp mod_time;
};

typedef struct localctx_struct {
#define SUBFMT_DEFAULT    0
#define SUBFMT_THUMBSDB   2
	int subformat;
	de_int64 minor_ver, major_ver;
	de_int64 sec_size;
	//de_int64 num_dir_sectors;
	de_int64 num_fat_sectors;
	de_int64 first_dir_sec_id;
	de_int64 std_stream_min_size;
	de_int64 first_minifat_sec_id;
	de_int64 num_minifat_sectors;
	de_int64 mini_sector_size;
	de_int64 first_difat_sec_id;
	de_int64 num_difat_sectors;
	de_int64 num_fat_entries;
	de_int64 num_dir_entries;

	// The DIFAT is an array of the secIDs that contain the FAT.
	// It is stored in a linked list of sectors, except that the first
	// 109 array entries are stored in the header.
	// After that, the last 4 bytes of each sector are the SecID of the
	// sector containing the next part of the DIFAT, and the remaining
	// bytes are the payload data.
	dbuf *difat;

	// The FAT is an array of "next sectors". Given a SecID, it will tell you
	// the "next" SecID in the stream that uses that sector, or it may have
	// a special code that means "end of chain", etc.
	// All the bytes of a FAT sector are used for payload data.
	dbuf *fat;

	dbuf *minifat; // mini sector allocation table
	dbuf *dir;
	dbuf *mini_sector_stream;

	de_int64 thumbsdb_catalog_num_entries;
	struct catalog_entry *thumbsdb_catalog;
} lctx;

static de_int64 sec_id_to_offset(deark *c, lctx *d, de_int64 sec_id)
{
	if(sec_id<0) return 0;
	return d->sec_size + sec_id * d->sec_size;
}

static de_int64 get_next_sec_id(deark *c, lctx *d, de_int64 cur_sec_id)
{
	de_int64 next_sec_id;

	if(cur_sec_id < 0) return -2;
	if(!d->fat) return -2;
	next_sec_id = dbuf_geti32le(d->fat, cur_sec_id*4);
	return next_sec_id;
}

static de_int64 get_next_minisec_id(deark *c, lctx *d, de_int64 cur_minisec_id)
{
	de_int64 next_minisec_id;

	if(cur_minisec_id < 0) return -2;
	if(!d->minifat) return -2;
	next_minisec_id = dbuf_geti32le(d->minifat, cur_minisec_id*4);
	return next_minisec_id;
}

static void describe_sec_id(deark *c, lctx *d, de_int64 sec_id,
	char *buf, size_t buf_len)
{
	de_int64 sec_offset;

	if(sec_id >= 0) {
		sec_offset = sec_id_to_offset(c, d, sec_id);
		de_snprintf(buf, buf_len, "offs=%d", (int)sec_offset);
	}
	else if(sec_id == -1) {
		de_strlcpy(buf, "free", buf_len);
	}
	else if(sec_id == -2) {
		de_strlcpy(buf, "end of chain", buf_len);
	}
	else if(sec_id == -3) {
		de_strlcpy(buf, "FAT SecID", buf_len);
	}
	else if(sec_id == -4) {
		de_strlcpy(buf, "DIFAT SecID", buf_len);
	}
	else {
		de_strlcpy(buf, "?", buf_len);
	}
}

// Copy a stream (with a known byte size) to a dbuf.
static void copy_normal_stream_to_dbuf(deark *c, lctx *d, de_int64 first_sec_id,
	de_int64 stream_startpos, de_int64 stream_size,
	dbuf *outf)
{
	de_int64 sec_id;
	de_int64 bytes_left_to_copy;
	de_int64 bytes_left_to_skip;

	if(stream_size<0 || stream_size>c->infile->len) return;

	bytes_left_to_copy = stream_size;
	bytes_left_to_skip = stream_startpos;
	sec_id = first_sec_id;
	while(bytes_left_to_copy > 0) {
		de_int64 sec_offs;
		de_int64 bytes_to_copy;
		de_int64 bytes_to_skip;

		if(sec_id<0) break;
		sec_offs = sec_id_to_offset(c, d, sec_id);

		bytes_to_skip = bytes_left_to_skip;
		if(bytes_to_skip > d->sec_size) bytes_to_skip = d->sec_size;

		bytes_to_copy = d->sec_size - bytes_to_skip;
		if(bytes_to_copy > bytes_left_to_copy) bytes_to_copy = bytes_left_to_copy;

		dbuf_copy(c->infile, sec_offs + bytes_to_skip, bytes_to_copy, outf);

		bytes_left_to_copy -= bytes_to_copy;
		bytes_left_to_skip -= bytes_to_skip;
		sec_id = get_next_sec_id(c, d, sec_id);
	}
}

// Same as copy_normal_stream_to_dbuf(), but for mini streams.
static void copy_mini_stream_to_dbuf(deark *c, lctx *d, de_int64 first_minisec_id,
	de_int64 stream_startpos, de_int64 stream_size,
	dbuf *outf)
{
	de_int64 minisec_id;
	de_int64 bytes_left_to_copy;
	de_int64 bytes_left_to_skip;

	if(!d->mini_sector_stream) return;
	if(stream_size<0 || stream_size>d->mini_sector_stream->len) return;

	bytes_left_to_copy = stream_size;
	bytes_left_to_skip = stream_startpos;
	minisec_id = first_minisec_id;
	while(bytes_left_to_copy > 0) {
		de_int64 minisec_offs;
		de_int64 bytes_to_copy;
		de_int64 bytes_to_skip;

		if(minisec_id<0) break;
		minisec_offs = minisec_id * d->mini_sector_size;

		bytes_to_skip = bytes_left_to_skip;
		if(bytes_to_skip > d->mini_sector_size) bytes_to_skip = d->mini_sector_size;

		bytes_to_copy = d->mini_sector_size - bytes_to_skip;
		if(bytes_to_copy > bytes_left_to_copy) bytes_to_copy = bytes_left_to_copy;

		dbuf_copy(d->mini_sector_stream, minisec_offs + bytes_to_skip, bytes_to_copy, outf);

		bytes_left_to_copy -= bytes_to_copy;
		bytes_left_to_skip -= bytes_to_skip;
		minisec_id = get_next_minisec_id(c, d, minisec_id);
	}
}

static void copy_any_stream_to_dbuf(deark *c, lctx *d, struct dir_entry_info *dei,
	de_int64 stream_startpos, de_int64 stream_size,
	dbuf *outf)
{
	if(dei->is_mini_stream) {
		copy_mini_stream_to_dbuf(c, d, dei->minisec_id, stream_startpos, stream_size, outf);
	}
	else {
		copy_normal_stream_to_dbuf(c, d, dei->normal_sec_id, stream_startpos, stream_size, outf);
	}
}

static int do_header(deark *c, lctx *d)
{
	de_int64 pos = 0;
	de_int64 byte_order_code;
	de_int64 sector_shift;
	de_int64 mini_sector_shift;
	char buf[80];
	int retval = 0;

	de_dbg(c, "header at %d\n", (int)pos);
	de_dbg_indent(c, 1);

	// offset 0-7: signature
	// offset 8-23: CLSID

	d->minor_ver = de_getui16le(pos+24);
	d->major_ver = de_getui16le(pos+26);
	de_dbg(c, "format version: %d.%d\n", (int)d->major_ver, (int)d->minor_ver);
	if(d->major_ver!=3 && d->major_ver!=4) {
		de_err(c, "Unsupported format version: %d\n", (int)d->major_ver);
		goto done;
	}

	byte_order_code = de_getui16le(pos+28);
	if(byte_order_code != 0xfffe) {
		de_err(c, "Unsupported byte order code: 0x%04x\n", (unsigned int)byte_order_code);
		goto done;
	}

	sector_shift = de_getui16le(pos+30); // aka ssz
	d->sec_size = (de_int64)(1<<(unsigned int)sector_shift);
	de_dbg(c, "sector size: 2^%d (%d bytes)\n", (int)sector_shift,
		(int)d->sec_size);
	if(d->sec_size!=512 && d->sec_size!=4096) {
		de_err(c, "Unsupported sector size: %d\n", (int)d->sec_size);
		goto done;
	}

	mini_sector_shift = de_getui16le(pos+32); // aka sssz
	d->mini_sector_size = (de_int64)(1<<(unsigned int)mini_sector_shift);
	de_dbg(c, "mini sector size: 2^%d (%d bytes)\n", (int)mini_sector_shift,
		(int)d->mini_sector_size);
	if(d->mini_sector_size!=64) {
		de_err(c, "Unsupported mini sector size: %d\n", (int)d->mini_sector_size);
		goto done;
	}

	// offset 34: 6 reserved bytes

	//d->num_dir_sectors = de_getui32le(pos+40);
	//de_dbg(c, "number of directory sectors: %u\n", (unsigned int)d->num_dir_sectors);
	// Should be 0 if major_ver==3

	// Number of sectors used by sector allocation table (FAT)
	d->num_fat_sectors = de_getui32le(pos+44);
	de_dbg(c, "number of FAT sectors: %d\n", (int)d->num_fat_sectors);

	d->first_dir_sec_id = dbuf_geti32le(c->infile, pos+48);
	describe_sec_id(c, d, d->first_dir_sec_id, buf, sizeof(buf));
	de_dbg(c, "first directory sector: %d (%s)\n", (int)d->first_dir_sec_id, buf);

	// offset 52, transaction signature number

	d->std_stream_min_size = de_getui32le(pos+56);
	de_dbg(c, "min size of a standard stream: %d\n", (int)d->std_stream_min_size);

	// First sector of mini sector allocation table (MiniFAT)
	d->first_minifat_sec_id = dbuf_geti32le(c->infile, pos+60);
	describe_sec_id(c, d, d->first_minifat_sec_id, buf, sizeof(buf));
	de_dbg(c, "first MiniFAT sector: %d (%s)\n", (int)d->first_minifat_sec_id, buf);

	// Number of sectors used by MiniFAT
	d->num_minifat_sectors = de_getui32le(pos+64);
	de_dbg(c, "number of MiniFAT sectors: %d\n", (int)d->num_minifat_sectors);

	// SecID of first (extra??) sector of the DIFAT
	// (also called the Master Sector Allocation Table (MSAT))
	d->first_difat_sec_id = dbuf_geti32le(c->infile, pos+68);
	describe_sec_id(c, d, d->first_difat_sec_id, buf, sizeof(buf));
	de_dbg(c, "first extended DIFAT sector: %d (%s)\n", (int)d->first_difat_sec_id, buf);

	// Number of (extra??) sectors used by the DIFAT
	d->num_difat_sectors = de_getui32le(pos+72);
	de_dbg(c, "number of extended DIFAT sectors: %d\n", (int)d->num_difat_sectors);

	// offset 76: 436 bytes of DIFAT data
	retval = 1;

done:
	de_dbg_indent(c, -1);
	return retval;
}

// Read the locations of the FAT sectors
static void read_difat(deark *c, lctx *d)
{
	de_int64 num_to_read;
	de_int64 still_to_read;
	de_int64 difat_sec_id;
	de_int64 difat_sec_offs;


	de_dbg(c, "reading DIFAT (total number of entries=%d)\n", (int)d->num_fat_sectors);
	de_dbg_indent(c, 1);

	if(d->num_fat_sectors > 1000000) {
		// TODO: Decide what limits to enforce.
		d->num_fat_sectors = 1000000;
	}

	// Expecting d->num_fat_sectors in the DIFAT table
	d->difat = dbuf_create_membuf(c, d->num_fat_sectors * 4, 1);

	still_to_read = d->num_fat_sectors;

	// Copy the part of the DIFAT that is in the header
	num_to_read = still_to_read;
	if(num_to_read>109) num_to_read = 109;
	de_dbg(c, "reading %d DIFAT entries from header, at 76\n", (int)num_to_read);
	dbuf_copy(c->infile, 76, num_to_read*4, d->difat);
	still_to_read -= num_to_read;

	difat_sec_id = d->first_difat_sec_id;
	while(still_to_read>0) {
		if(difat_sec_id<0) break;

		difat_sec_offs = sec_id_to_offset(c, d, difat_sec_id);
		de_dbg(c, "reading DIFAT sector at %d\n", (int)difat_sec_offs);
		num_to_read = (d->sec_size - 4)/4;

		dbuf_copy(c->infile, difat_sec_offs, num_to_read*4, d->difat);
		still_to_read -= num_to_read;
		difat_sec_id = (de_int64)dbuf_geti32le(c->infile, difat_sec_offs + num_to_read*4);
	}

	de_dbg_indent(c, -1);
}

static void dump_fat(deark *c, lctx *d)
{
	de_int64 i;
	de_int64 sec_id;
	char buf[80];

	if(c->debug_level<2) return;

	de_dbg2(c, "dumping FAT contents (%d entries)\n", (int)d->num_fat_entries);

	de_dbg_indent(c, 1);
	for(i=0; i<d->num_fat_entries; i++) {
		sec_id = dbuf_geti32le(d->fat, i*4);
		describe_sec_id(c, d, sec_id, buf, sizeof(buf));
		de_dbg2(c, "FAT[%d]: next_SecID=%d (%s)\n", (int)i, (int)sec_id, buf);
	}
	de_dbg_indent(c, -1);
}

// Read the contents of the FAT sectors
static void read_fat(deark *c, lctx *d)
{
	de_int64 i;
	de_int64 sec_id;
	de_int64 sec_offset;
	char buf[80];

	d->fat = dbuf_create_membuf(c, d->num_fat_sectors * d->sec_size, 1);

	de_dbg(c, "reading FAT contents (%d sectors)\n", (int)d->num_fat_sectors);
	de_dbg_indent(c, 1);
	for(i=0; i<d->num_fat_sectors; i++) {
		sec_id = dbuf_geti32le(d->difat, i*4);
		sec_offset = sec_id_to_offset(c, d, sec_id);
		describe_sec_id(c, d, sec_id, buf, sizeof(buf));
		de_dbg(c, "reading sector: DIFAT_idx=%d, SecID=%d (%s)\n",
			(int)i, (int)sec_id, buf);
		dbuf_copy(c->infile, sec_offset, d->sec_size, d->fat);
	}
	de_dbg_indent(c, -1);

	d->num_fat_entries = d->fat->len/4;
	dump_fat(c, d);
}

static void dump_minifat(deark *c, lctx *d)
{
	de_int64 i;
	de_int64 sec_id;
	de_int64 num_minifat_entries;

	if(c->debug_level<2) return;
	if(!d->minifat) return;

	num_minifat_entries = d->minifat->len / 4;
	de_dbg2(c, "dumping MiniFAT contents (%d entries)\n", (int)num_minifat_entries);

	de_dbg_indent(c, 1);
	for(i=0; i<num_minifat_entries; i++) {
		sec_id = dbuf_geti32le(d->minifat, i*4);
		de_dbg2(c, "MiniFAT[%d]: next_MiniSecID=%d\n", (int)i, (int)sec_id);
	}
	de_dbg_indent(c, -1);
}

// Read the contents of the MiniFAT sectors into d->minifat
static void read_minifat(deark *c, lctx *d)
{
	de_int64 i;
	de_int64 sec_id;
	de_int64 sec_offset;
	char buf[80];

	if(d->num_minifat_sectors > 1000000) {
		// TODO: Decide what limits to enforce.
		d->num_minifat_sectors = 1000000;
	}

	d->minifat = dbuf_create_membuf(c, d->num_minifat_sectors * d->sec_size, 1);

	// TODO: Use copy_normal_stream_to_dbuf
	de_dbg(c, "reading MiniFAT contents (%d sectors)\n", (int)d->num_minifat_sectors);
	de_dbg_indent(c, 1);

	sec_id = d->first_minifat_sec_id;

	for(i=0; i<d->num_minifat_sectors; i++) {
		if(sec_id<0) break;

		sec_offset = sec_id_to_offset(c, d, sec_id);
		describe_sec_id(c, d, sec_id, buf, sizeof(buf));
		de_dbg(c, "reading MiniFAT sector #%d, SecID=%d (%s)\n",
			(int)i, (int)sec_id, buf);
		dbuf_copy(c->infile, sec_offset, d->sec_size, d->minifat);

		sec_id = get_next_sec_id(c, d, sec_id);
	}
	de_dbg_indent(c, -1);

	dump_minifat(c, d);
}

// Returns -1 if not a valid name
static de_int64 stream_name_to_catalog_id(deark *c, lctx *d, struct dir_entry_info *dei)
{
	char buf[16];
	size_t nlen;
	size_t i;

	nlen = de_strlen(dei->fname_utf8);
	if(nlen>sizeof(buf)-1) return -1;

	for(i=0; i<nlen; i++) {
		// Name should contain only digits
		if(dei->fname_utf8[i]<0x30 || dei->fname_utf8[i]>0x39) return -1;

		// The stream name is the *reversed* string form of the ID number.
		// (I assume this is to try to keep the directory tree structure balanced.)
		buf[nlen-1-i] = dei->fname_utf8[i];
	}
	buf[nlen] = '\0';

	return de_atoi64(buf);
}

// Returns an index into d->thumbsdb_catalog.
// Returns -1 if not found.
static de_int64 lookup_catalog_entry(deark *c, lctx *d, struct dir_entry_info *dei)
{
	de_int64 i;
	de_int64 id;

	if(d->thumbsdb_catalog_num_entries<1 || !d->thumbsdb_catalog) return -1;
	if(!dei->fname) return -1;

	id = stream_name_to_catalog_id(c, d, dei);
	if(id<0) return -1;

	for(i=0; i<d->thumbsdb_catalog_num_entries; i++) {
		if(d->thumbsdb_catalog[i].id == id)
			return i;
	}
	return -1;
}

static void extract_stream_to_file(deark *c, lctx *d, struct dir_entry_info *dei)
{
	de_int64 startpos;
	de_int64 final_streamsize;
	dbuf *outf = NULL;
	dbuf *tmpdbuf = NULL;
	de_finfo *fi = NULL;
	de_ucstring *tmpfn = NULL;

	startpos = 0;
	final_streamsize = dei->stream_size;

	// By default, use the "stream name" as the filename.
	tmpfn = ucstring_clone(dei->fname);

	fi = de_finfo_create(c);

	// By default, use the mod time from the directory entry.
	if(dei->mod_time.is_valid) {
		fi->mod_time = dei->mod_time; // struct copy
	}

	if(d->subformat==SUBFMT_THUMBSDB) {
		de_int64 hdrsize;
		de_int64 catalog_idx;
		const char *ext;

		// Special handling of Thumbs.db files.
		// A Thumbs.db stream typically has a header, followed by an embedded JPEG
		// (or something) file.

		if(!de_strcmp(dei->fname_utf8, thumbsdb_catalog_streamname)) {
			goto done;
		}

		catalog_idx = lookup_catalog_entry(c, d, dei);

		if(catalog_idx>=0) {
			if(d->thumbsdb_catalog[catalog_idx].mod_time.is_valid) {
				fi->mod_time = d->thumbsdb_catalog[catalog_idx].mod_time; // struct copy
			}
		}

		// Read the first part of the stream. 32 bytes should be enough to get
		// the header, and enough of the payload to choose a file extension.
		tmpdbuf = dbuf_create_membuf(c, 32, 0);
		copy_any_stream_to_dbuf(c, d, dei, 0, 32, tmpdbuf);

		// This might be a 4-byte int, but since I'm not sure, I'll only test
		// the first 2 bytes.
		hdrsize = dbuf_getui16le(tmpdbuf, 0);
		// 0x0c = "Original format" Thumbs.db
		// 0x18 = "Windows 7 format"
		if((hdrsize==0x0c || hdrsize==0x18) && dei->stream_size>hdrsize) {
			de_byte b;

			startpos = hdrsize;
			final_streamsize -= hdrsize;

			if(catalog_idx>=0 && c->filenames_from_file) {
				de_dbg(c, "name from catalog: \"%s\"\n",
					ucstring_get_printable_sz(d->thumbsdb_catalog[catalog_idx].fname));

				// Replace the default name with the name from the catalog.
				ucstring_empty(tmpfn);
				ucstring_append_ucstring(tmpfn, d->thumbsdb_catalog[catalog_idx].fname);
			}

			b = dbuf_getbyte(tmpdbuf, hdrsize);
			if(b==0xff) ext = "jpg";
			else if(b==0x89) ext = "png";
			else ext = "bin";

			ucstring_printf(tmpfn, DE_ENCODING_ASCII, ".thumb.%s", ext);
		}
		else {
			de_warn(c, "Unidentified Thumbs.db stream \"%s\"\n",
				ucstring_get_printable_sz(dei->fname));
		}
	}

	de_finfo_set_name_from_ucstring(c, fi, tmpfn);
	fi->original_filename_flag = 1;

	outf = dbuf_create_output_file(c, NULL, fi, 0);
	copy_any_stream_to_dbuf(c, d, dei, startpos, final_streamsize, outf);

done:
	dbuf_close(tmpdbuf);
	dbuf_close(outf);
	ucstring_destroy(tmpfn);
	de_finfo_destroy(c, fi);
}

static int read_thumbsdb_catalog(deark *c, lctx *d, struct dir_entry_info *dei)
{
	de_int64 item_len;
	de_int64 n;
	de_int64 i;
	de_int64 pos;
	int retval = 0;
	dbuf *catf = NULL;

	if(d->thumbsdb_catalog) return 0; // Already read a catalog

	de_dbg(c, "reading thumbsdb catalog\n");
	de_dbg_indent(c, 1);

	catf = dbuf_create_membuf(c, dei->stream_size, 0);
	copy_any_stream_to_dbuf(c, d, dei, 0, dei->stream_size, catf);

	item_len = dbuf_getui16le(catf, 0);
	de_dbg(c, "header size: %d\n", (int)item_len); // (?)
	if(item_len!=16) goto done;

	n = dbuf_getui16le(catf, 2);
	de_dbg(c, "version: %d\n", (int)n); // (?)
	if(n!=7) goto done;

	d->thumbsdb_catalog_num_entries = dbuf_getui16le(catf, 4); // This might really be a 4 byte int.
	de_dbg(c, "num entries: %d\n", (int)d->thumbsdb_catalog_num_entries);
	if(d->thumbsdb_catalog_num_entries>2048)
		d->thumbsdb_catalog_num_entries = 2048;

	d->thumbsdb_catalog = de_malloc(c, d->thumbsdb_catalog_num_entries * sizeof(struct catalog_entry));

	pos = item_len;

	for(i=0; i<d->thumbsdb_catalog_num_entries; i++) {
		de_int64 mod_time_as_FILETIME;
		char timestamp_buf[64];

		if(pos >= catf->len) goto done;
		item_len = dbuf_getui32le(catf, pos);
		de_dbg(c, "catalog entry #%d, len=%d\n", (int)i, (int)item_len);
		if(item_len<20) goto done;

		de_dbg_indent(c, 1);

		d->thumbsdb_catalog[i].id = (de_uint32)dbuf_getui32le(catf, pos+4);
		de_dbg(c, "id: %u\n", (unsigned int)d->thumbsdb_catalog[i].id);

		mod_time_as_FILETIME = dbuf_geti64le(catf, pos+8);
		de_FILETIME_to_timestamp(mod_time_as_FILETIME, &d->thumbsdb_catalog[i].mod_time);
		de_timestamp_to_string(&d->thumbsdb_catalog[i].mod_time, timestamp_buf, sizeof(timestamp_buf), 1);
		de_dbg(c, "timestamp: %s\n", timestamp_buf);

		d->thumbsdb_catalog[i].fname = ucstring_create(c);

		dbuf_read_to_ucstring(catf, pos+16, item_len-20, d->thumbsdb_catalog[i].fname,
			0, DE_ENCODING_UTF16LE);
		de_dbg(c, "name: \"%s\"\n", ucstring_get_printable_sz(d->thumbsdb_catalog[i].fname));

		de_dbg_indent(c, -1);

		pos += item_len;
	}

	retval = 1;
done:
	de_dbg_indent(c, -1);
	dbuf_close(catf);
	if(!retval) {
		d->thumbsdb_catalog_num_entries = 0; // Make sure we don't use a bad catalog.
	}
	return retval;
}

static void read_mini_sector_stream(deark *c, lctx *d, de_int64 first_sec_id, de_int64 stream_size)
{
	if(d->mini_sector_stream) return; // Already done

	de_dbg(c, "reading mini sector stream (%d bytes)\n", (int)stream_size);
	d->mini_sector_stream = dbuf_create_membuf(c, 0, 0);
	copy_normal_stream_to_dbuf(c, d, first_sec_id, 0, stream_size, d->mini_sector_stream);
}

// Reads the directory stream into d->dir, and sets d->num_dir_entries.
static void read_directory_stream(deark *c, lctx *d)
{
	de_int64 dir_sec_id;
	de_int64 dir_sector_offs;
	de_int64 num_entries_per_sector;
	de_int64 dir_sector_count = 0;

	de_dbg(c, "reading directory stream\n");
	de_dbg_indent(c, 1);

	d->dir = dbuf_create_membuf(c, 0, 0);

	dir_sec_id = d->first_dir_sec_id;

	num_entries_per_sector = d->sec_size / 128;
	d->num_dir_entries = 0;

	// TODO: Use copy_normal_stream_to_dbuf
	while(1) {
		if(dir_sec_id<0) break;

		dir_sector_offs = sec_id_to_offset(c, d, dir_sec_id);

		de_dbg(c, "directory sector #%d SecID=%d (offs=%d), entries %d-%d\n",
			(int)dir_sector_count,
			(int)dir_sec_id, (int)dir_sector_offs,
			(int)d->num_dir_entries, (int)(d->num_dir_entries + num_entries_per_sector - 1));

		dbuf_copy(c->infile, dir_sector_offs, d->sec_size, d->dir);

		d->num_dir_entries += num_entries_per_sector;

		dir_sec_id = get_next_sec_id(c, d, dir_sec_id);
		dir_sector_count++;
	}

	de_dbg(c, "number of directory entries: %d\n", (int)d->num_dir_entries);

	de_dbg_indent(c, -1);
}

// Read and process a directory entry from the d->dir stream
static void do_dir_entry(deark *c, lctx *d, de_int64 dir_entry_idx, de_int64 dir_entry_offs,
	int pass)
{
	de_int64 name_len_raw;
	de_int64 name_len_bytes;
	de_byte entry_type;
	de_int64 raw_sec_id;
	struct dir_entry_info *dei = NULL;
	int need_to_read_stream_info = 0;
	int is_thumbsdb_catalog = 0;
	const char *tname;
	de_int64 mod_time_as_FILETIME;
	char timestamp_buf[64];
	de_byte clsid[16];
	char clsid_string[50];
	char buf[80];

	dei = de_malloc(c, sizeof(struct dir_entry_info));

	entry_type = dbuf_getbyte(d->dir, dir_entry_offs+66);
	switch(entry_type) {
	case OBJTYPE_EMPTY: tname="empty"; break;
	case OBJTYPE_STORAGE: tname="storage object"; break;
	case OBJTYPE_STREAM: tname="stream"; break;
	case OBJTYPE_ROOT_STORAGE: tname="root storage object"; break;
	default: tname="?";
	}
	de_dbg(c, "type: 0x%02x (%s)\n", (unsigned int)entry_type, tname);
	if(entry_type==0x00) goto done;

	if(pass==2 && entry_type==OBJTYPE_ROOT_STORAGE) goto done;

	name_len_raw = dbuf_getui16le(d->dir, dir_entry_offs+64);
	de_dbg2(c, "name len: %d bytes\n", (int)name_len_raw);
	name_len_bytes = name_len_raw-2; // Ignore the trailing U+0000
	if(name_len_bytes<0) name_len_bytes = 0;

	dei->fname = ucstring_create(c);
	dbuf_read_to_ucstring(d->dir, dir_entry_offs, name_len_bytes, dei->fname,
		0, DE_ENCODING_UTF16LE);
	de_dbg(c, "name: \"%s\"\n", ucstring_get_printable_sz(dei->fname));

	// A C-style version of the stream name, to make it easier to analyze.
	ucstring_to_sz(dei->fname, dei->fname_utf8, sizeof(dei->fname_utf8), DE_ENCODING_UTF8);

	if(entry_type==OBJTYPE_STORAGE || entry_type==OBJTYPE_ROOT_STORAGE) {
		dbuf_read(d->dir, clsid, dir_entry_offs+80, 16);
		de_fmtutil_guid_to_uuid(clsid);
		de_fmtutil_render_uuid(c, clsid, clsid_string, sizeof(clsid_string));
		de_dbg(c, "%sclsid: {%s}\n", (entry_type==OBJTYPE_ROOT_STORAGE)?"root ":"",
			clsid_string);
	}

	// Try to avoid doing too much of the same thing twice (once per pass).
	// The default is to read sector numbers, etc., only on pass 2.
	need_to_read_stream_info = (pass==2);

	if(entry_type==OBJTYPE_ROOT_STORAGE)
		need_to_read_stream_info = (pass==1);

	if(d->subformat==SUBFMT_THUMBSDB &&
		!de_strcmp(dei->fname_utf8, thumbsdb_catalog_streamname))
	{
		is_thumbsdb_catalog = 1;
		need_to_read_stream_info = (pass==1);
	}

	if(!need_to_read_stream_info) goto done;

	mod_time_as_FILETIME = dbuf_geti64le(d->dir, dir_entry_offs+108);
	if(mod_time_as_FILETIME!=0) {
		de_FILETIME_to_timestamp(mod_time_as_FILETIME, &dei->mod_time);
		de_timestamp_to_string(&dei->mod_time, timestamp_buf, sizeof(timestamp_buf), 1);
		de_dbg(c, "mod time: %s\n", timestamp_buf);
	}

	raw_sec_id = dbuf_geti32le(d->dir, dir_entry_offs+116);

	if(d->major_ver<=3) {
		dei->stream_size = dbuf_getui32le(d->dir, dir_entry_offs+120);
	}
	else {
		dei->stream_size = dbuf_geti64le(d->dir, dir_entry_offs+120);
	}

	de_dbg(c, "stream size: %"INT64_FMT"\n", dei->stream_size);
	dei->is_mini_stream = (entry_type==OBJTYPE_STREAM) && (dei->stream_size < d->std_stream_min_size);

	if(dei->is_mini_stream) {
		dei->minisec_id = raw_sec_id;
		de_dbg(c, "MiniSecID: %d\n", (int)dei->minisec_id);
	}
	else {
		dei->normal_sec_id = raw_sec_id;
		describe_sec_id(c, d, dei->normal_sec_id, buf, sizeof(buf));
		de_dbg(c, "SecID: %d (%s)\n", (int)dei->normal_sec_id, buf);
	}

	if(pass==2 && entry_type==OBJTYPE_STREAM) {
		extract_stream_to_file(c, d, dei);
	}
	else if(pass==1 && is_thumbsdb_catalog) {
		read_thumbsdb_catalog(c, d, dei);
	}
	else if(pass==1 && entry_type==OBJTYPE_ROOT_STORAGE) {
		read_mini_sector_stream(c, d, dei->normal_sec_id, dei->stream_size);
	}

done:
	if(dei) {
		ucstring_destroy(dei->fname);
		de_free(c, dei);
	}
}

// Pass 1: Detect the file format, and read the mini sector stream.
// Pass 2: Extract files.
static void do_directory(deark *c, lctx *d, int pass)
{
	de_int64 dir_entry_offs; // Offset in d->dir
	de_int64 i;

	de_dbg(c, "scanning directory, pass %d\n", pass);
	de_dbg_indent(c, 1);

	for(i=0; i<d->num_dir_entries; i++) {
		dir_entry_offs = 128*i;
		de_dbg(c, "directory entry #%d\n", (int)i);

		de_dbg_indent(c, 1);
		do_dir_entry(c, d, i, dir_entry_offs, pass);
		de_dbg_indent(c, -1);
	}

	// TODO: Autodetect Thumbs.db files (during and after pass 1)

	de_dbg_indent(c, -1);
}

static void de_run_cfb(deark *c, de_module_params *mparams)
{
	lctx *d = NULL;
	const char *cfbfmt_opt;

	d = de_malloc(c, sizeof(lctx));

	cfbfmt_opt = de_get_ext_option(c, "cfb:fmt");
	if(cfbfmt_opt) {
		if(!de_strcmp(cfbfmt_opt, "thumbsdb")) {
			d->subformat = SUBFMT_THUMBSDB;
		}
	}

	if(!do_header(c, d)) {
		goto done;
	}

	read_difat(c, d);

	read_fat(c, d);

	read_minifat(c, d);

	read_directory_stream(c, d);

	do_directory(c, d, 1);

	do_directory(c, d, 2);

done:
	if(d) {
		dbuf_close(d->difat);
		dbuf_close(d->fat);
		dbuf_close(d->minifat);
		dbuf_close(d->dir);
		dbuf_close(d->mini_sector_stream);
		if(d->thumbsdb_catalog) {
			de_int64 k;
			for(k=0; k<d->thumbsdb_catalog_num_entries; k++) {
				ucstring_destroy(d->thumbsdb_catalog[k].fname);
			}
			de_free(c, d->thumbsdb_catalog);
			d->thumbsdb_catalog = NULL;
		}
		de_free(c, d);
	}
}

static int de_identify_cfb(deark *c)
{
	if(!dbuf_memcmp(c->infile, 0, "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8))
		return 100;
	return 0;
}

void de_module_cfb(deark *c, struct deark_module_info *mi)
{
	mi->id = "cfb";
	mi->desc = "Microsoft Compound File Binary File";
	mi->run_fn = de_run_cfb;
	mi->identify_fn = de_identify_cfb;
}