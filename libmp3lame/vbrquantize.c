/*
 *	MP3 quantization
 *
 *	Copyright (c) 1999 Mark Taylor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include "util.h"
#include "l3side.h"
#include "reservoir.h"
#include "quantize_pvt.h"
#include "vbrquantize.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif



typedef union {
    float f;
    int   i;
} fi_union;

#define MAGIC_FLOAT (65536*(128))
#define MAGIC_INT    0x4b000000

#ifdef TAKEHIRO_IEEE754_HACK

#define DUFFBLOCKMQ() do { \
        xp = xr34[0] * sfpow34_p1; \
        xe = xr34[0] * sfpow34_eq; \
        xm = xr34[0] * sfpow34_m1; \
        if (xm > IXMAX_VAL)  \
            return -1; \
        xp += MAGIC_FLOAT; \
        xe += MAGIC_FLOAT; \
        xm += MAGIC_FLOAT; \
        fi[0].f = xp; \
        fi[1].f = xe; \
        fi[2].f = xm; \
        fi[0].f = xp + (adj43asm - MAGIC_INT)[fi[0].i]; \
        fi[1].f = xe + (adj43asm - MAGIC_INT)[fi[1].i]; \
        fi[2].f = xm + (adj43asm - MAGIC_INT)[fi[2].i]; \
        fi[0].i -= MAGIC_INT; \
        fi[1].i -= MAGIC_INT; \
        fi[2].i -= MAGIC_INT; \
        x0 = fabs(xr[0]); \
        xp = x0 - pow43[fi[0].i] * sfpow_p1; \
        xe = x0 - pow43[fi[1].i] * sfpow_eq; \
        xm = x0 - pow43[fi[2].i] * sfpow_m1; \
        xp *= xp; \
        xe *= xe; \
        xm *= xm; \
        xfsf_eq = Max(xfsf_eq, xe); \
        xfsf_p1 = Max(xfsf_p1, xp); \
        xfsf_m1 = Max(xfsf_m1, xm); \
        ++xr; \
        ++xr34; \
    } while(0)  

#define DUFFBLOCK() do { \
        xp = xr34[0] * sfpow34_p1; \
        xe = xr34[0] * sfpow34_eq; \
        xm = xr34[0] * sfpow34_m1; \
        if (xm > IXMAX_VAL)  \
            return -1; \
        xp += MAGIC_FLOAT; \
        xe += MAGIC_FLOAT; \
        xm += MAGIC_FLOAT; \
        fi[0].f = xp; \
        fi[1].f = xe; \
        fi[2].f = xm; \
        fi[0].f = xp + (adj43asm - MAGIC_INT)[fi[0].i]; \
        fi[1].f = xe + (adj43asm - MAGIC_INT)[fi[1].i]; \
        fi[2].f = xm + (adj43asm - MAGIC_INT)[fi[2].i]; \
        fi[0].i -= MAGIC_INT; \
        fi[1].i -= MAGIC_INT; \
        fi[2].i -= MAGIC_INT; \
        x0 = fabs(xr[0]); \
        xp = x0 - pow43[fi[0].i] * sfpow_p1; \
        xe = x0 - pow43[fi[1].i] * sfpow_eq; \
        xm = x0 - pow43[fi[2].i] * sfpow_m1; \
        xfsf_p1 += xp * xp; \
        xfsf_eq += xe * xe; \
        xfsf_m1 += xm * xm; \
        ++xr; \
        ++xr34; \
    } while(0)  

#else

/*********************************************************************
 * XRPOW_FTOI is a macro to convert floats to ints.  
 * if XRPOW_FTOI(x) = nearest_int(x), then QUANTFAC(x)=adj43asm[x]
 *                                         ROUNDFAC= -0.0946
 *
 * if XRPOW_FTOI(x) = floor(x), then QUANTFAC(x)=asj43[x]   
 *                                   ROUNDFAC=0.4054
 *********************************************************************/
#  define QUANTFAC(rx)  adj43[rx]
#  define ROUNDFAC 0.4054
#  define XRPOW_FTOI(src,dest) ((dest) = (int)(src))


#endif

/*  caution: a[] will be resorted!!
 */
FLOAT8 select_kth(FLOAT8 a[], int N, int k)
{
    int i, j, l, r;
    FLOAT8 v, w;
    
    l = 0;
    r = N-1;
    while (r > l) {
        v = a[r];
        i = l-1;
        j = r;
        for (;;) {
            while (a[++i] < v) /*empty*/;
            while (a[--j] > v) /*empty*/;
            if (i >= j) 
                break;
            /* swap i and j */
            w = a[i];
            a[i] = a[j];
            a[j] = w;
        }
        /* swap i and r */
        w = a[i];
        a[i] = a[r];
        a[r] = w;
        if (i >= k) 
            r = i-1;
        if (i <= k) 
            l = i+1;
    }
    return a[k];
}




static FLOAT8
calc_sfb_noise(const FLOAT8 *xr, const FLOAT8 *xr34, int bw, int sf)
{
    int j;
    fi_union fi; 
    FLOAT8 temp;
    FLOAT8 xfsf = 0;
    FLOAT8 sfpow, sfpow34;

    sfpow   =  POW20(sf+210); /*pow(2.0,sf/4.0); */
    sfpow34 = IPOW20(sf+210); /*pow(sfpow,-3.0/4.0);*/

    for ( j = 0; j < bw ; ++j ) {
        if ( xr34[j]*sfpow34 > IXMAX_VAL ) return -1;

#ifdef TAKEHIRO_IEEE754_HACK
        temp   = sfpow34*xr34[j];
        temp  += MAGIC_FLOAT; 
        fi.f  = temp;
        fi.f  = temp + (adj43asm - MAGIC_INT)[fi.i];
        fi.i -= MAGIC_INT;
#else
        temp = xr34[j]*sfpow34;
        XRPOW_FTOI(temp, fi.i);
        XRPOW_FTOI(temp + QUANTFAC(fi.i), fi.i);
#endif

        temp = fabs(xr[j])- pow43[fi.i]*sfpow;
        xfsf += temp*temp;
    }
    return xfsf;
}




static FLOAT8
calc_sfb_noise_mq( const FLOAT8 *xr, const FLOAT8 *xr34, int bw, int sf, 
                   int mq, FLOAT8 *scratch )
{
    int j, k;
    fi_union fi; 
    FLOAT8 temp;
    FLOAT8 sfpow, sfpow34, xfsfm = 0, xfsf = 0;

    sfpow   =  POW20(sf+210); /*pow(2.0,sf/4.0); */
    sfpow34 = IPOW20(sf+210); /*pow(sfpow,-3.0/4.0);*/

    for ( j = 0; j < bw; ++j ) {
        if ( xr34[j]*sfpow34 > IXMAX_VAL ) return -1;

#ifdef TAKEHIRO_IEEE754_HACK
        temp  = sfpow34*xr34[j];
        temp += MAGIC_FLOAT; 
        fi.f  = temp;
        fi.f  = temp + (adj43asm - MAGIC_INT)[fi.i];
        fi.i -= MAGIC_INT;
#else
        temp = xr34[j]*sfpow34;
        XRPOW_FTOI(temp, fi.i);
        XRPOW_FTOI(temp + QUANTFAC(fi.i), fi.i);
#endif

        temp = fabs(xr[j])- pow43[fi.i]*sfpow;
        temp *= temp;
  
        scratch[j] = temp;  
        if ( xfsfm < temp ) xfsfm = temp;
        xfsf += temp;
    }
    if ( mq == 1 ) return bw*select_kth(scratch,bw,bw*13/16);
    
    xfsf /= bw;
    for ( k = 1, j = 0; j < bw; ++j ) {
        if ( scratch[j] > xfsf ) { 
            xfsfm += scratch[j];
            ++k; 
        }
    }
    return xfsfm/k * bw;
}




static FLOAT8
calc_sfb_noise_ave(const FLOAT8 *xr, const FLOAT8 *xr34, int bw, int sf)
{
    double xp;
    double xe;
    double xm;
#ifdef TAKEHIRO_IEEE754_HACK
    double x0;
#endif
    int xx[3], j;
    fi_union *fi = (fi_union *)xx; 
    FLOAT8 sfpow34_eq, sfpow34_p1, sfpow34_m1;
    FLOAT8 sfpow_eq, sfpow_p1, sfpow_m1;
    FLOAT8 xfsf_eq = 0, xfsf_p1 = 0, xfsf_m1 = 0;

    sfpow_eq = POW20(sf + 210); /*pow(2.0,sf/4.0); */
    sfpow_m1 = sfpow_eq * .8408964153;  /* pow(2,(sf-1)/4.0) */
    sfpow_p1 = sfpow_eq * 1.189207115;  
    
    sfpow34_eq = IPOW20(sf + 210); /*pow(sfpow,-3.0/4.0);*/
    sfpow34_m1 = sfpow34_eq * 1.13878863476;       /* .84089 ^ -3/4 */
    sfpow34_p1 = sfpow34_eq * 0.878126080187;

#ifdef TAKEHIRO_IEEE754_HACK
    /*
     *  loop unrolled into "Duff's Device".   Robert Hegemann
     */    
    j = (bw+3) / 4;
    switch (bw % 4) {
        default:
        case 0: do{ DUFFBLOCK();
        case 3:     DUFFBLOCK();
        case 2:     DUFFBLOCK();
        case 1:     DUFFBLOCK(); } while (--j);
    }
#else
    for (j = 0; j < bw; ++j) {

        if (xr34[j]*sfpow34_m1 > IXMAX_VAL) return -1;

        xe = xr34[j]*sfpow34_eq;
        XRPOW_FTOI(xe, fi[0].i);
        XRPOW_FTOI(xe + QUANTFAC(fi[0].i), fi[0].i);
        xe = fabs(xr[j])- pow43[fi[0].i]*sfpow_eq;
        xe *= xe;

        xp = xr34[j]*sfpow34_p1;
        XRPOW_FTOI(xp, fi[0].i);
        XRPOW_FTOI(xp + QUANTFAC(fi[0].i), fi[0].i);
        xp = fabs(xr[j])- pow43[fi[0].i]*sfpow_p1;
        xp *= xp;

        xm = xr34[j]*sfpow34_m1;
        XRPOW_FTOI(xm, fi[0].i);
        XRPOW_FTOI(xm + QUANTFAC(fi[0].i), fi[0].i);
        xm = fabs(xr[j])- pow43[fi[0].i]*sfpow_m1;
        xm *= xm;

        xfsf_eq += xe;
        xfsf_p1 += xp;
        xfsf_m1 += xm;
    }
#endif

    if ( xfsf_eq < xfsf_p1 ) xfsf_eq = xfsf_p1;
    if ( xfsf_eq < xfsf_m1 ) xfsf_eq = xfsf_m1;
    return xfsf_eq;
}



INLINE int
find_scalefac( const FLOAT8 *xr, const FLOAT8 *xr34, FLOAT8 l3_xmin, int bw )
{
    FLOAT8 xfsf;
    int i, sf, sf_ok, delsf;

    /* search will range from sf:  -209 -> 45  */
    sf = -82;
    delsf = 128;

    sf_ok = 10000;
    for (i = 0; i < 7; ++i) {
        delsf /= 2;
        xfsf = calc_sfb_noise( xr, xr34, bw, sf );

        if (xfsf < 0) {
            /* scalefactors too small */
            sf += delsf;
        }
        else {
            if (xfsf > l3_xmin) {
                /* distortion.  try a smaller scalefactor */
                sf -= delsf;
            }
            else {
                sf_ok = sf;
                sf += delsf;
            }
        }
    } 

    /*  returning a scalefac without distortion, if possible
     */
    return sf_ok > 45 ? sf : sf_ok;
}

INLINE int
find_scalefac_mq( const FLOAT8 *xr, const FLOAT8 *xr34, FLOAT8 l3_xmin, 
                  int bw, int mq, FLOAT8 *scratch )
{
    FLOAT8 xfsf;
    int i, sf, sf_ok, delsf;

    /* search will range from sf:  -209 -> 45  */
    sf = -82;
    delsf = 128;

    sf_ok = 10000;
    for (i = 0; i < 7; ++i) {
        delsf /= 2;
        xfsf = calc_sfb_noise_mq( xr, xr34, bw, sf, mq, scratch );

        if (xfsf < 0) {
            /* scalefactors too small */
            sf += delsf;
        }
        else {
            if (xfsf > l3_xmin) {
                /* distortion.  try a smaller scalefactor */
                sf -= delsf;
            }
            else {
                sf_ok = sf;
                sf += delsf;
            }
        }
    } 

    /*  returning a scalefac without distortion, if possible
     */
    return sf_ok > 45 ? sf : sf_ok;
}

INLINE int
find_scalefac_ave( const FLOAT8 *xr, const FLOAT8 *xr34, FLOAT8 l3_xmin, int bw )
{
    FLOAT8 xfsf; 
    int i, sf, sf_ok, delsf;

    /* search will range from sf:  -209 -> 45  */
    sf = -82;
    delsf = 128;

    sf_ok = 10000;
    for (i = 0; i < 7; ++i) {
        delsf /= 2;
        xfsf = calc_sfb_noise_ave( xr, xr34, bw, sf );

        if (xfsf < 0) {
            /* scalefactors too small */
            sf += delsf;
        }
        else{
            if (xfsf > l3_xmin) {
                /* distortion.  try a smaller scalefactor */
                sf -= delsf;
            }
            else {
                sf_ok = sf;
                sf += delsf;
            }
        }
    } 

    /*  returning a scalefac without distortion, if possible
     */
    return sf_ok > 45 ? sf : sf_ok;
}


/**
 *  Robert Hegemann 2001-05-01
 *  calculates quantization step size determined by allowed masking
 */
INLINE int 
calc_scalefac( FLOAT8 l3_xmin, int bw, FLOAT8 preset_tune )
{
    FLOAT8 const c = (preset_tune > 0 ? preset_tune
		                              : 5.799142446);   // 10 * 10^(2/3) * log10(4/3)
    return (int)(c*log10(l3_xmin/bw)-.5);
}



static const int max_range_short[SBMAX_s] =
{15, 15, 15, 15, 15, 15, 7, 7, 7, 7, 7, 7, 0 };

static const int max_range_long[SBMAX_l] =
{15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0};

static const int max_range_long_lsf_pretab[SBMAX_l] =
{ 7,7,7,7,7,7, 3,3,3,3,3, 0,0,0,0, 0,0,0, 0,0,0, 0 };
    


/*
    sfb=0..5  scalefac < 16 
    sfb>5     scalefac < 8

    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    ol_sf =  (cod_info->global_gain-210.0);
    ol_sf -= 8*cod_info->subblock_gain[i];
    ol_sf -= ifqstep*scalefac[gr][ch].s[sfb][i];

*/
INLINE int 
compute_scalefacs_short(
    int sf[][3], const gr_info *cod_info, int scalefac[][3], int *sbg )
{
    const int maxrange1 = 15, maxrange2 = 7;
    int maxrange, maxover = 0;
    int sfb, i;
    int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;

    for (i = 0; i < 3; ++i) {
        int maxsf1 = 0, maxsf2 = 0, minsf = 1000;
        /* see if we should use subblock gain */
        for (sfb = 0; sfb < 6; ++sfb) {         /* part 1 */
            if (maxsf1 < -sf[sfb][i]) maxsf1 = -sf[sfb][i];
            if (minsf  > -sf[sfb][i]) minsf  = -sf[sfb][i];
        }
        for (; sfb < SBPSY_s; ++sfb) {          /* part 2 */
            if (maxsf2 < -sf[sfb][i]) maxsf2 = -sf[sfb][i];
            if (minsf  > -sf[sfb][i]) minsf  = -sf[sfb][i];
        }

        /* boost subblock gain as little as possible so we can
         * reach maxsf1 with scalefactors 
         * 8*sbg >= maxsf1   
         */
        maxsf1 = Max (maxsf1-maxrange1*ifqstep, maxsf2-maxrange2*ifqstep);
        sbg[i] = 0;
        if (minsf  > 0) sbg[i] = floor (.125*minsf + .001);
        if (maxsf1 > 0) sbg[i] = Max (sbg[i], (maxsf1/8 + (maxsf1 % 8 != 0)));
        if (sbg[i] > 7) sbg[i] = 7;

        for (sfb = 0; sfb < SBPSY_s; ++sfb) {
            sf[sfb][i] += 8*sbg[i];

            if (sf[sfb][i] < 0) {
                maxrange = sfb < 6 ? maxrange1 : maxrange2;
                
                scalefac[sfb][i]
                     = -sf[sfb][i]/ifqstep + (-sf[sfb][i]%ifqstep != 0);
                
                if (scalefac[sfb][i] > maxrange)
                    scalefac[sfb][i] = maxrange;

                if (maxover < -(sf[sfb][i] + scalefac[sfb][i]*ifqstep)) 
                    maxover = -(sf[sfb][i] + scalefac[sfb][i]*ifqstep);
            }
        }
        scalefac[sfb][i] = 0;
    }

    return maxover;
}



/*
	  ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
	  ol_sf =  (cod_info->global_gain-210.0);
	  ol_sf -= ifqstep*scalefac[gr][ch].l[sfb];
	  if (cod_info->preflag && sfb>=11) 
	  ol_sf -= ifqstep*pretab[sfb];
*/
INLINE int
compute_scalefacs_long_lsf( int *sf, const gr_info * cod_info, int *scalefac )
{
    const int * max_range = max_range_long;
    int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    int sfb;
    int maxover;

    if (cod_info->preflag) {
        max_range = max_range_long_lsf_pretab;
        for (sfb = 11; sfb < SBPSY_l; ++sfb) 
            sf[sfb] += pretab[sfb] * ifqstep;
    }

    maxover = 0;
    for (sfb = 0; sfb < SBPSY_l; ++sfb) {

        if (sf[sfb] < 0) {
            /* ifqstep*scalefac >= -sf[sfb], so round UP */
            scalefac[sfb] = -sf[sfb]/ifqstep  + (-sf[sfb] % ifqstep != 0);
            if (scalefac[sfb] > max_range[sfb]) 
                scalefac[sfb] = max_range[sfb];
      
            /* sf[sfb] should now be positive: */
            if (-(sf[sfb] + scalefac[sfb]*ifqstep) > maxover) {
                maxover = -(sf[sfb] + scalefac[sfb]*ifqstep);
            }
        }
    }
    scalefac[sfb] = 0;  /* sfb21 */

    return maxover;
}





/*
	  ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
	  ol_sf =  (cod_info->global_gain-210.0);
	  ol_sf -= ifqstep*scalefac[gr][ch].l[sfb];
	  if (cod_info->preflag && sfb>=11) 
	  ol_sf -= ifqstep*pretab[sfb];
*/
INLINE int
compute_scalefacs_long( int *sf, const gr_info * cod_info, int *scalefac )
{
    int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    int sfb;
    int maxover;  

    if (cod_info->preflag) {
        for (sfb = 11; sfb < SBPSY_l; ++sfb) 
            sf[sfb] += pretab[sfb] * ifqstep;
    }

    maxover = 0;
    for (sfb = 0; sfb < SBPSY_l; ++sfb) {

        if (sf[sfb] < 0) {
            /* ifqstep*scalefac >= -sf[sfb], so round UP */
            scalefac[sfb] = -sf[sfb]/ifqstep  + (-sf[sfb] % ifqstep != 0);
            if (scalefac[sfb] > max_range_long[sfb]) 
                scalefac[sfb] = max_range_long[sfb];
      
            /* sf[sfb] should now be positive: */
            if (-(sf[sfb] + scalefac[sfb]*ifqstep) > maxover) {
                maxover = -(sf[sfb] + scalefac[sfb]*ifqstep);
            }
        }
    }
    scalefac[sfb] = 0;  /* sfb21 */

    return maxover;
}
  
  






/************************************************************************
 *
 * quantize and encode with the given scalefacs and global gain
 *
 * compute scalefactors, l3_enc, and return number of bits needed to encode
 *
 *
 ************************************************************************/

static int
VBR_quantize_granule( 
          lame_internal_flags * gfc,
	  gr_info             * cod_info,
          FLOAT8              * xr34, 
          int gr, int ch )
{
    int status;

    /* encode scalefacs */
    if ( gfc->is_mpeg1 ) 
        status = scale_bitcount( &cod_info->scalefac, cod_info );
    else
        status = scale_bitcount_lsf( gfc, &cod_info->scalefac, cod_info );

    if (status != 0) {
        return -1;
    }
  
    /* quantize xr34 */
    cod_info->part2_3_length = count_bits(gfc,cod_info->l3_enc,xr34,cod_info);
    if (cod_info->part2_3_length >= LARGE_BITS) return -2;
    cod_info->part2_3_length += cod_info->part2_length;


    if (gfc->use_best_huffman == 1) {
        best_huffman_divide(gfc, cod_info);
    }
    return 0;
}


  
/***********************************************************************
 *
 *      calc_short_block_vbr_sf()
 *      calc_long_block_vbr_sf()
 *
 *  Mark Taylor 2000-??-??
 *  Robert Hegemann 2000-10-25 made functions of it
 *
 ***********************************************************************/
static const int MAX_SF_DELTA = 4;    

static void
short_block_sf (
    const lame_internal_flags * gfc,
    const III_psy_xmin   * l3_xmin,
    const FLOAT8         * xr34_orig,
    const FLOAT8         * xr34,
          III_scalefac_t * vbrsf,
          int            * vbrmin,
          int            * vbrmax )
{
    int j, sfb, b;
    int vbrmean, vbrmn;
    int sf_cache[SBMAX_s];
    int scalefac_criteria;

    if (gfc->presetTune.use) {
	    /* map experimentalX settings to internal selections */
        static char const map[] = {2,1,0,3,6};
        scalefac_criteria = map[gfc->presetTune.quantcomp_current];
    }
	else {
        scalefac_criteria = gfc->VBR->quality;
    }
  
    for (j = 0, sfb = 0; sfb < SBMAX_s; ++sfb) {
        for (b = 0; b < 3; ++b) {
            const  int start = gfc->scalefac_band.s[ sfb ];
            const  int end   = gfc->scalefac_band.s[ sfb+1 ];
            const  int width = end - start;
	    
            switch( scalefac_criteria ) {
            default:
                /*  the fastest
                 */
                vbrsf->s[sfb][b] = calc_scalefac( l3_xmin->s[sfb][b], width,
					                              gfc->presetTune.quantcomp_adjust_mtrh );
                break;
            case 5:
            case 4:
            case 3:
                /*  the faster and sloppier mode to use at lower quality
                 */
                vbrsf->s[sfb][b] = find_scalefac (&xr34[j], &xr34_orig[j], 
                                              l3_xmin->s[sfb][b], width);
                break;
            case 2:
                /*  the slower and better mode to use at higher quality
                 */
                vbrsf->s[sfb][b] = find_scalefac_ave (&xr34[j], &xr34_orig[j],
                                                    l3_xmin->s[sfb][b], width);
                break;
            case 1:
                /*  maxnoise mode to use at higher quality
                 */
                vbrsf->s[sfb][b] = find_scalefac_mq (&xr34[j], &xr34_orig[j], 
                                              l3_xmin->s[sfb][b], width,
                                              1, gfc->VBR->scratch);
                break;
            case 0:
                /*  maxnoise mode to use at higher quality
                 */
                vbrsf->s[sfb][b] = find_scalefac_mq (&xr34[j], &xr34_orig[j], 
                                              l3_xmin->s[sfb][b], width,
                                              0, gfc->VBR->scratch);
                break;
            }
            j += width;
        }
    }
    
    *vbrmax = -10000;
    *vbrmin = +10000;
    
    for (b = 0; b < 3; ++b) { 

        /*  smoothing
         */
        switch( gfc->VBR->smooth ) {
        default:
        case  0: 
            break;
            
        case  1:
            /*  make working copy, get min value, select_kth_int will reorder!
             */
            for (vbrmn = +10000, sfb = 0; sfb < SBMAX_s; ++sfb) {
                sf_cache[sfb] = vbrsf->s[sfb][b];
                if (vbrmn > vbrsf->s[sfb][b])
                    vbrmn = vbrsf->s[sfb][b];
            }

            /*  find median value, take it as mean 
             */
            vbrmean = select_kth_int (sf_cache, SBMAX_s, (SBMAX_s+1)/2);

            /*  cut peaks
             */
            for (sfb = 0; sfb < SBMAX_s; ++sfb) {
                if (vbrsf->s[sfb][b] > vbrmean+(vbrmean-vbrmn))
                    vbrsf->s[sfb][b] = vbrmean+(vbrmean-vbrmn);
            }
            break;
            
        case  2:
            for (sfb = 0; sfb < SBMAX_s; ++sfb) {
	        if (sfb > 0) 
	            if (vbrsf->s[sfb][b] > vbrsf->s[sfb-1][b]+MAX_SF_DELTA)
                        vbrsf->s[sfb][b] = vbrsf->s[sfb-1][b]+MAX_SF_DELTA;
	        if (sfb < SBMAX_s-1) 
	            if (vbrsf->s[sfb][b] > vbrsf->s[sfb+1][b]+MAX_SF_DELTA)
                        vbrsf->s[sfb][b] = vbrsf->s[sfb+1][b]+MAX_SF_DELTA;
            }
            break;
        }
        
        /*  get max value
         */
        for (sfb = 0; sfb < SBMAX_s; ++sfb) { 
            if (*vbrmax < vbrsf->s[sfb][b])
                *vbrmax = vbrsf->s[sfb][b];
            if (*vbrmin > vbrsf->s[sfb][b])
                *vbrmin = vbrsf->s[sfb][b];
        }
    }
}


    /* a variation for vbr-mtrh */
static void
long_block_sf (
    const lame_internal_flags * gfc,
    const III_psy_xmin   * l3_xmin,
    const FLOAT8         * xr34_orig,
    const FLOAT8         * xr34,
          III_scalefac_t * vbrsf,
          int            * vbrmin,
          int            * vbrmax )
{
    int sfb;
    int vbrmean, vbrmn;
    int sf_cache[SBMAX_l];
    int scalefac_criteria;

    if (gfc->presetTune.use) {
	    /* map experimentalX settings to internal selections */
        static char const map[] = {2,1,0,3,6};
        scalefac_criteria = map[gfc->presetTune.quantcomp_current];
    }
	else {
        scalefac_criteria = gfc->VBR->quality;
    }
    
    for (sfb = 0; sfb < SBMAX_l; ++sfb) {
        const  int start = gfc->scalefac_band.l[ sfb ];
        const  int end   = gfc->scalefac_band.l[ sfb+1 ];
        const  int width = end - start;
        
        switch( scalefac_criteria ) {
        default:
            /*  the fastest
             */
            vbrsf->l[sfb] = calc_scalefac( l3_xmin->l[sfb], width,
				                           gfc->presetTune.quantcomp_adjust_mtrh );
            break;
        case 5:
        case 4:
        case 3:
            /*  the faster and sloppier mode to use at lower quality
             */
            vbrsf->l[sfb] = find_scalefac (&xr34[start], &xr34_orig[start], 
                                           l3_xmin->l[sfb], width);
            break;
        case 2:
            /*  the slower and better mode to use at higher quality
             */
            vbrsf->l[sfb] = find_scalefac_ave (&xr34[start], &xr34_orig[start],
                                               l3_xmin->l[sfb], width);
            break;
        case 1:
            /*  maxnoise mode to use at higher quality
             */
            vbrsf->l[sfb] = find_scalefac_mq (&xr34[start], &xr34_orig[start], 
                                           l3_xmin->l[sfb], width, 
                                           1, gfc->VBR->scratch);
            break;
        case 0:
            /*  maxnoise mode to use at higher quality
             */
            vbrsf->l[sfb] = find_scalefac_mq (&xr34[start], &xr34_orig[start], 
                                           l3_xmin->l[sfb], width, 
                                           0, gfc->VBR->scratch);
            break;
        }
    }

    switch( gfc->VBR->smooth ) {
    default:
    case  0:
        break;
        
    case  1:
        /*  make working copy, get min value, select_kth_int will reorder!
         */
        for (vbrmn = +10000, sfb = 0; sfb < SBMAX_l; ++sfb) {
            sf_cache[sfb] = vbrsf->l[sfb];
            if (vbrmn > vbrsf->l[sfb])
                vbrmn = vbrsf->l[sfb];
        }    
        /*  find median value, take it as mean 
         */
        vbrmean = select_kth_int (sf_cache, SBMAX_l, (SBMAX_l+1)/2);

        /*  cut peaks
         */
        for (sfb = 0; sfb < SBMAX_l; ++sfb) {
            if (vbrsf->l[sfb] > vbrmean+(vbrmean-vbrmn))
                vbrsf->l[sfb] = vbrmean+(vbrmean-vbrmn);
        }
        break;
        
    case  2:
        for (sfb = 0; sfb < SBMAX_l; ++sfb) {
            if (sfb > 0) 
	        if (vbrsf->l[sfb] > vbrsf->l[sfb-1]+MAX_SF_DELTA)
                    vbrsf->l[sfb] = vbrsf->l[sfb-1]+MAX_SF_DELTA;
            if (sfb < SBMAX_l-1) 
	        if (vbrsf->l[sfb] > vbrsf->l[sfb+1]+MAX_SF_DELTA)
                    vbrsf->l[sfb] = vbrsf->l[sfb+1]+MAX_SF_DELTA;
        }
        break;
    }
    
    /*  get max value
     */
    for (*vbrmin = +10000, *vbrmax = -10000, sfb = 0; sfb < SBMAX_l; ++sfb) {
        if (*vbrmax < vbrsf->l[sfb]) 
            *vbrmax = vbrsf->l[sfb];
        if (*vbrmin > vbrsf->l[sfb])
            *vbrmin = vbrsf->l[sfb];
    }
}



/******************************************************************
 *
 *  short block scalefacs
 *
 ******************************************************************/

static void 
short_block_scalefacs (
    const lame_internal_flags * gfc,
          gr_info             * cod_info,
          III_scalefac_t      * vbrsf,
          int                 * VBRmax )
{
    int sfb, maxsfb, b;
    int maxover, maxover0, maxover1, mover;
    int v0, v1;
    int minsfb;
    int vbrmax = *VBRmax;
    
    maxover0 = 0;
    maxover1 = 0;
    maxsfb = gfc->sfb21_extra ? SBMAX_s : SBPSY_s;
    for (sfb = 0; sfb < maxsfb; ++sfb) {
        for (b = 0; b < 3; ++b) {
            v0 = (vbrmax - vbrsf->s[sfb][b]) - (4*14 + 2*max_range_short[sfb]);
            v1 = (vbrmax - vbrsf->s[sfb][b]) - (4*14 + 4*max_range_short[sfb]);
            if (maxover0 < v0)
                maxover0 = v0;
            if (maxover1 < v1)
                maxover1 = v1;
        }
    }

    if ((gfc->noise_shaping == 2) && (gfc->presetTune.use && !(gfc->presetTune.athadjust_safe_noiseshaping || gfc->ATH->adjust < 1.0)))
        /* allow scalefac_scale=1 */
        mover = Min (maxover0, maxover1);
    else
        mover = maxover0; 

    vbrmax   -= mover;
    maxover0 -= mover;
    maxover1 -= mover;

    if (maxover0 == 0) 
        cod_info->scalefac_scale = 0;
    else if (maxover1 == 0)
        cod_info->scalefac_scale = 1;

    /* sf =  (cod_info->global_gain-210.0) */
    cod_info->global_gain = vbrmax + 210;
    assert(cod_info->global_gain < 256);
    
    if (cod_info->global_gain < 0) {
        cod_info->global_gain = 0; 
    }
    else
    if (cod_info->global_gain > 255) {
        cod_info->global_gain = 255;
    }
    for (sfb = 0; sfb < SBMAX_s; ++sfb) {
        for (b = 0; b < 3; ++b) {
            vbrsf->s[sfb][b] -= vbrmax;
        }
    }
    maxover = compute_scalefacs_short (vbrsf->s, cod_info, cod_info->scalefac.s,
                                           cod_info->subblock_gain);
                                           
    assert (maxover <= 0);

    /* adjust global_gain so at least 1 subblock gain = 0 */
    minsfb = 999; /* prepare for minimum search */
    for (b = 0; b < 3; ++b) 
        if (minsfb > cod_info->subblock_gain[b])
            minsfb = cod_info->subblock_gain[b];
    
    if (minsfb > cod_info->global_gain/8)
        minsfb = cod_info->global_gain/8;
    
    vbrmax                -= 8*minsfb; 
    cod_info->global_gain -= 8*minsfb;
    
    for (b = 0; b < 3; ++b)
        cod_info->subblock_gain[b] -= minsfb;
        
    *VBRmax = vbrmax;
}



/******************************************************************
 *
 *  long block scalefacs
 *
 ******************************************************************/

static void 
long_block_scalefacs (
    const lame_internal_flags * gfc,
          gr_info             * cod_info,
          III_scalefac_t      * vbrsf,
          int                 * VBRmax )
{
    const int * max_rangep;
    int sfb, maxsfb;
    int maxover, maxover0, maxover1, maxover0p, maxover1p, mover;
    int v0, v1, v0p, v1p;
    int vbrmax = *VBRmax;

    max_rangep = gfc->is_mpeg1 ? max_range_long : max_range_long_lsf_pretab;
    
    maxover0  = 0;
    maxover1  = 0;
    maxover0p = 0; /* pretab */
    maxover1p = 0; /* pretab */
       
    maxsfb = gfc->sfb21_extra ? SBMAX_l : SBPSY_l;
    for ( sfb = 0; sfb < maxsfb; ++sfb ) {
        v0  = (vbrmax - vbrsf->l[sfb]) - 2*max_range_long[sfb];
        v1  = (vbrmax - vbrsf->l[sfb]) - 4*max_range_long[sfb];
        v0p = (vbrmax - vbrsf->l[sfb]) - 2*(max_rangep[sfb]+pretab[sfb]);
        v1p = (vbrmax - vbrsf->l[sfb]) - 4*(max_rangep[sfb]+pretab[sfb]);
        if (maxover0 < v0)
            maxover0 = v0;
        if (maxover1 < v1)
            maxover1 = v1;
        if (maxover0p < v0p)
            maxover0p = v0p;
        if (maxover1p < v1p)
            maxover1p = v1p;
    }

    mover = Min (maxover0, maxover0p);
    if ((gfc->noise_shaping == 2) && (gfc->presetTune.use && !(gfc->presetTune.athadjust_safe_noiseshaping || gfc->ATH->adjust < 1.0))) {
        /* allow scalefac_scale=1 */
        mover = Min (mover, maxover1);
        mover = Min (mover, maxover1p);
    }

    vbrmax    -= mover;
    maxover0  -= mover;
    maxover0p -= mover;
    maxover1  -= mover;
    maxover1p -= mover;

    if (maxover0 <= 0) {
        cod_info->scalefac_scale = 0;
        cod_info->preflag = 0;
        vbrmax -= maxover0;
    } else if (maxover0p <= 0) {
        cod_info->scalefac_scale = 0;
        cod_info->preflag = 1;
        vbrmax -= maxover0p;
    } else if (maxover1 == 0) {
        cod_info->scalefac_scale = 1;
        cod_info->preflag = 0;
    } else if (maxover1p == 0) {
        cod_info->scalefac_scale = 1;
        cod_info->preflag = 1;
    } else {
        assert(0); /* this should not happen */
    }

    /* sf =  (cod_info->global_gain-210.0) */
    cod_info->global_gain = vbrmax + 210;
    assert (cod_info->global_gain < 256);

    if (cod_info->global_gain < 0) {
        cod_info->global_gain = 0; 
    }
    else
    if (cod_info->global_gain > 255) 
        cod_info->global_gain = 255;
    
    for (sfb = 0; sfb < SBMAX_l; ++sfb)   
        vbrsf->l[sfb] -= vbrmax;

    if ( gfc->is_mpeg1 == 1 ) 
        maxover = compute_scalefacs_long (vbrsf->l, cod_info, cod_info->scalefac.l);
    else
        maxover = compute_scalefacs_long_lsf (vbrsf->l, cod_info, cod_info->scalefac.l);

    assert (maxover <= 0);
    
    *VBRmax = vbrmax;
}



/***********************************************************************
 *
 *  quantize xr34 based on scalefactors
 *
 *  calc_short_block_xr34      
 *  calc_long_block_xr34
 *
 *  Mark Taylor 2000-??-??
 *  Robert Hegemann 2000-10-20 made functions of them
 *
 ***********************************************************************/

static void
short_block_xr34 ( 
    const lame_internal_flags * gfc,
    const gr_info             * cod_info,
    const FLOAT8              * xr34_orig,
          FLOAT8              * xr34 )
{
    int    sfb, l, j, b;
    int    ifac, ifqstep, start, end;
    FLOAT8 fac;

    /* even though there is no scalefactor for sfb12
     * subblock gain affects upper frequencies too, that's why
     * we have to go up to SBMAX_s
     */
    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    for ( j = 0, sfb = 0; sfb < SBMAX_s; ++sfb ) {
        start = gfc->scalefac_band.s[ sfb ];
        end   = gfc->scalefac_band.s[ sfb+1 ];
        for (b = 0; b < 3; ++b) {
            ifac = 8*cod_info->subblock_gain[b]+ifqstep*cod_info->scalefac.s[sfb][b];
            
            if ( ifac == 0 ) {  /* just copy */
                l = (end-start+7) / 8;
                switch ((end-start) % 8) {
                    default:
                    case 0: do{ xr34[j] = xr34_orig[j]; ++j;
                    case 7:     xr34[j] = xr34_orig[j]; ++j;
                    case 6:     xr34[j] = xr34_orig[j]; ++j;
                    case 5:     xr34[j] = xr34_orig[j]; ++j;
                    case 4:     xr34[j] = xr34_orig[j]; ++j;
                    case 3:     xr34[j] = xr34_orig[j]; ++j;
                    case 2:     xr34[j] = xr34_orig[j]; ++j;
                    case 1:     xr34[j] = xr34_orig[j]; ++j; } while (--l);
                }
                continue;
            }
            if (ifac < Q_MAX-210)
                fac = IIPOW20_(ifac);    
            else
                fac = pow (2.0, 0.1875*ifac);
            
            /*
             *  loop unrolled into "Duff's Device".  Robert Hegemann
             */
            l = (end-start+7) / 8;
            switch ((end-start) % 8) {
                default:
                case 0: do{ xr34[j] = xr34_orig[j]*fac; ++j;
                case 7:     xr34[j] = xr34_orig[j]*fac; ++j;
                case 6:     xr34[j] = xr34_orig[j]*fac; ++j;
                case 5:     xr34[j] = xr34_orig[j]*fac; ++j;
                case 4:     xr34[j] = xr34_orig[j]*fac; ++j;
                case 3:     xr34[j] = xr34_orig[j]*fac; ++j;
                case 2:     xr34[j] = xr34_orig[j]*fac; ++j;
                case 1:     xr34[j] = xr34_orig[j]*fac; ++j; } while (--l);
            }
        }
    }
}



static void 
long_block_xr34 ( 
    const lame_internal_flags * gfc,
    const gr_info             * cod_info,
    const FLOAT8              * xr34_orig,
          FLOAT8              * xr34 )
{ 
    int    sfb, l, j;
    int    ifac, ifqstep, start, end;
    FLOAT8 fac;
        
    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    for ( sfb = 0; sfb < SBMAX_l; ++sfb ) {

        ifac = ifqstep*cod_info->scalefac.l[sfb];
        if (cod_info->preflag)
            ifac += ifqstep*pretab[sfb];

        start = gfc->scalefac_band.l[ sfb ];
        end   = gfc->scalefac_band.l[ sfb+1 ];
        
        if ( ifac == 0 ) {  /* just copy */
            j = start;
            l = (end-start+7) / 8;
            switch ((end-start) % 8) {
                default:
                case 0: do{ xr34[j] = xr34_orig[j]; ++j;
                case 7:     xr34[j] = xr34_orig[j]; ++j;
                case 6:     xr34[j] = xr34_orig[j]; ++j;
                case 5:     xr34[j] = xr34_orig[j]; ++j;
                case 4:     xr34[j] = xr34_orig[j]; ++j;
                case 3:     xr34[j] = xr34_orig[j]; ++j;
                case 2:     xr34[j] = xr34_orig[j]; ++j;
                case 1:     xr34[j] = xr34_orig[j]; ++j; } while (--l);
            }
            continue;
        }
        if (ifac < Q_MAX-210)
            fac = IIPOW20_(ifac);    
        else
            fac = pow (2.0, 0.1875*ifac);
        
        /*
         *  loop unrolled into "Duff's Device".  Robert Hegemann
         */
        j = start;
        l = (end-start+7) / 8;
        switch ((end-start) % 8) {
            default:
            case 0: do{ xr34[j] = xr34_orig[j]*fac; ++j;
            case 7:     xr34[j] = xr34_orig[j]*fac; ++j;
            case 6:     xr34[j] = xr34_orig[j]*fac; ++j;
            case 5:     xr34[j] = xr34_orig[j]*fac; ++j;
            case 4:     xr34[j] = xr34_orig[j]*fac; ++j;
            case 3:     xr34[j] = xr34_orig[j]*fac; ++j;
            case 2:     xr34[j] = xr34_orig[j]*fac; ++j;
            case 1:     xr34[j] = xr34_orig[j]*fac; ++j; } while (--l);
        }
    }
}







/************************************************************************
 *
 *  VBR_noise_shaping()
 *
 *  may result in a need of too many bits, then do it CBR like
 *
 *  Robert Hegemann 2000-10-25
 *
 ***********************************************************************/
 
int
VBR_noise_shaping (
    lame_internal_flags * gfc,
    FLOAT8            * xr34orig,
    int                 minbits,
    int                 maxbits,
    III_psy_xmin      * l3_xmin,
    int                 gr,
    int                 ch )
{
    III_scalefac_t vbrsf;
    III_scalefac_t vbrsf2;
    gr_info *cod_info;  
    FLOAT8 xr34[576];
    int shortblock, ret, bits, huffbits;
    int vbrmin, vbrmax, vbrmin2, vbrmax2;
    int best_huffman = gfc->use_best_huffman;
    int count=6;
    
    gfc->use_best_huffman = 0; /* we will do it later */
 
    cod_info   = &gfc->l3_side.tt[gr][ch];
    shortblock = (cod_info->block_type == SHORT_TYPE);
      
    if (shortblock) {
        short_block_sf (gfc, l3_xmin, xr34orig, cod_info->xr, &vbrsf2, &vbrmin2, &vbrmax2);
    } else {
        long_block_sf (gfc, l3_xmin, xr34orig, cod_info->xr, &vbrsf2, &vbrmin2, &vbrmax2);  
    } 
    vbrsf = vbrsf2;  
    vbrmin = vbrmin2;
    vbrmax = vbrmax2;

do {
--count;    

    if (shortblock) {
        short_block_scalefacs (gfc, cod_info, &vbrsf, &vbrmax);
        short_block_xr34      (gfc, cod_info, xr34orig, xr34);
    } else {
        long_block_scalefacs (gfc, cod_info, &vbrsf, &vbrmax);
        long_block_xr34      (gfc, cod_info, xr34orig, xr34);
    } 
    
    ret = VBR_quantize_granule (gfc, cod_info, xr34, gr, ch);
    
    if (vbrmin == vbrmax) break;
    else if (cod_info->part2_3_length < minbits) {
        int i;
        vbrmax = vbrmin2 + (vbrmax2-vbrmin2) * count/6;
        vbrmin = vbrmin2;
        if (shortblock) {
            for (i = 0; i < SBMAX_s; ++i) {
                //vbrsf.s[i][0] = vbrmin2 + (vbrsf2.s[i][0]-vbrmin2) * count/6;
                //vbrsf.s[i][1] = vbrmin2 + (vbrsf2.s[i][1]-vbrmin2) * count/6;
                //vbrsf.s[i][2] = vbrmin2 + (vbrsf2.s[i][2]-vbrmin2) * count/6;
                vbrsf.s[i][0] = Min(vbrsf2.s[i][0], vbrmax);
                vbrsf.s[i][1] = Min(vbrsf2.s[i][1], vbrmax);
                vbrsf.s[i][2] = Min(vbrsf2.s[i][2], vbrmax);
            }
        }
        else {
            for (i = 0; i < SBMAX_l; ++i) {
                //vbrsf.l[i] = vbrmin2 + (vbrsf2.l[i]-vbrmin2) * count/6;
                vbrsf.l[i] = Min(vbrsf2.l[i], vbrmax);
            }
        }
    }
    else if (cod_info->part2_3_length > maxbits) {
        int i;
        vbrmax = vbrmax2;
        vbrmin = vbrmax2 + (vbrmin2-vbrmax2) * count/6;
        if (shortblock) {
            for (i = 0; i < SBMAX_s; ++i) {
                //vbrsf.s[i][0] = vbrmax2 + (vbrsf2.s[i][0]-vbrmax2) * count/6;
                //vbrsf.s[i][1] = vbrmax2 + (vbrsf2.s[i][1]-vbrmax2) * count/6;
                //vbrsf.s[i][2] = vbrmax2 + (vbrsf2.s[i][2]-vbrmax2) * count/6;
                vbrsf.s[i][0] = Max(vbrsf2.s[i][0], vbrmin);
                vbrsf.s[i][1] = Max(vbrsf2.s[i][1], vbrmin);
                vbrsf.s[i][2] = Max(vbrsf2.s[i][2], vbrmin);
            }
        }
        else {
            for (i = 0; i < SBMAX_l; ++i) {
                //vbrsf.l[i] = vbrmax2 + (vbrsf2.l[i]-vbrmax2) * count/6;
                vbrsf.l[i] = Max(vbrsf2.l[i], vbrmin);
            }
        }
    }
    else break;
} while(1 && ret != -1);


    gfc->use_best_huffman = best_huffman;

    if (ret == -1) /* Houston, we have a problem */
        return -1;

    if (cod_info->part2_3_length < minbits) {
        huffbits = minbits - cod_info->part2_length;
        bits = bin_search_StepSize (gfc, cod_info, huffbits, 
                                    gfc->OldValue[ch], xr34, cod_info->l3_enc);
        gfc->OldValue[ch] = cod_info->global_gain;
    }
    if (cod_info->part2_3_length > maxbits) {
        huffbits = maxbits - cod_info->part2_length;
        if (huffbits < 0) huffbits = 0;
        bits = bin_search_StepSize (gfc, cod_info, huffbits, 
                                    gfc->OldValue[ch], xr34, cod_info->l3_enc);
        gfc->OldValue[ch] = cod_info->global_gain;
        if (bits >= LARGE_BITS) /* Houston, we have a problem */
            return -2;
        cod_info->part2_3_length += cod_info->part2_length;
    }

    if (cod_info->part2_length >= LARGE_BITS) /* Houston, we have a problem */
        return -2;
        
    assert (cod_info->global_gain < 256);
    
    return 0;
}
