/*
 *	MP3 quantization
 *
 *	Copyright (c) 1999 Mark Taylor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <assert.h>
#include "util.h"
#include "l3side.h"
#include "quantize.h"
#include "reservoir.h"
#include "quantize-pvt.h"
#include "gtkanal.h"


#if (defined(__GNUC__) && defined(__i386__))
#define USE_GNUC_ASM
#endif
#ifdef _MSC_VER
#define USE_MSC_ASM
#endif



/*********************************************************************
 * XRPOW_FTOI is a macro to convert floats to ints.  
 * if XRPOW_FTOI(x) = nearest_int(x), then QUANTFAC(x)=adj43asm[x]
 *                                         ROUNDFAC= -0.0946
 *
 * if XRPOW_FTOI(x) = floor(x), then QUANTFAC(x)=asj43[x]   
 *                                   ROUNDFAC=0.4054
 *********************************************************************/
#ifdef USE_GNUC_ASM
#  define QUANTFAC(rx)  adj43asm[rx]
#  define ROUNDFAC -0.0946
#  define XRPOW_FTOI(src, dest) \
     asm ("fistpl %0 " : "=m"(dest) : "t"(src) : "st")
#elif defined (USE_MSC_ASM)
#  define QUANTFAC(rx)  adj43asm[rx]
#  define ROUNDFAC -0.0946
#  define XRPOW_FTOI(src, dest) do { \
     FLOAT8 src_ = (src); \
     int dest_; \
     { \
       __asm fld src_ \
       __asm fistp dest_ \
     } \
     (dest) = dest_; \
   } while (0)
#else
#  define QUANTFAC(rx)  adj43[rx]
#  define ROUNDFAC 0.4054
#  define XRPOW_FTOI(src,dest) ((dest) = (int)(src))
#endif






#define MAXQUANTERRORXXX


FLOAT8 calc_sfb_noise(FLOAT8 *xr, FLOAT8 *xr34, int stride, int bw, int sf)
{
  int j;
  FLOAT8 xfsf=0;
  FLOAT8 sfpow,sfpow34;

  sfpow = POW20(sf+210); /*pow(2.0,sf/4.0); */
  sfpow34  = IPOW20(sf+210); /*pow(sfpow,-3.0/4.0);*/

  for ( j=0; j < stride*bw ; j += stride) {
    int ix;
    FLOAT8 temp;

#if 0
    if (xr34[j]*sfpow34 > IXMAX_VAL) return -1;
    ix=floor( xr34[j]*sfpow34);
    temp = fabs(xr[j])- pow43[ix]*sfpow;
    temp *= temp;

    if (ix < IXMAX_VAL) {
      temp2 = fabs(xr[j])- pow43[ix+1]*sfpow;
      temp2 *=temp2;
      if (temp2<temp) {
	temp=temp2;
	++ix;
      }
    }
#else
    if (xr34[j]*sfpow34 > IXMAX_VAL) return -1;

    temp = xr34[j]*sfpow34;
    XRPOW_FTOI(temp, ix);
    XRPOW_FTOI(temp + QUANTFAC(ix), ix);
    temp = fabs(xr[j])- pow43[ix]*sfpow;
    temp *= temp;
    
#endif
    
#ifdef MAXQUANTERROR
    xfsf = Max(xfsf,temp);
#else
    xfsf += temp;
#endif
  }
#ifdef MAXQUANTERROR
  return xfsf;
#else
  return xfsf/bw;
#endif
}





FLOAT8 calc_sfb_noise_ave(FLOAT8 *xr, FLOAT8 *xr34, int stride, int bw,int sf)
{
  int j;
  FLOAT8 xfsf=0, xfsf_p1=0, xfsf_m1=0;
  FLOAT8 sfpow34,sfpow34_p1,sfpow34_m1;
  FLOAT8 sfpow,sfpow_p1,sfpow_m1;

  sfpow = POW20(sf+210); /*pow(2.0,sf/4.0); */
  sfpow34  = IPOW20(sf+210); /*pow(sfpow,-3.0/4.0);*/

  sfpow_m1 = sfpow*.8408964153;  /* pow(2,(sf-1)/4.0) */
  sfpow34_m1 = sfpow34*1.13878863476;       /* .84089 ^ -3/4 */

  sfpow_p1 = sfpow*1.189207115;  
  sfpow34_p1 = sfpow34*0.878126080187;

  for ( j=0; j < stride*bw ; j += stride) {
    int ix;
    FLOAT8 temp,temp_p1,temp_m1;

    if (xr34[j]*sfpow34_m1 > IXMAX_VAL) return -1;

    temp = xr34[j]*sfpow34;
    XRPOW_FTOI(temp, ix);
    XRPOW_FTOI(temp + QUANTFAC(ix), ix);
    temp = fabs(xr[j])- pow43[ix]*sfpow;
    temp *= temp;

    temp_p1 = xr34[j]*sfpow34_p1;
    XRPOW_FTOI(temp_p1, ix);
    XRPOW_FTOI(temp_p1 + QUANTFAC(ix), ix);
    temp_p1 = fabs(xr[j])- pow43[ix]*sfpow_p1;
    temp_p1 *= temp_p1;
    
    temp_m1 = xr34[j]*sfpow34_m1;
    XRPOW_FTOI(temp_m1, ix);
    XRPOW_FTOI(temp_m1 + QUANTFAC(ix), ix);
    temp_m1 = fabs(xr[j])- pow43[ix]*sfpow_m1;
    temp_m1 *= temp_m1;

#ifdef MAXQUANTERROR
    xfsf = Max(xfsf,temp);
    xfsf_p1 = Max(xfsf_p1,temp_p1);
    xfsf_m1 = Max(xfsf_m1,temp_m1);
#else
    xfsf += temp;
    xfsf_p1 += temp_p1;
    xfsf_m1 += temp_m1;
#endif
  }
  if (xfsf_p1>xfsf) xfsf = xfsf_p1;
  if (xfsf_m1>xfsf) xfsf = xfsf_m1;
#ifdef MAXQUANTERROR
  return xfsf;
#else
  return xfsf/bw;
#endif
}



int find_scalefac(FLOAT8 *xr,FLOAT8 *xr34,int stride,int sfb,
		     FLOAT8 l3_xmin,int bw)
{
  FLOAT8 xfsf;
  int i,sf,sf_ok,delsf;

  /* search will range from sf:  -209 -> 45  */
  sf = -82;
  delsf = 128;

  sf_ok=10000;
  for (i=0; i<7; i++) {
    delsf /= 2;
    if (delsf >= 16)
      xfsf = calc_sfb_noise(xr,xr34,stride,bw,sf);
    else
      xfsf = calc_sfb_noise_ave(xr,xr34,stride,bw,sf);

    if (xfsf < 0) {
      /* scalefactors too small */
      sf += delsf;
    }else{
      if (sf_ok==10000) sf_ok=sf;  
      if (xfsf > l3_xmin)  {
	/* distortion.  try a smaller scalefactor */
	sf -= delsf;
      }else{
	sf_ok = sf;
	sf += delsf;
      }
    }
  } 
  assert(sf_ok!=10000);
  assert(delsf==1);

  return sf;
}



/*
    sfb=0..5  scalefac < 16 
    sfb>5     scalefac < 8

    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    ol_sf =  (cod_info->global_gain-210.0);
    ol_sf -= 8*cod_info->subblock_gain[i];
    ol_sf -= ifqstep*scalefac[gr][ch].s[sfb][i];

*/
int compute_scalefacs_short(int sf[SBPSY_s][3],gr_info *cod_info,
int scalefac[SBPSY_s][3],int sbg[3])
{
  int maxrange,maxrange1,maxrange2,maxover;
  int sfb,i;
  int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;

  maxover=0;
  maxrange1 = 15;
  maxrange2 = 7;


  for (i=0; i<3; ++i) {
    int maxsf1=0,maxsf2=0,minsf=1000;
    /* see if we should use subblock gain */
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      if (sfb < 6) {
	if (-sf[sfb][i]>maxsf1) maxsf1 = -sf[sfb][i];
      } else {
	if (-sf[sfb][i]>maxsf2) maxsf2 = -sf[sfb][i];
      }
      if (-sf[sfb][i]<minsf) minsf = -sf[sfb][i];
    }

    /* boost subblock gain as little as possible so we can
     * reach maxsf1 with scalefactors 
     * 8*sbg >= maxsf1   
     */
    maxsf1 = Max(maxsf1-maxrange1*ifqstep,maxsf2-maxrange2*ifqstep);
    sbg[i]=0;
    if (minsf >0 ) sbg[i] = floor(.125*minsf + .001);
    if (maxsf1 > 0)  sbg[i]  = Max(sbg[i],maxsf1/8 + (maxsf1 % 8 != 0));
    if (sbg[i] > 7) sbg[i]=7;


    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      sf[sfb][i] += 8*sbg[i];

      if (sf[sfb][i] < 0) {
	maxrange = sfb < 6 ? maxrange1 : maxrange2;
	scalefac[sfb][i]=-sf[sfb][i]/ifqstep + (-sf[sfb][i]%ifqstep != 0);
	if (scalefac[sfb][i]>maxrange) scalefac[sfb][i]=maxrange;
	
	if (-(sf[sfb][i] + scalefac[sfb][i]*ifqstep) >maxover)  {
	  maxover=-(sf[sfb][i] + scalefac[sfb][i]*ifqstep);
	}
      }
    }
  }

  return maxover;
}





int max_range_short[SBPSY_s]=
{15, 15, 15, 15, 15, 15 ,  7,    7,    7,    7,   7,     7 };
int max_range_long[SBPSY_l]=
{15,   15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   7,   7,   7,   7,   7,   7,   7,    7,    7,    7};


/*
	  ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
	  ol_sf =  (cod_info->global_gain-210.0);
	  ol_sf -= ifqstep*scalefac[gr][ch].l[sfb];
	  if (cod_info->preflag && sfb>=11) 
	  ol_sf -= ifqstep*pretab[sfb];
*/
int compute_scalefacs_long(int sf[SBPSY_l],gr_info *cod_info,int scalefac[SBPSY_l])
{
  int sfb;
  int maxover;
  int ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
  

  if (cod_info->preflag)
    for ( sfb = 11; sfb < SBPSY_l; sfb++ ) 
      sf[sfb] += pretab[sfb]*ifqstep;


  maxover=0;
  for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {

    if (sf[sfb]<0) {
      /* ifqstep*scalefac >= -sf[sfb], so round UP */
      scalefac[sfb]=-sf[sfb]/ifqstep  + (-sf[sfb] % ifqstep != 0);
      if (scalefac[sfb] > max_range_long[sfb]) scalefac[sfb]=max_range_long[sfb];
      
      /* sf[sfb] should now be positive: */
      if (  -(sf[sfb] + scalefac[sfb]*ifqstep)  > maxover) {
	maxover = -(sf[sfb] + scalefac[sfb]*ifqstep);
      }
    }
  }

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
void
VBR_quantize_granule(lame_global_flags *gfp,
		     FLOAT8 xr[576],
                FLOAT8 xr34[576], int l3_enc[2][2][576],
		     III_psy_ratio *ratio,      III_psy_xmin l3_xmin,
                III_scalefac_t scalefac[2][2],int gr, int ch)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  gr_info *cod_info;  
  III_side_info_t * l3_side;
  l3_side = &gfc->l3_side;
  cod_info = &l3_side->gr[gr].ch[ch].tt;


  /* encode scalefacs */
  if ( gfp->version == 1 ) 
    scale_bitcount(&scalefac[gr][ch], cod_info);
  else
    scale_bitcount_lsf(&scalefac[gr][ch], cod_info);
  
  /* quantize xr34 */
  cod_info->part2_3_length = count_bits(gfp,l3_enc[gr][ch],xr34,cod_info);
  cod_info->part2_3_length += cod_info->part2_length;
  assert((int)count_bits != LARGE_BITS);
  
  /* do this before calling best_scalefac_store! */
  if (gfp->gtkflag) {
    FLOAT8 noise[4];
    FLOAT8 xfsf[4][SBMAX_l];
    FLOAT8 distort[4][SBMAX_l];
    FLOAT8 save;
    
    /* recompute allowed noise with no 'masking_lower' for
     * frame analyzer */
    save = gfc->masking_lower;
    gfc->masking_lower=1.0;
    calc_xmin( gfp,xr, ratio, cod_info, &l3_xmin);
    gfc->masking_lower=save;

    noise[0]=calc_noise1( gfp, xr, l3_enc[gr][ch], cod_info, 
			  xfsf,distort, &l3_xmin, &scalefac[gr][ch], 
			  &noise[2],&noise[3],&noise[1]);
    
    set_pinfo (gfp, cod_info, ratio, &scalefac[gr][ch], xr, xfsf, noise, gr, ch);
  }

  
  cod_info = &l3_side->gr[gr].ch[ch].tt;
  best_scalefac_store(gfp,gr, ch, l3_enc, l3_side, scalefac);
  if (gfc->use_best_huffman==1 && cod_info->block_type != SHORT_TYPE) {
    best_huffman_divide(gfc, gr, ch, cod_info, l3_enc[gr][ch]);
  }
  if (cod_info->part2_3_length>4095) 
    cod_info->part2_3_length=LARGE_BITS;
  

  if (gfc->pinfo != NULL) {
    plotting_data *pinfo=gfc->pinfo;
    pinfo->LAMEmainbits[gr][ch]=cod_info->part2_3_length;
  }

  return;
}
  

















/************************************************************************
 *
 * VBR_noise_shaping()
 *
 * compute scalefactors, l3_enc, and return number of bits needed to encode
 *
 * return code:    0   scalefactors were found with all noise < masking
 *
 *               n>0   scalefactors required too many bits.  global gain
 *                     was decreased by n
 *                     If n is large, we should probably recompute scalefacs
 *                     with a lower quality.
 *
 *               n<0   scalefactors used less than minbits.
 *                     global gain was increased by n.  
 *                     If n is large, might want to recompute scalefacs
 *                     with a higher quality setting?
 *
 ************************************************************************/
int
VBR_noise_shaping
(
 lame_global_flags *gfp,
 FLOAT8 xr[576], III_psy_ratio *ratio,
 int l3_enc[2][2][576], int *ath_over, int minbits,
 III_scalefac_t scalefac[2][2],
 int gr,int ch)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  int       start,end,bw,sfb,l, i, vbrmax;
  III_scalefac_t vbrsf;
  III_scalefac_t save_sf;
  int maxover0,maxover1,maxover0p,maxover1p,maxover;
  int ifqstep;
  III_psy_xmin l3_xmin;
  III_side_info_t * l3_side;
  gr_info *cod_info;  
  FLOAT8 xr34[576];
  int shortblock;
  int global_gain_adjust=0;

  l3_side = &gfc->l3_side;
  cod_info = &l3_side->gr[gr].ch[ch].tt;
  shortblock = (cod_info->block_type == SHORT_TYPE);
  *ath_over = calc_xmin( gfp,xr, ratio, cod_info, &l3_xmin);

  
  for(i=0;i<576;i++) {
    FLOAT8 temp=fabs(xr[i]);
    xr34[i]=sqrt(sqrt(temp)*temp);
  }
  
  
  vbrmax=-10000;
  if (shortblock) {
    for ( sfb = 0; sfb < SBPSY_s; sfb++ )  {
      for ( i = 0; i < 3; i++ ) {
	start = gfc->scalefac_band.s[ sfb ];
	end   = gfc->scalefac_band.s[ sfb+1 ];
	bw = end - start;
	vbrsf.s[sfb][i] = find_scalefac(&xr[3*start+i],&xr34[3*start+i],3,sfb,
					l3_xmin.s[sfb][i],bw);
	if (vbrsf.s[sfb][i]>vbrmax) vbrmax=vbrsf.s[sfb][i];
      }
    }
  }else{
    for ( sfb = 0; sfb < SBPSY_l; sfb++ )   {
      start = gfc->scalefac_band.l[ sfb ];
      end   = gfc->scalefac_band.l[ sfb+1 ];
      bw = end - start;
      vbrsf.l[sfb] = find_scalefac(&xr[start],&xr34[start],1,sfb,
				   l3_xmin.l[sfb],bw);
      if (vbrsf.l[sfb]>vbrmax) vbrmax = vbrsf.l[sfb];
    }
    
  } /* compute needed scalefactors */
  memcpy(&save_sf,&vbrsf,sizeof(III_scalefac_t));



  do { 

  memset(&scalefac[gr][ch],0,sizeof(III_scalefac_t));
    
  if (shortblock) {
    /******************************************************************
     *
     *  short block scalefacs
     *
     ******************************************************************/
    maxover0=0;
    maxover1=0;
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      for ( i = 0; i < 3; i++ ) {
	maxover0 = Max(maxover0,(vbrmax - vbrsf.s[sfb][i]) - (4*14 + 2*max_range_short[sfb]) );
	maxover1 = Max(maxover1,(vbrmax - vbrsf.s[sfb][i]) - (4*14 + 4*max_range_short[sfb]) );
      }
    }
    if (maxover0==0) 
      cod_info->scalefac_scale = 0;
    else if (maxover1==0)
      cod_info->scalefac_scale = 1;
    else {
      cod_info->scalefac_scale = 1;
      vbrmax -= maxover1;
    }

    /* sf =  (cod_info->global_gain-210.0) */
    cod_info->global_gain = vbrmax +210;
    if (cod_info->global_gain>255) cod_info->global_gain=255;
    
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      for ( i = 0; i < 3; i++ ) {
	vbrsf.s[sfb][i]-=vbrmax;
      }
    }
    maxover=compute_scalefacs_short(vbrsf.s,cod_info,scalefac[gr][ch].s,cod_info->subblock_gain);
    assert(maxover <=0);
    {
      /* adjust global_gain so at least 1 subblock gain = 0 */
      int minsfb=999;
      for (i=0; i<3; i++) minsfb = Min(minsfb,cod_info->subblock_gain[i]);
      minsfb = Min(cod_info->global_gain/8,minsfb);
      vbrmax -= 8*minsfb; 
      cod_info->global_gain -= 8*minsfb;
      for (i=0; i<3; i++) cod_info->subblock_gain[i] -= minsfb;
    }
    
    
    
    /* quantize xr34[] based on computed scalefactors */
    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    for ( sfb = 0; sfb < SBPSY_s; sfb++ ) {
      start = gfc->scalefac_band.s[ sfb ];
      end   = gfc->scalefac_band.s[ sfb+1 ];
      for (i=0; i<3; i++) {
	int ifac;
	FLOAT8 fac;
	ifac = (8*cod_info->subblock_gain[i]+ifqstep*scalefac[gr][ch].s[sfb][i]);
	if (ifac+210<256) 
	  fac = 1/IPOW20(ifac+210);
	else
	  fac = pow(2.0,.75*ifac/4.0);
	for ( l = start; l < end; l++ ) 
	  xr34[3*l +i]*=fac;
      }
    }
    
    
    
  }else{
    /******************************************************************
     *
     *  long block scalefacs
     *
     ******************************************************************/
    maxover0=0;
    maxover1=0;
    maxover0p=0;
    maxover1p=0;
    
    
    for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {
      maxover0 = Max(maxover0,(vbrmax - vbrsf.l[sfb]) - 2*max_range_long[sfb] );
      maxover0p = Max(maxover0,(vbrmax - vbrsf.l[sfb]) - 2*(max_range_long[sfb]+pretab[sfb]) );
      maxover1 = Max(maxover1,(vbrmax - vbrsf.l[sfb]) - 4*max_range_long[sfb] );
      maxover1p = Max(maxover1,(vbrmax - vbrsf.l[sfb]) - 4*(max_range_long[sfb]+pretab[sfb]));
    }
    
    
    if (maxover0==0) {
      cod_info->scalefac_scale = 0;
      cod_info->preflag=0;
    } else if (maxover0p==0) {
      cod_info->scalefac_scale = 0;
      cod_info->preflag=1;
    } else if (maxover1==0) {
      cod_info->preflag=0;
      cod_info->scalefac_scale = 1;
    } else if (maxover1p==0) {
      cod_info->scalefac_scale = 1;
      cod_info->preflag=1;
    } else {
      /* none were sufficient.  we need to boost vbrmax */
      if (maxover1 < maxover1p) {
	cod_info->scalefac_scale = 1;
	cod_info->preflag=0;
	vbrmax -= maxover1;
      }else{
	cod_info->scalefac_scale = 1;
	cod_info->preflag=1;
	vbrmax -= maxover1p;
      }
    }
    
    
    /* sf =  (cod_info->global_gain-210.0) */
    cod_info->global_gain = vbrmax +210;
    if (cod_info->global_gain>255) cod_info->global_gain=255;
    
    for ( sfb = 0; sfb < SBPSY_l; sfb++ )   
      vbrsf.l[sfb] -= vbrmax;
    
    
    maxover=compute_scalefacs_long(vbrsf.l,cod_info,scalefac[gr][ch].l);
    assert(maxover <=0);
    
    
    /* quantize xr34[] based on computed scalefactors */
    ifqstep = ( cod_info->scalefac_scale == 0 ) ? 2 : 4;
    for ( sfb = 0; sfb < SBPSY_l; sfb++ ) {
      int ifac;
      FLOAT8 fac;
      ifac = ifqstep*scalefac[gr][ch].l[sfb];
      if (cod_info->preflag)
	ifac += ifqstep*pretab[sfb];

      assert(ifac >= -vbrsf.l[sfb]);
      if (ifac+210<256) 
	fac = 1/IPOW20(ifac+210);
      else
	fac = pow(2.0,.75*ifac/4.0);

      start = gfc->scalefac_band.l[ sfb ];
      end   = gfc->scalefac_band.l[ sfb+1 ];
      for ( l = start; l < end; l++ ) 
	xr34[l]*=fac;
    }
  } 
  
  VBR_quantize_granule(gfp,xr,xr34,l3_enc,ratio,l3_xmin,scalefac,gr,ch);

  if (cod_info->part2_3_length < minbits) {
    /* decrease global gain, recompute scale factors */
    if (*ath_over==0) break;  
    if (cod_info->part2_3_length-cod_info->part2_length== 0) break;
    if (vbrmax+210 ==0 ) break;
    
    //    printf("not enough bits, decreasing vbrmax. g_gainv=%i\n",cod_info->global_gain);
    //    printf("%i minbits=%i   part2_3_length=%i  part2=%i\n",
    //	   gfp->frameNum,minbits,cod_info->part2_3_length,cod_info->part2_length);
    --vbrmax;
    --global_gain_adjust;
    memcpy(&vbrsf,&save_sf,sizeof(III_scalefac_t));
    for(i=0;i<576;i++) {
      FLOAT8 temp=fabs(xr[i]);
      xr34[i]=sqrt(sqrt(temp)*temp);
    }

  }

  } while ((cod_info->part2_3_length < minbits));

  while (cod_info->part2_3_length > 4095) {
    /* increase global gain, keep exisiting scale factors */
    ++cod_info->global_gain;
    if (cod_info->global_gain > 255) 
      fprintf(stderr,"%i impossible to encode this frame! bits=%i\n",
gfp->frameNum,cod_info->part2_3_length);
    VBR_quantize_granule(gfp,xr,xr34,l3_enc,ratio,l3_xmin,scalefac,gr,ch);
    ++global_gain_adjust;
  }

  return global_gain_adjust;
}




void
VBR_quantize(lame_global_flags *gfp,
                FLOAT8 pe[2][2], FLOAT8 ms_ener_ratio[2],
                FLOAT8 xr[2][2][576], III_psy_ratio ratio[2][2],
                int l3_enc[2][2][576],
                III_scalefac_t scalefac[2][2])
{
  lame_internal_flags *gfc=gfp->internal_flags;
  int minbits,maxbits,max_frame_bits,totbits,bits,gr,ch,i,bits_ok;
  int bitsPerFrame,mean_bits;
  FLOAT8 qadjust;
  III_side_info_t * l3_side;
  gr_info *cod_info;  
  int ath_over[2][2];
  FLOAT8 masking_lower_db;
  //  static const FLOAT8 dbQ[10]={-6.0,-4.5,-3.0,-1.5,0,0.3,0.6,1.0,1.5,2.0};
  static const FLOAT8 dbQ[10]={-9.0,-7.5,-6.0,-4.5,-3.0,-1.5,-.5,0.3,0.6,1.0};

  l3_side = &gfc->l3_side;
  gfc->ATH_lower = (4-gfp->VBR_q)*4.0; 
  if (gfc->ATH_lower < 0) gfc->ATH_lower=0;
  iteration_init(gfp,l3_side,l3_enc);

  gfc->bitrate_index=gfc->VBR_min_bitrate;
  getframebits(gfp,&bitsPerFrame, &mean_bits);
  minbits = .4*(mean_bits/gfc->stereo);

  gfc->bitrate_index=gfc->VBR_max_bitrate;
  getframebits(gfp,&bitsPerFrame, &mean_bits);
  max_frame_bits = ResvFrameBegin(gfp,l3_side, mean_bits, bitsPerFrame);
  maxbits=2.5*(mean_bits/gfc->stereo);

  {
  /* compute a target  mean_bits based on compression ratio 
   * which was set based on VBR_q  
   */
  int bit_rate = gfp->out_samplerate*16*gfc->stereo/(1000.0*gfp->compression_ratio);
  bitsPerFrame = (bit_rate*gfp->framesize*1000)/gfp->out_samplerate;
  mean_bits = (bitsPerFrame - 8*gfc->sideinfo_len) / gfc->mode_gr;
  }

  minbits=Max(minbits,.4*(mean_bits/gfc->stereo));
  maxbits=Min(maxbits,2.5*(mean_bits/gfc->stereo));

  if (gfc->mode_ext==MPG_MD_MS_LR) 
    for (gr = 0; gr < gfc->mode_gr; gr++) 
      ms_convert(xr[gr],xr[gr]);


  qadjust=0;
  do {
  
  totbits=0;
  for (gr = 0; gr < gfc->mode_gr; gr++) {
    for (ch = 0; ch < gfc->stereo; ch++) { 
      int shortblock;
      cod_info = &l3_side->gr[gr].ch[ch].tt;
      shortblock = (cod_info->block_type == SHORT_TYPE);


      /* Adjust allowed masking based on quality setting */
      assert( gfp->VBR_q <= 9 );
      assert( gfp->VBR_q >= 0 );
      masking_lower_db = dbQ[gfp->VBR_q] + qadjust;
      
      if (pe[gr][ch]>750)
      	masking_lower_db -= 4*(pe[gr][ch]-750.)/750.;
      //      if (shortblock) masking_lower_db -= 1;


      do {
	gfc->masking_lower = pow(10.0,masking_lower_db/10);
	VBR_noise_shaping (gfp,xr[gr][ch],&ratio[gr][ch],l3_enc,&ath_over[gr][ch],minbits,scalefac,gr,ch);
	bits = cod_info->part2_3_length;
	if (bits>maxbits) {
	  printf("%i masking_lower=%f  bits>maxbits. bits:  %i  %i  %i  \n",gfp->frameNum,masking_lower_db,minbits,bits,maxbits);
	  masking_lower_db  += Max(.10,(bits-maxbits)/200.0);
	}

      }	while (bits > maxbits);

      totbits += bits;
    }
  }
  bits_ok=1;
  if (totbits>max_frame_bits) {
    printf("%i totbits>max_frame_bits   totbits=%i  maxbits=%i \n",gfp->frameNum,totbits,max_frame_bits);
    qadjust += Max(.10,(totbits-max_frame_bits)/200.0);
    printf("next masking_lower_db = %f \n",masking_lower_db + qadjust);
    bits_ok=0;
  }

  } while (!bits_ok);




  

  for( gfc->bitrate_index = (gfp->VBR_hard_min ? gfc->VBR_min_bitrate : 1);
       gfc->bitrate_index < gfc->VBR_max_bitrate;
       gfc->bitrate_index++    ) {

    getframebits (gfp,&bitsPerFrame, &mean_bits);
    maxbits = ResvFrameBegin(gfp,l3_side, mean_bits, bitsPerFrame);
    if (totbits <= maxbits) break;
  }
  if (gfc->bitrate_index == gfc->VBR_max_bitrate) {
    getframebits (gfp,&bitsPerFrame, &mean_bits);
    maxbits = ResvFrameBegin(gfp,l3_side, mean_bits, bitsPerFrame);
  }

  //  printf("%i total_bits=%i max_frame_bits=%i index=%i  \n",gfp->frameNum,totbits,max_frame_bits,gfc->bitrate_index);

  for (gr = 0; gr < gfc->mode_gr; gr++) {
    for (ch = 0; ch < gfc->stereo; ch++) {
      cod_info = &l3_side->gr[gr].ch[ch].tt;
      ResvAdjust (gfp,cod_info, l3_side, mean_bits);
      
      /*******************************************************************
       * set the sign of l3_enc from the sign of xr
       *******************************************************************/
      for ( i = 0; i < 576; i++) {
        if (xr[gr][ch][i] < 0) l3_enc[gr][ch][i] *= -1;
      }
    }
  }
  ResvFrameEnd (gfp,l3_side, mean_bits);
}



