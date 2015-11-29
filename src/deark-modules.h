// This file is part of Deark, by Jason Summers.
// This software is in the public domain. See the file COPYING for details.

// When you add a module, list it in this file and deark-modules.c.

#include "deark-private.h"

void de_module_unsupported(deark *c, struct deark_module_info *mi);
void de_module_jpeg(deark *c, struct deark_module_info *mi);
void de_module_jpegscan(deark *c, struct deark_module_info *mi);
void de_module_tiff(deark *c, struct deark_module_info *mi);
void de_module_os2bmp(deark *c, struct deark_module_info *mi);
void de_module_eps(deark *c, struct deark_module_info *mi);
void de_module_zlib(deark *c, struct deark_module_info *mi);
void de_module_bsave(deark *c, struct deark_module_info *mi);
void de_module_psd(deark *c, struct deark_module_info *mi);
void de_module_copy(deark *c, struct deark_module_info *mi);
void de_module_msp(deark *c, struct deark_module_info *mi);
void de_module_pcpaint(deark *c, struct deark_module_info *mi);
void de_module_graspgl(deark *c, struct deark_module_info *mi);
void de_module_amigaicon(deark *c, struct deark_module_info *mi);
void de_module_macpaint(deark *c, struct deark_module_info *mi);
void de_module_epocimage(deark *c, struct deark_module_info *mi);
void de_module_psionpic(deark *c, struct deark_module_info *mi);
void de_module_psionapp(deark *c, struct deark_module_info *mi);
void de_module_hpicn(deark *c, struct deark_module_info *mi);
void de_module_exe(deark *c, struct deark_module_info *mi);
void de_module_ani(deark *c, struct deark_module_info *mi);
void de_module_jpeg2000(deark *c, struct deark_module_info *mi);
void de_module_fnt(deark *c, struct deark_module_info *mi);
void de_module_zip(deark *c, struct deark_module_info *mi);
void de_module_xpuzzle(deark *c, struct deark_module_info *mi);
void de_module_dcx(deark *c, struct deark_module_info *mi);
void de_module_nol(deark *c, struct deark_module_info *mi);
void de_module_ngg(deark *c, struct deark_module_info *mi);
void de_module_npm(deark *c, struct deark_module_info *mi);
void de_module_nlm(deark *c, struct deark_module_info *mi);
void de_module_nsl(deark *c, struct deark_module_info *mi);
void de_module_grob(deark *c, struct deark_module_info *mi);
void de_module_t64(deark *c, struct deark_module_info *mi);
void de_module_atari_cas(deark *c, struct deark_module_info *mi);
void de_module_atr(deark *c, struct deark_module_info *mi);
void de_module_mrw(deark *c, struct deark_module_info *mi);
void de_module_cardfile(deark *c, struct deark_module_info *mi);
void de_module_winzle(deark *c, struct deark_module_info *mi);
void de_module_bob(deark *c, struct deark_module_info *mi);
void de_module_alias_pix(deark *c, struct deark_module_info *mi);
void de_module_tivariable(deark *c, struct deark_module_info *mi);
void de_module_basic_c64(deark *c, struct deark_module_info *mi);
void de_module_applevol(deark *c, struct deark_module_info *mi);
void de_module_hr(deark *c, struct deark_module_info *mi);
void de_module_ico(deark *c, struct deark_module_info *mi);
void de_module_ripicon(deark *c, struct deark_module_info *mi);
void de_module_rpm(deark *c, struct deark_module_info *mi);
void de_module_qtif(deark *c, struct deark_module_info *mi);
void de_module_ilbm(deark *c, struct deark_module_info *mi);
void de_module_lss16(deark *c, struct deark_module_info *mi);
void de_module_printshop(deark *c, struct deark_module_info *mi);
void de_module_newprintshop(deark *c, struct deark_module_info *mi);
void de_module_printmaster(deark *c, struct deark_module_info *mi);
void de_module_icns(deark *c, struct deark_module_info *mi);
void de_module_awbm(deark *c, struct deark_module_info *mi);
void de_module_vbm(deark *c, struct deark_module_info *mi);
void de_module_fp_art(deark *c, struct deark_module_info *mi);
void de_module_pgx(deark *c, struct deark_module_info *mi);
void de_module_pf_pgf(deark *c, struct deark_module_info *mi);
void de_module_pgc(deark *c, struct deark_module_info *mi);
void de_module_binhex(deark *c, struct deark_module_info *mi);
void de_module_pcx(deark *c, struct deark_module_info *mi);
void de_module_bpg(deark *c, struct deark_module_info *mi);
void de_module_shg(deark *c, struct deark_module_info *mi);
void de_module_rosprite(deark *c, struct deark_module_info *mi);
void de_module_png(deark *c, struct deark_module_info *mi);
void de_module_ar(deark *c, struct deark_module_info *mi);
void de_module_ybm(deark *c, struct deark_module_info *mi);
void de_module_prismpaint(deark *c, struct deark_module_info *mi);
void de_module_degas(deark *c, struct deark_module_info *mi);
void de_module_ansiart(deark *c, struct deark_module_info *mi);
void de_module_olpc565(deark *c, struct deark_module_info *mi);
void de_module_rsc(deark *c, struct deark_module_info *mi);
void de_module_gemraster(deark *c, struct deark_module_info *mi);
void de_module_xbin(deark *c, struct deark_module_info *mi);
void de_module_graspfont(deark *c, struct deark_module_info *mi);
void de_module_pff2(deark *c, struct deark_module_info *mi);
void de_module_gemfont(deark *c, struct deark_module_info *mi);
void de_module_ftc(deark *c, struct deark_module_info *mi);
void de_module_tinystuff(deark *c, struct deark_module_info *mi);
void de_module_neochrome(deark *c, struct deark_module_info *mi);
void de_module_neochrome_ani(deark *c, struct deark_module_info *mi);
void de_module_fpaint_pi4(deark *c, struct deark_module_info *mi);
void de_module_fpaint_pi9(deark *c, struct deark_module_info *mi);
void de_module_atari_pi7(deark *c, struct deark_module_info *mi);
void de_module_eggpaint(deark *c, struct deark_module_info *mi);
void de_module_indypaint(deark *c, struct deark_module_info *mi);
void de_module_tga(deark *c, struct deark_module_info *mi);
void de_module_tim(deark *c, struct deark_module_info *mi);
void de_module_insetpix(deark *c, struct deark_module_info *mi);
void de_module_alphabmp(deark *c, struct deark_module_info *mi);
void de_module_wmf(deark *c, struct deark_module_info *mi);
void de_module_emf(deark *c, struct deark_module_info *mi);
void de_module_pict(deark *c, struct deark_module_info *mi);
void de_module_iim(deark *c, struct deark_module_info *mi);
void de_module_falcon_xga(deark *c, struct deark_module_info *mi);
void de_module_base16(deark *c, struct deark_module_info *mi);
void de_module_base64(deark *c, struct deark_module_info *mi);
void de_module_uuencode(deark *c, struct deark_module_info *mi);
void de_module_xxencode(deark *c, struct deark_module_info *mi);
void de_module_ascii85(deark *c, struct deark_module_info *mi);
void de_module_pm_xv(deark *c, struct deark_module_info *mi);
