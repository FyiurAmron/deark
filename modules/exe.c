// This file is part of Deark, by Jason Summers.
// This software is in the public domain. See the file COPYING for details.

// Microsoft EXE executable formats.

#include <deark-config.h>
#include <deark-modules.h>

#define EXE_FMT_DOS    1
#define EXE_FMT_NE     2
#define EXE_FMT_PE     3
#define EXE_FMT_PEPLUS 4

typedef struct localctx_struct {
	int fmt;
	de_int64 ext_header_offset;
	de_int64 sections_offset;
	de_int64 number_of_sections;

	// File offset where the resources start. Some addresses are relative
	// to this.
	de_int64 cur_base_addr;

	de_int64 cur_section_virt_addr;
	de_int64 cur_section_data_offset;
	de_int64 cur_rsrc_type;
	de_int64 rsrc_item_count;
} lctx;

static void do_opt_coff_data_dirs(deark *c, lctx *d, de_int64 pos)
{
	de_int64 rsrc_tbl_rva;
	de_int64 rsrc_tbl_size;

	de_dbg(c, "COFF/PE optional header (data directories) at %d\n", (int)pos);
	rsrc_tbl_rva = de_getui32le(pos+16);
	// I don't know if rsrc_tbl_rva will be needed for anything. It seems redundant.
	rsrc_tbl_size = de_getui32le(pos+20);
	de_dbg(c, "resource table RVA=0x%08x, size=%d\n", (unsigned int)rsrc_tbl_rva,
		(int)rsrc_tbl_size);
}

static void do_opt_coff_nt_header(deark *c, lctx *d, de_int64 pos)
{
	de_int64 x;
	de_int64 subsystem;
	const char *desc;

	de_dbg(c, "COFF/PE optional header (Windows NT) at %d\n", (int)pos);
	x = de_getui32le(pos);
	de_dbg(c, "image base offset: 0x%08x\n", (unsigned int)x);

	subsystem = de_getui16le(pos+40);
	switch(subsystem) {
	case 2: desc = " (Windows GUI)"; break;
	case 3: desc = " (console)"; break;
	default: desc = "";
	}
	de_dbg(c, "subsystem: %d%s\n", (int)subsystem, desc);
}

static void do_opt_coff_header(deark *c, lctx *d, de_int64 pos, de_int64 len)
{
	de_int64 sig;
	de_int64 coff_opt_hdr_size;

	de_dbg(c, "COFF/PE optional header at %d, size=%d\n", (int)pos, (int)len);

	sig = de_getui16le(pos);
	de_dbg(c, "signature: 0x%04x\n", (int)sig);

	if(sig==0x010b)
		coff_opt_hdr_size = 28;
	else
		coff_opt_hdr_size = 24;

	if(sig==0x010b) { // = PE32
		d->fmt = EXE_FMT_PE;
		de_declare_fmt(c, "PE32 executable file");
		do_opt_coff_nt_header(c, d, pos+coff_opt_hdr_size);
		do_opt_coff_data_dirs(c, d, pos+coff_opt_hdr_size+68);
	}
	else if(sig==0x020b) {
		d->fmt = EXE_FMT_PEPLUS;
		de_declare_fmt(c, "PE32+ executable file");
	}
	else if(sig==0x0107) {
		de_declare_fmt(c, "PE ROM image");
	}
	else {
		de_declare_fmt(c, "Unknown PE file type");
	}
}

// 'pos' is the start of the 4-byte PE signature.
// Following it is a 20-byte COFF header.
static void do_pe_coff_header(deark *c, lctx *d, de_int64 pos)
{
	de_int64 arch;
	de_int64 opt_hdr_size;

	arch = de_getui16le(pos+4+0);
	de_dbg(c, "target architecture: 0x%04x\n", (int)arch);

	d->number_of_sections = de_getui16le(pos+4+2);
	de_dbg(c, "number of sections: %d\n", (int)d->number_of_sections);

	opt_hdr_size = de_getui16le(pos+4+16);
	de_dbg(c, "optional header size: %d\n", (int)opt_hdr_size);
	if(opt_hdr_size>0) {
		do_opt_coff_header(c, d, pos+4+20, opt_hdr_size);
		d->sections_offset = pos+4+20+opt_hdr_size;
	}
}

static void do_ne_ext_header(deark *c, lctx *d, de_int64 pos)
{
	de_int64 rsrc_tbl_offset;
	de_byte target_os;
	const char *desc;

	rsrc_tbl_offset = de_getui16le(pos+36);
	de_dbg(c, "offset of resource table: %d\n", (int)rsrc_tbl_offset);
	target_os = de_getbyte(pos+54);
	switch(target_os) {
	case 1: desc=" (OS/2)"; break;
	case 2: desc=" (Windows)"; break;
	case 4: desc=" (Windows 386)"; break;
	default: desc="";
	}
	de_dbg(c, "target OS: %d%s\n", (int)target_os, desc);
}

static void do_ext_header(deark *c, lctx *d)
{
	de_byte buf[4];

	de_read(buf, d->ext_header_offset, 4);
	if(!de_memcmp(buf, "PE\0\0", 4)) {
		de_dbg(c, "PE header at %d\n", (int)d->ext_header_offset);
		do_pe_coff_header(c, d, d->ext_header_offset);
	}
	else if(!de_memcmp(buf, "NE", 2)) {
		de_declare_fmt(c, "NE");
		d->fmt = EXE_FMT_NE;
		do_ne_ext_header(c, d, d->ext_header_offset);
	}
}

static void do_fileheader(deark *c, lctx *d)
{
	de_int64 reloc_tbl_offset;

	reloc_tbl_offset = de_getui16le(24);
	de_dbg(c, "relocation table offset: %d\n", (int)reloc_tbl_offset);

	if(reloc_tbl_offset>=64) {
		d->ext_header_offset = de_getui32le(60);
		de_dbg(c, "extended header offset: %d\n", (int)d->ext_header_offset);

		if(d->ext_header_offset >= c->infile->len) {
			// TODO: Some DOS executables have reloc_tbl_offset>=64, and do not have
			// ext_header_offset at offset 60.
			d->ext_header_offset = 0;
		}
	}
	else {
		de_declare_fmt(c, "MS-DOS EXE");
		d->fmt = EXE_FMT_DOS;
	}

	if(d->ext_header_offset!=0) {
		do_ext_header(c, d);
	}
}

// Extract a raw DIB, and write it to a file as a BMP.
// TODO: Move this to a common "bmp.c" file.
static void de_DIB_to_BMP(deark *c, dbuf *inf, de_int64 pos, de_int64 len, dbuf *outf)
{
	de_int64 infohdrsize;
	de_int64 hdrs_size;
	de_int64 bitcount = 0;
	de_int64 pal_entries = 0;
	de_int64 bytes_per_pal_entry = 0;
	de_int64 compression = 0;

	hdrs_size = 14; // Start with the size of the FILEHEADER.

	// Calculate the position of the bitmap bits.
	infohdrsize = dbuf_getui32le(inf, pos);
	hdrs_size += infohdrsize;

	if(infohdrsize==12) {
		bytes_per_pal_entry = 3;
		bitcount = dbuf_getui16le(inf, pos+10);
	}
	else if(infohdrsize>=16) {
		bytes_per_pal_entry = 4;
		bitcount = dbuf_getui16le(inf, pos+14);

		if(infohdrsize>=20) {
			compression = dbuf_getui32le(inf, pos+16);
			if(compression==3) { // BI_BITFIELDS
				hdrs_size += 12;
			}
		}

		if(infohdrsize>=36) {
			pal_entries = dbuf_getui32le(inf, pos+32);
		}
	}
	else {
		return;
	}

	if(pal_entries==0 && bitcount>0 && bitcount<=8) {
		pal_entries = (de_int64)(1<<(unsigned int)bitcount);
	}

	// Account for the palette.
	hdrs_size += pal_entries * bytes_per_pal_entry;

	// Manufacture a BITMAPFILEHEADER.
	dbuf_write(outf, (const de_byte*)"BM", 2);
	dbuf_writeui32le(outf, 14+(de_uint32)len); // File size
	dbuf_write(outf, (const de_byte*)"\0\0\0\0", 4);
	dbuf_writeui32le(outf, (de_uint32)hdrs_size); // "Bits offset"

	dbuf_copy(inf, pos, len, outf); // Copy the rest of the file.
}

static void do_extract_BITMAP(deark *c, lctx *d, de_int64 pos, de_int64 len)
{
	dbuf *f;
	if(len<12) return;

	f = dbuf_create_output_file(c, "bmp");
	de_DIB_to_BMP(c, c->infile, pos, len, f);
	dbuf_close(f);
}

static void do_resource_data_entry(deark *c, lctx *d, de_int64 rel_pos)
{
	de_int64 data_size;
	de_int64 data_virt_addr;
	de_int64 data_real_offset;
	de_int64 type_id;

	type_id = d->cur_rsrc_type;

	de_dbg(c, " resource data entry at %d(%d) rsrc_type=%d\n",
		(int)(d->cur_base_addr+rel_pos), (int)rel_pos, (int)type_id);

	data_virt_addr = de_getui32le(d->cur_base_addr+rel_pos);
	data_size = de_getui32le(d->cur_base_addr+rel_pos+4);
	de_dbg(c, " resource data virt. addr=%d (0x%08x), size=%d\n",
		(int)data_virt_addr, (unsigned int)data_virt_addr, (int)data_size);

	data_real_offset = data_virt_addr - d->cur_section_virt_addr + d->cur_section_data_offset;
	de_dbg(c, " data offset in file: %d\n",
		(int)data_real_offset);

	if(data_size>DE_MAX_FILE_SIZE) return;

	switch(type_id) {
	case 2:
		do_extract_BITMAP(c, d, data_real_offset, data_size);
		break;
	//case 14:  // GROUP_ICON
	//	break;
	}
}

static void do_resource_dir_table(deark *c, lctx *d, de_int64 pos, int level);

static void do_resource_node(deark *c, lctx *d, de_int64 rel_pos, int level)
{
	de_int64 name_or_id;
	de_int64 next_offset;
	int has_name, is_branch_node;

	d->rsrc_item_count++;
	if(d->rsrc_item_count>10000) {
		// Loops are possible. This is an emergency brake.
		de_err(c, "Too many resources.\n");
		return;
	}

	has_name = 0;
	is_branch_node = 0;

	name_or_id = de_getui32le(d->cur_base_addr+rel_pos);
	if(name_or_id & 0x80000000) {
		has_name = 1;
		name_or_id -= 0x80000000;
	}
	next_offset = de_getui32le(d->cur_base_addr+rel_pos+4);
	if(next_offset & 0x80000000) {
		is_branch_node = 1;
		next_offset -= 0x80000000;
	}

	if(level==1) {
		d->cur_rsrc_type = name_or_id;
	}

	// TODO: If a resource has a name (at level 2), we should read it so we
	// can use it for the filename.

	de_dbg(c, "level %d node at %d(%d) id=%d next-offset=%d is-named=%d is-branch=%d\n",
		level, (int)(d->cur_base_addr+rel_pos), (int)rel_pos,
		(int)name_or_id, (int)next_offset, has_name, is_branch_node);

	// If high bit is q, we need to go deeper.
	if(is_branch_node) {
		do_resource_dir_table(c, d, next_offset, level+1);
	}
	else {
		do_resource_data_entry(c, d, next_offset);
	}
}

static void do_resource_dir_table(deark *c, lctx *d, de_int64 rel_pos, int level)
{
	de_int64 named_node_count;
	de_int64 unnamed_node_count;
	de_int64 node_count;
	de_int64 i;

	// 16-byte "Resource node header" a.k.a "Resource directory table"

	if(level>3) {
		de_warn(c, "Resource tree too deep\n");
		return;
	}

	de_dbg(c, "resource directory table at %d(%d), level=%d\n",
		(unsigned int)(d->cur_base_addr+rel_pos), (unsigned int)rel_pos, level);

	named_node_count = de_getui16le(d->cur_base_addr+rel_pos+12);
	unnamed_node_count = de_getui16le(d->cur_base_addr+rel_pos+14);
	de_dbg(c, "number of node entries: named=%d, unnamed=%d\n", (unsigned int)named_node_count,
		(unsigned int)unnamed_node_count);

	node_count = named_node_count + unnamed_node_count;
	
	// An array of 8-byte "Resource node entries" follows the Resource node header.
	for(i=0; i<node_count; i++) {
		do_resource_node(c, d, rel_pos+16+8*i, level);
	}
}

static void do_resource_section(deark *c, lctx *d, de_int64 pos, de_int64 len)
{
	d->cur_base_addr = pos;
	d->rsrc_item_count = 0;
	do_resource_dir_table(c, d, 0, 1);
}

static void do_section_header(deark *c, lctx *d, de_int64 pos)
{
	de_byte name_raw[8];
	char name[9];
	de_int64 i;
	de_int64 section_data_size;

	de_dbg(c, "section header at %d\n", (unsigned int)pos);

	de_read(name_raw, pos, 8); // Section name

	if(c->debug_level>0) {
		for(i=0; i<8; i++) {
			if(name_raw[i]==0 || (name_raw[i]>=32 && name_raw[i]<=126))
				name[i] = (char)name_raw[i];
			else
				name[i] = '_';
		}
		name[8] = '\0';
		de_dbg(c, "section name: \"%s\"\n", name);
	}

	d->cur_section_virt_addr = de_getui32le(pos+12);
	section_data_size = de_getui32le(pos+16);
	d->cur_section_data_offset = de_getui32le(pos+20);

	de_dbg(c, "section virt. addr=%d (0x%08x)\n", (int)d->cur_section_virt_addr, (unsigned int)d->cur_section_virt_addr);
	de_dbg(c, "section data offset=%d, size=%d\n", (int)d->cur_section_data_offset, (int)section_data_size);

	if(!de_memcmp(name_raw, ".rsrc\0", 5)) {
		do_resource_section(c, d, d->cur_section_data_offset, section_data_size);
	}
}

static void do_section_table_pe(deark *c, lctx *d)
{
	de_int64 pos;
	de_int64 i;

	pos = d->sections_offset;
	de_dbg(c, "section table at %d\n", (unsigned int)pos);
	for(i=0; i<d->number_of_sections; i++) {
		do_section_header(c, d, pos + 40*i);
	}
}

static void de_run_exe(deark *c, const char *params)
{
	lctx *d = NULL;

	de_dbg(c, "In EXE module\n");
	d = de_malloc(c, sizeof(lctx));

	do_fileheader(c, d);

	if(d->fmt==EXE_FMT_PE && d->sections_offset>0) {
		do_section_table_pe(c, d);
	}

	de_free(c, d);
}

static int de_identify_exe(deark *c)
{
	de_byte buf[2];
	de_read(buf, 0, 2);

	if(buf[0]=='M' && buf[1]=='Z') return 80;
	return 0;
}

void de_module_exe(deark *c, struct deark_module_info *mi)
{
	mi->id = "exe";
	mi->run_fn = de_run_exe;
	mi->identify_fn = de_identify_exe;
}
