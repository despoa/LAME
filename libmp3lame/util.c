/*
 *	lame utility library source file
 *
 *	Copyright (c) 1999 Albert L Faber
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

#include "util.h"
#include <ctype.h>
#include <assert.h>

#ifdef LAME_STD_PRINT
#include <stdarg.h>
#endif

/***********************************************************************
*
*  Global Function Definitions
*
***********************************************************************/
/*empty and close mallocs in gfc */
void freegfc(lame_internal_flags *gfc)   /* bit stream structure */
{
   if (gfc->bs.buf) free(gfc->bs.buf);
   free(gfc);
}



FLOAT8 ATHformula(FLOAT8 f)
{
  FLOAT8 ath;
  f  = Max(0.01, f);
  f  = Min(18.0,f);

  /* from Painter & Spanias, 1997 */
  /* minimum: (i=77) 3.3kHz = -5db */
  ath =    3.640 * pow(f,-0.8)
         - 6.500 * exp(-0.6*pow(f-3.3,2.0))
         + 0.001 * pow(f,4.0);
  return ath;
}


/* see for example "Zwicker: Psychoakustik, 1982; ISBN 3-540-11401-7 */
FLOAT8 freq2bark(FLOAT8 freq)
{
  /* input: freq in hz  output: barks */
    freq = freq * 0.001;
    return 13.0*atan(.76*freq) + 3.5*atan(freq*freq/(7.5*7.5));
}

/* see for example "Zwicker: Psychoakustik, 1982; ISBN 3-540-11401-7 */
FLOAT8 freq2cbw(FLOAT8 freq)
{
  /* input: freq in hz  output: critical band width */
    freq = freq * 0.001;
    return 25+75*pow(1+1.4*(freq*freq),0.69);
}






/***********************************************************************
 * compute bitsperframe and mean_bits for a layer III frame 
 **********************************************************************/
void getframebits(lame_global_flags *gfp,int *bitsPerFrame, int *mean_bits) {
  int whole_SpF;
  lame_internal_flags *gfc=gfp->internal_flags;

  int bit_rate;
  
  if (gfc->bitrate_index) 
    bit_rate = bitrate_table[gfp->version][gfc->bitrate_index];
  else
    bit_rate = gfp->brate;
  
  whole_SpF=((gfp->version+1)*72000*bit_rate) / gfp->out_samplerate;
  *bitsPerFrame = 8 * (whole_SpF + gfc->padding);
  *mean_bits = (*bitsPerFrame - 8*gfc->sideinfo_len) / gfc->mode_gr;

}




#define ABS(A) (((A)>0) ? (A) : -(A))

int FindNearestBitrate(
int bRate,        /* legal rates from 32 to 448 */
int version,      /* MPEG-1 or MPEG-2 LSF */
int samplerate)   /* convert bitrate in kbps to index */
{
  int     index = 0;
  int     bitrate = 10000;
  
  while( index<15)   {
    if( ABS( bitrate_table[version][index] - bRate) < ABS(bitrate-bRate) ) {
      bitrate = bitrate_table[version][index];
    }
    ++index;
  }
  return bitrate;
}


/* map samplerate to a valid samplerate
 *
 * Robert.Hegemann@gmx.de 2000-07-01
 */
int validSamplerate(int samplerate)
{
    if (samplerate<= 8000) return  8000;
    if (samplerate<=11025) return 11025;
    if (samplerate<=12000) return 12000;
    if (samplerate<=16000) return 16000;
    if (samplerate<=22050) return 22050;
    if (samplerate<=24000) return 24000;
    if (samplerate<=32000) return 32000;
    if (samplerate<=44100) return 44100;
    
    return 48000;
}

int BitrateIndex(
int bRate,        /* legal rates from 32 to 448 */
int version,      /* MPEG-1 or MPEG-2 LSF */
int samplerate)   /* convert bitrate in kbps to index */
{
int     index = 0;
int     found = 0;

    while(!found && index<15)   {
        if(bitrate_table[version][index] == bRate)
            found = 1;
        else
            ++index;
    }
    if(found)
        return(index);
    else {
        ERRORF("Bitrate %dkbs not legal for %iHz output sampling.\n",
                bRate, samplerate);
        return(-1);     /* Error! */
    }
}

int SmpFrqIndex(  /* convert samp frq in Hz to index */
int sRate,             /* legal rates 16000, 22050, 24000, 32000, 44100, 48000 */
int  *version)
{
	/* Assign default value */
	*version=0;

    if (sRate == 44100) {
        *version = 1; return(0);
    }
    else if (sRate == 48000) {
        *version = 1; return(1);
    }
    else if (sRate == 32000) {
        *version = 1; return(2);
    }
    else if (sRate == 24000) {
        *version = 0; return(1);
    }
    else if (sRate == 22050) {
        *version = 0; return(0);
    }
    else if (sRate == 16000) {
        *version = 0; return(2);
    }
    else if (sRate == 12000) {
        *version = 0; return(1);
    }
    else if (sRate == 11025) {
        *version = 0; return(0);
    }
    else if (sRate ==  8000) {
        *version = 0; return(2);
    }
    else {
        ERRORF("SmpFrqIndex: %ldHz is not a legal sample rate\n", sRate);
        return(-1);     /* Error! */
    }
}




/*****************************************************************************
*
*  End of bit_stream.c package
*
*****************************************************************************/

/* reorder the three short blocks By Takehiro TOMINAGA */
/*
  Within each scalefactor band, data is given for successive
  time windows, beginning with window 0 and ending with window 2.
  Within each window, the quantized values are then arranged in
  order of increasing frequency...
*/
void freorder(int scalefac_band[],FLOAT8 ix_orig[576]) {
  int i,sfb, window, j=0;
  FLOAT8 ix[576];
  for (sfb = 0; sfb < SBMAX_s; sfb++) {
    int start = scalefac_band[sfb];
    int end   = scalefac_band[sfb + 1];
    for (window = 0; window < 3; window++) {
      for (i = start; i < end; ++i) {
	ix[j++] = ix_orig[3*i+window];
      }
    }
  }
  memcpy(ix_orig,ix,576*sizeof(FLOAT8));
}










/* resampling via FIR filter, blackman window */
INLINE double blackman(int i,double offset,double fcn,int l)
{
  /* This algorithm from:
SIGNAL PROCESSING ALGORITHMS IN FORTRAN AND C
S.D. Stearns and R.A. David, Prentice-Hall, 1992
  */

  double bkwn;
  double wcn = (PI * fcn);
  double dly = l / 2.0;
  double x = i-offset;
  if (x<0) x=0;
  if (x>l) x=l;
  bkwn = 0.42 - 0.5 * cos((x * 2) * PI /l)
    + 0.08 * cos((x * 4) * PI /l);
  if (fabs(x-dly)<1e-9) return wcn/PI;
  else 
    return  (sin( (wcn *  ( x - dly))) / (PI * ( x - dly)) * bkwn );
}

/* gcd - greatest common divisor */
/* Joint work of Euclid and M. Hendry */
int gcd(int i, int j) {
  return j ? gcd(j, i % j) : i;
}


int fill_buffer_resample(lame_global_flags *gfp,sample_t *outbuf,int desired_len,
			 short int *inbuf,int len,int *num_used,int ch) {
  
  lame_internal_flags *gfc=gfp->internal_flags;
  FLOAT8 offset,xvalue;
  int i,j=0,k;
  int filter_l;
  FLOAT8 fcn,intratio;
  short int *inbuf_old;
  int bpc;   /* number of convolution functions to pre-compute */
  bpc = gfp->out_samplerate/gcd(gfp->out_samplerate,gfp->in_samplerate);
  if (bpc>BPC) bpc = BPC;

  intratio=( fabs(gfc->resample_ratio - floor(.5+gfc->resample_ratio)) < .0001 );
  fcn = .90/gfc->resample_ratio;
  if (fcn>.90) fcn=.90;
  filter_l=BLACKSIZE-6;  
  if (0==filter_l % 2 ) --filter_l;  /* must be odd */


  /* if resample_ratio = int, filter_l should be even */
  filter_l += intratio;
  assert(filter_l +5 < BLACKSIZE);
  
  if (!gfc->fill_buffer_resample_init) {
    gfc->fill_buffer_resample_init=1;
    gfc->itime[0]=0;
    gfc->itime[1]=0;
    memset((char *) gfc->inbuf_old, 0, sizeof(short int)*2*BLACKSIZE);
    /* precompute blackman filter coefficients */
    for (j= 0; j<= 2*bpc; ++j) {
      FLOAT8 sum=0;
      offset=(double)(j-bpc)/(double)(2*bpc);
      for (i=0; i<=filter_l; ++i) {
	sum+= gfc->blackfilt[j][i]=blackman(i,offset,fcn,filter_l);
      }
      gfc->blackfilt[j][i] /= sum;
    }

  }
  inbuf_old=gfc->inbuf_old[ch];

  /* time of j'th element in inbuf = itime + j/ifreq; */
  /* time of k'th element in outbuf   =  j/ofreq */
  for (k=0;k<desired_len;k++) {
    FLOAT8 time0;
    int joff;

    time0 = k*gfc->resample_ratio;       /* time of k'th output sample */
    j = floor( time0 -gfc->itime[ch]  );
    if ((j+filter_l/2) >= len) break;

    /* blackmon filter.  by default, window centered at j+.5(filter_l%2) */
    /* but we want a window centered at time0.   */
    offset = ( time0 -gfc->itime[ch] - (j + .5*(filter_l%2)));
    assert(fabs(offset)<=.500001);
    joff = floor((offset*2*bpc) + bpc +.5);

    xvalue=0;
    for (i=0 ; i<=filter_l ; ++i) {
      int j2 = i+j-filter_l/2;
      int y;
      y = (j2<0) ? inbuf_old[BLACKSIZE+j2] : inbuf[j2];
#define PRECOMPUTE
#ifdef PRECOMPUTE
      xvalue += y*gfc->blackfilt[joff][i];
#else
      xvalue += y*blackman(i,offset,fcn,filter_l);  /* very slow! */
#endif
    }
#if 1
    outbuf[k]=xvalue;
#else
    /* output buffer is of type FLOAT - this is no longer needed */
    {
    int value = floor(.5+xvalue);
    if (value > 32767) outbuf[k]=32767;
    else if (value < -32767) outbuf[k]=-32767;
    else outbuf[k]=value;
    }
#endif
  }

  
  /* k = number of samples added to outbuf */
  /* last k sample used data from j,j+1, or j+1 overflowed buffer */
  /* remove num_used samples from inbuf: */
  *num_used = Min(len,j+filter_l/2);
  gfc->itime[ch] += *num_used - k*gfc->resample_ratio;
  for (i=0;i<BLACKSIZE;i++)
    inbuf_old[i]=inbuf[*num_used + i -BLACKSIZE];
  return k;
}





/***********************************************************************
*
*  Message Output
*
***********************************************************************/

#ifdef LAME_STD_PRINT

void
lame_errorf(const char * s, ...)
{
  va_list args;
  va_start(args, s);
  vfprintf(stderr, s, args);
  va_end(args);
}

#endif



/***********************************************************************
 *
 *      routines to detect CPU specific features like 3DNow, MMX, SIMD
 *
 *  donated by Frank Klemm
 *  added Robert Hegemann 2000-10-10
 *
 ***********************************************************************/

int has_3DNow (void)
{
#if (defined(__GNUC__) && defined(__i386__)) 
    __asm__ (
        "pushal \n"
        "pushfl \n"                             
        "popl  %eax \n"
        "movl  %eax,%ecx \n"
        "xorl  $0x00200000,%eax \n"
        "pushl %eax \n"
        "popfl \n"
        "pushfl \n"
        "popl  %eax \n"
        "cmpl  %ecx,%eax \n"
        "jz    NO_CPUID1 \n"

        "movl  $0x80000000,%eax \n"
        "CPUID \n"
        "cmpl  $0x80000000,%eax \n"
        "jbe   NO_EXTENDED_MSR1 \n"

        "movl  $0x80000001,%eax \n"
        "CPUID \n"
        "testl $0x80000000,%edx \n"
        "jz    NO_3DNow1 \n"
        "popal \n"
        "movl  $1,%eax \n"
        "jmp   return1 \n"
    "NO_CPUID1: \n"
    "NO_EXTENDED_MSR1: \n"
    "NO_3DNow1: \n"
        "popal \n"
        "xorl  %eax,%eax \n"
    "return1: \n"    
    );
    return;
#else
    return 0;
#endif
}    

int has_MMX (void)
{
#if (defined(__GNUC__) && defined(__i386__)) 
    __asm__ (
        "pushal \n"
        "pushfl \n"                             
        "popl  %eax \n"
        "movl  %eax,%ecx \n"
        "xorl  $0x00200000,%eax \n"
        "pushl %eax \n"
        "popfl \n"
        "pushfl \n"
        "popl  %eax \n"
        "cmpl  %ecx,%eax \n"
        "jz    NO_CPUID2 \n"

        "movl  $1,%eax \n"
        "CPUID \n"
        "testl $0x800000,%edx \n"
        "jz    NO_MMX2 \n"
        "popal \n"
        "movl  $1,%eax \n"
        "jmp   return2 \n"
    "NO_CPUID2: \n"
    "NO_MMX2: \n"
        "popal \n"
        "xorl  %eax,%eax \n"
    "return2: \n"    
    );
    return;
#else
    return 0;
#endif
}    

int has_SIMD (void)
{
#if (defined(__GNUC__) && defined(__i386__)) 
    __asm__ (
        "pushal \n"
        "pushfl \n"                             
        "popl  %eax \n"
        "movl  %eax,%ecx \n"
        "xorl  $0x00200000,%eax \n"
        "pushl %eax \n"
        "popfl \n"
        "pushfl \n"
        "popl  %eax \n"
        "cmpl  %ecx,%eax \n"
        "jz    NO_CPUID3 \n"

        "movl  $1,%eax \n"
        "CPUID \n"
        "testl $0x2000000,%edx \n"
        "jz    NO_SIMD3 \n"
        "popal \n"
        "movl  $1,%eax \n"
        "jmp   return3 \n"
    "NO_CPUID3: \n"
    "NO_SIMD3: \n"
        "popal \n"
        "xorl  %eax,%eax \n"
    "return3: \n"    
    );
    return;
#else
    return 0;
#endif
}    
