// This file is part of Deark, by Jason Summers.
// This software is in the public domain. See the file COPYING for details.

struct de_bmpinfo {
#define DE_BMPINFO_FMT_BMP 0
#define DE_BMPINFO_FMT_PNG 1
	int file_format;

	de_int64 hotspot_x, hotspot_y;
	de_int64 bitsoffset; // Literal value from FILEHEADER
	de_int64 infohdrsize;
	de_int64 width;
	de_int64 height;
	de_int64 bitcount;
	de_int64 compression_field;

	de_int64 bytes_per_pal_entry;
	de_int64 pal_entries;
	de_int64 num_colors; // For use in ICO/CUR file headers.
	de_int64 rowspan;

	de_int64 foreground_size;
	de_int64 mask_rowspan;
	de_int64 mask_size;

	de_int64 pal_bytes; // Size of palette in bytes
	de_int64 size_of_headers_and_pal; // Relative offset to bitmap (bitsoffset might be absolute)
	de_int64 total_size;

	int is_topdown;
};

#define DE_BMPINFO_HAS_FILEHEADER 0x1
#define DE_BMPINFO_ICO_FORMAT     0x2
#define DE_BMPINFO_HAS_HOTSPOT    0x4

int de_fmtutil_get_bmpinfo(deark *c,  dbuf *f, struct de_bmpinfo *bi, de_int64 pos,
	de_int64 len, unsigned int flags);

void de_fmtutil_handle_exif(deark *c, de_int64 pos, de_int64 len);

void de_fmtutil_handle_photoshop_rsrc(deark *c, de_int64 pos, de_int64 len);

int de_fmtutil_uncompress_packbits(dbuf *f, de_int64 pos1, de_int64 len,
	dbuf *unc_pixels, de_int64 *cmpr_bytes_consumed);

struct de_SAUCE_info {
	de_ucstring *title;
	de_ucstring *artist;
	de_ucstring *organization;
	de_ucstring *creation_date;
	de_int64 original_file_size;
	de_byte data_type;
	de_byte file_type;
	de_int64 width_in_chars; // 0 if unknown
	de_int64 number_of_lines; // Reported value. May be incorrect.
};

int de_has_SAUCE(deark *c, dbuf *f, de_int64 pos);
int de_read_SAUCE(deark *c, dbuf *f, de_int64 pos, struct de_SAUCE_info *si);
void de_free_SAUCE(deark *c, struct de_SAUCE_info *si);

struct de_boxesctx;

// Return 0 to stop reading
typedef int (*de_handle_box_fn)(deark *c, struct de_boxesctx *bctx);

struct de_boxesctx {
	void *userdata;
	dbuf *f; // Input file
	de_handle_box_fn handle_box_fn;

	// Per-box info supplied to handle_box_fn:
	int level;
	de_uint32 boxtype;
	int is_uuid;
	de_byte uuid[16]; // Valid only if is_uuid is set.
	de_int64 box_pos;
	de_int64 box_len;
	// Note: for UUID boxes, payload does not include the UUID
	de_int64 payload_pos;
	de_int64 payload_len;

	// To be filled in by handle_box_fn:
	int is_superbox;
};

void de_read_boxes_format(deark *c, struct de_boxesctx *bctx);
