/*
 * presets.c -- Apply presets
 *
 * Copyright (C) 2002 Gabriel Bouvigne / Lame project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include "lame.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "util.h"




int apply_abr_preset(lame_global_flags*  gfp, int preset)
{
    int k; 

    typedef struct {
        int    abr_kbps;
        int    expZ;
        int    quant_comp;
        int    safejoint;
        double nsmsfix;
        double st_lrm; /*short threshold*/
        double st_s;
        double nsbass;
        double scale;
        double ath_curve;
        double interch;
    } abr_presets_t;



    /* Switch mappings for ABR mode */
    const abr_presets_t abr_switch_map [] = {
        /* kbps Z  quant safejoint nsmsfix st_lrm  st_s  ns-bass scale ath4_curve, interch */
        {   8,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0012 }, /*   8, impossible to use in stereo */
        {  16,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0010 }, /*  16 */
        {  24,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0010 }, /*  24 */
        {  32,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0010 }, /*  32 */
        {  40,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0009 }, /*  40 */
        {  48,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0009 }, /*  48 */
        {  56,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0008 }, /*  56 */
        {  64,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        11,  0.0008 }, /*  64 */
        {  80,  1, 3,    0,        0   ,    4.50, 30   ,  0,      0.95,        10,  0.0007 }, /*  80 */
        {  96,  1, 1,    0,        0   ,    4.50, 30   , -2,      0.95,         8,  0.0006 }, /*  96 */
        { 112,  1, 1,    0,        0   ,    4.50, 30   , -4,      0.95,         7,  0.0005 }, /* 112 */
        { 128,  1, 1,    0,        0   ,    4.40, 25   , -6,      0.95,         5,  0.0002 }, /* 128 */
        { 160,  1, 1,    1,        1.64,    4.25, 15   , -4,      0.95,         4,  0      }, /* 160 */
        { 192,  1, 1,    1,        1.38,    3.50, 15   , -2,      0.97,         3,  0      }, /* 192 */
        { 224,  1, 3,    1,        1.10,    3.20, 15   ,  0,      0.98,         2,  0      }, /* 224 */
        { 256,  0, 3,    1,        0.85,    3.10, 15   ,  0,      1.00,         1,  0      }, /* 256 */
        { 320,  0, 3,    1,        0.6 ,    3.00, 15   ,  0,      1.00,         0,  0      }  /* 320 */
                                       };

    
    /* Variables for the ABR stuff */
    int r;
    int actual_bitrate = preset;

    r= nearestBitrateFullIndex(preset);


    lame_set_VBR(gfp, vbr_abr); 
    lame_set_VBR_mean_bitrate_kbps(gfp, (actual_bitrate));
    lame_set_VBR_mean_bitrate_kbps(gfp, Min(lame_get_VBR_mean_bitrate_kbps(gfp), 320)); 
    lame_set_VBR_mean_bitrate_kbps(gfp, Max(lame_get_VBR_mean_bitrate_kbps(gfp), 8)); 
    lame_set_brate(gfp, lame_get_VBR_mean_bitrate_kbps(gfp));


    lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);
    lame_set_experimentalZ(gfp, abr_switch_map[r].expZ);
    lame_set_quant_comp(gfp, abr_switch_map[r].quant_comp);
    lame_set_quant_comp_short( gfp, abr_switch_map[r].quant_comp);
    lame_set_quality(gfp, 3);
    lame_set_mode(gfp, JOINT_STEREO);
    lame_set_interChRatio(gfp, abr_switch_map[r].interch);

    lame_set_ATHtype(gfp, 4);
    lame_set_ATHcurve(gfp, abr_switch_map[r].ath_curve);


    if (abr_switch_map[r].safejoint > 0)
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 2); /* safejoint */

    lame_set_short_threshold(gfp, abr_switch_map[r].st_lrm, abr_switch_map[r].st_s);

    if (abr_switch_map[r].nsmsfix > 0)
            (void) lame_set_msfix( gfp, abr_switch_map[r].nsmsfix );

    /* ns-bass tweaks */
    if (abr_switch_map[r].nsbass != 0) {
        k = (int)(abr_switch_map[r].nsbass * 4);
        if (k < 0) k += 64;
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | (k << 2));
    }

    /* ABR seems to have big problems with clipping, especially at low bitrates */
    /* so we compensate for that here by using a scale value depending on bitrate */
    if (abr_switch_map[r].scale != 1)
        (void) lame_set_scale( gfp, abr_switch_map[r].scale );

    return preset;
}





int apply_preset(lame_global_flags*  gfp, int preset)
{
    switch (preset) {
    case PORTABLE: {
	    lame_set_VBR(gfp, vbr_rh);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 17500);
	    lame_set_mode(gfp, JOINT_STEREO);

        lame_set_short_threshold(gfp, 4.40f, 25.0f);
        lame_set_quant_comp(gfp, 3);
        lame_set_quant_comp_short(gfp, 3);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);
        lame_set_maskingadjust( gfp, 1.1 );
        lame_set_maskingadjust_short( gfp, .9 );
        lame_set_interChRatio(gfp, 0.0002);
        lame_set_ATHlower( gfp, -17 );
        lame_set_ATHtype(gfp, 4);
        lame_set_ATHcurve(gfp, 5);
	    lame_set_athaa_sensitivity(gfp, -16);
        
	    lame_set_experimentalY(gfp, 1);
	    lame_set_substep( gfp, 1 );

	    return preset;

    }
    case PORTABLE1: {
	    lame_set_VBR(gfp, vbr_rh);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 17500);
	    lame_set_mode(gfp, JOINT_STEREO);

        lame_set_short_threshold(gfp, 4.40f, 25.0f);
        lame_set_quant_comp(gfp, 3);
        lame_set_quant_comp_short(gfp, 3);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);
        lame_set_maskingadjust( gfp, 1.2 );
        lame_set_maskingadjust_short( gfp, 1 );
        lame_set_interChRatio(gfp, 0.0002);
        lame_set_ATHlower( gfp, -21.5 );
        lame_set_ATHtype(gfp, 4);
        lame_set_ATHcurve(gfp, 5);
	    lame_set_athaa_sensitivity(gfp, -16);
        
	    lame_set_experimentalY(gfp, 1);

	    return preset;

    }
    case MEDIUM: {
	    lame_set_VBR(gfp, vbr_rh);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 18000);
	    lame_set_mode(gfp, JOINT_STEREO);

        lame_set_short_threshold(gfp, 4.25f, 15.0f);
        lame_set_quant_comp(gfp, 3);
        lame_set_quant_comp_short(gfp, 3);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 2); /* safejoint */
        lame_set_msfix( gfp, 1.62 );
        lame_set_maskingadjust( gfp, 1.1 );
        lame_set_maskingadjust_short( gfp, .7 );
        lame_set_ATHlower( gfp, -11 );
        lame_set_ATHtype(gfp, 4);
        lame_set_ATHcurve(gfp, 4);
	    lame_set_athaa_sensitivity(gfp, -8);
        
	    lame_set_experimentalY(gfp, 1);

	    return preset;

    }
    case MEDIUM_FAST: {
	    lame_set_VBR(gfp, vbr_mtrh);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 18000);
	    lame_set_mode(gfp, JOINT_STEREO);
	    lame_set_VBR_min_bitrate_kbps(gfp, 64);
	    lame_set_athaa_sensitivity(gfp, -11);
	    lame_set_msfix(gfp, 3);
	    lame_set_VBR_q(gfp, 3);
	    lame_set_experimentalY(gfp, 1);
	    return preset;
    }
    case STANDARD: {
	    lame_set_VBR(gfp, vbr_rh);
	    lame_set_VBR_min_bitrate_kbps(gfp, 96); /*ideally, we should get rid of this*/

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 19000);
	    lame_set_mode(gfp, JOINT_STEREO);

        lame_set_short_threshold(gfp, 3.5f, 15.0f);
        lame_set_quant_comp(gfp, 3);
        lame_set_quant_comp_short(gfp, 3);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 2); /* safejoint */
        lame_set_msfix( gfp, 1.38 );
        lame_set_maskingadjust( gfp, .9 );
        lame_set_maskingadjust_short( gfp, -1 );
        lame_set_ATHlower( gfp, -1.5 );
        lame_set_ATHtype(gfp, 4);
        lame_set_ATHcurve(gfp, 2);
        /* modify sfb21 by 3.75 dB plus ns-treble=0                  */
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | (15 << 20));

	    return preset;
    }
    case STANDARD_FAST: {
	    lame_set_VBR(gfp, vbr_mtrh);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 19000);
	    lame_set_mode(gfp, JOINT_STEREO);
	    lame_set_VBR_min_bitrate_kbps(gfp, 128);
	    return preset;
    }
    case EXTREME: {
	    lame_set_VBR(gfp, vbr_rh);
	    lame_set_VBR_min_bitrate_kbps(gfp, 128);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 19500);
	    lame_set_mode(gfp, JOINT_STEREO);

        lame_set_short_threshold(gfp, 3.1f, 15.0f);
        lame_set_quant_comp(gfp, 3);
        lame_set_quant_comp_short(gfp, 3);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1);
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 2); /* safejoint */
        lame_set_msfix( gfp, 0.85);
        lame_set_maskingadjust( gfp, -0.8 );
        lame_set_maskingadjust_short( gfp, -2.9 );
        lame_set_ATHlower( gfp, 2 );
        lame_set_ATHtype(gfp, 4);
        lame_set_ATHcurve(gfp, 1);
        /* modify sfb21 by 3 dB plus ns-treble=0                  */
        lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | (12 << 20));

	    return preset;
    }
    case EXTREME_FAST: {
	    lame_set_VBR(gfp, vbr_mtrh);

	    lame_set_quality(gfp, 3);
	    lame_set_lowpassfreq(gfp, 19500);
	    lame_set_mode(gfp, JOINT_STEREO);
	    lame_set_VBR_min_bitrate_kbps(gfp, 128);

	    return preset;
    }
    case INSANE: {
        return apply_abr_preset(gfp, 320);
    }
    case R3MIX: {
	    lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | 1); /*nspsytune*/
	    (void) lame_set_scale( gfp, 0.98 ); /* --scale 0.98*/

	    lame_set_exp_nspsytune(gfp, lame_get_exp_nspsytune(gfp) | (8 << 20));

	    lame_set_VBR(gfp,vbr_mtrh); 
	    lame_set_VBR_q(gfp,1);
	    lame_set_quality( gfp, 3);
	    lame_set_lowpassfreq(gfp,19500);
	    lame_set_mode( gfp, JOINT_STEREO );
	    lame_set_ATHtype( gfp, 3 );
	    lame_set_VBR_min_bitrate_kbps(gfp,96);
	    return preset;
    }
    default:
	break;
    }

    if ((preset >= 8) && (preset <=320))
        return apply_abr_preset(gfp, preset);


    return preset;
}

