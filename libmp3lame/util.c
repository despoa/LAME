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

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define PRECOMPUTE

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
   int i;
   for (i=0 ; i<2*BPC+1 ; ++i)
     if (gfc->blackfilt[i]) free(gfc->blackfilt[i]);

   if (gfc->bs.buf) free(gfc->bs.buf);
   if (gfc->inbuf_old[0]) free(gfc->inbuf_old[0]);
   if (gfc->inbuf_old[1]) free(gfc->inbuf_old[1]);
   free(gfc);
}

#ifdef KLEMM_01

/* 
 *  Klemm 1994 and 1997. Experimental data. Sorry, data looks a little bit
 *  dodderly. Data below 30 Hz is extrapolated from other material, above 18
 *  kHz the ATH is limited due to the original purpose (too much noise at
 *  ATH is not good even if it's theoretically inaudible).
 */

double  ATHformula ( double freq )
{
    /* short [MilliBel] is also sufficient */
    static float tab [] = {
        /*    10.0 */  96.69, 96.69, 96.26, 95.12,
        /*    12.6 */  93.53, 91.13, 88.82, 86.76,
        /*    15.8 */  84.69, 82.43, 79.97, 77.48,
        /*    20.0 */  74.92, 72.39, 70.00, 67.62,
        /*    25.1 */  65.29, 63.02, 60.84, 59.00,
        /*    31.6 */  57.17, 55.34, 53.51, 51.67,
        /*    39.8 */  50.04, 48.12, 46.38, 44.66,
        /*    50.1 */  43.10, 41.73, 40.50, 39.22,
        /*    63.1 */  37.23, 35.77, 34.51, 32.81,
        /*    79.4 */  31.32, 30.36, 29.02, 27.60,
        /*   100.0 */  26.58, 25.91, 24.41, 23.01,
        /*   125.9 */  22.12, 21.25, 20.18, 19.00,
        /*   158.5 */  17.70, 16.82, 15.94, 15.12,
        /*   199.5 */  14.30, 13.41, 12.60, 11.98,
        /*   251.2 */  11.36, 10.57,  9.98,  9.43,
        /*   316.2 */   8.87,  8.46,  7.44,  7.12,
        /*   398.1 */   6.93,  6.68,  6.37,  6.06,
        /*   501.2 */   5.80,  5.55,  5.29,  5.02,
        /*   631.0 */   4.75,  4.48,  4.22,  3.98,
        /*   794.3 */   3.75,  3.51,  3.27,  3.22,
        /*  1000.0 */   3.12,  3.01,  2.91,  2.68,
        /*  1258.9 */   2.46,  2.15,  1.82,  1.46,
        /*  1584.9 */   1.07,  0.61,  0.13, -0.35,
        /*  1995.3 */  -0.96, -1.56, -1.79, -2.35,
        /*  2511.9 */  -2.95, -3.50, -4.01, -4.21,
        /*  3162.3 */  -4.46, -4.99, -5.32, -5.35,
        /*  3981.1 */  -5.13, -4.76, -4.31, -3.13,
        /*  5011.9 */  -1.79,  0.08,  2.03,  4.03,
        /*  6309.6 */   5.80,  7.36,  8.81, 10.22,
        /*  7943.3 */  11.54, 12.51, 13.48, 14.21,
        /* 10000.0 */  14.79, 13.99, 12.85, 11.93,
        /* 12589.3 */  12.87, 15.19, 19.14, 23.69,
        /* 15848.9 */  33.52, 48.65, 59.42, 61.77,
        /* 19952.6 */  63.85, 66.04, 68.33, 70.09,
        /* 25118.9 */  70.66, 71.27, 71.91, 72.60,
    };
    double    freq_log;
    unsigned  index;
    
    freq *= 1000.;
    if ( freq <    10. ) freq =    10.;
    if ( freq > 25000. ) freq = 25000.;
    
    freq_log = 40. * log10 (0.1 * freq);   /* 4 steps per third, starting at 10 Hz */
    index    = (unsigned) freq_log;
    assert ( index < sizeof(tab)/sizeof(*tab) );
    return tab [index] * (1 + index - freq_log) + tab [index+1] * (freq_log - index);
}

#else

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

#endif

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
void getframebits(lame_internal_flags *const gfc, int *bitsPerFrame, int *mean_bits) 
{
  int  whole_SpF;  /* integral number of Slots per Frame without padding */
  int  bit_rate;
  
  /* get bitrate in kbps [?] */
  if (gfc->bitrate_index) 
    bit_rate = bitrate_table[gfc->gfp->version][gfc->bitrate_index];
  else
    bit_rate = gfc->gfp->brate;
  assert ( bit_rate <= 550 );
  
  // bytes_per_frame = bitrate * 1000 / ( gfp->out_samplerate / (gfp->version == 1  ?  1152  :  576 )) / 8;
  // bytes_per_frame = bitrate * 1000 / gfp->out_samplerate * (gfp->version == 1  ?  1152  :  576 ) / 8;
  // bytes_per_frame = bitrate * ( gfp->version == 1  ?  1152/8*1000  :  576/8*1000 ) / gfp->out_samplerate;
  
  whole_SpF = (gfc->gfp->version+1)*72000*bit_rate / gfc->gfp->out_samplerate;
  
  // There must be somewhere code toggling gfc->padding on and off
  
  /* main encoding routine toggles padding on and off */
  /* one Layer3 Slot consists of 8 bits */
  *bitsPerFrame = 8 * (whole_SpF + gfc->padding);
  
  // sideinfo_len
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


/* map frequency to a valid MP3 sample frequency
 *
 * Robert.Hegemann@gmx.de 2000-07-01
 */
int map2MP3Frequency(int freq)
{
    if (freq <=  8000) return  8000;
    if (freq <= 11025) return 11025;
    if (freq <= 12000) return 12000;
    if (freq <= 16000) return 16000;
    if (freq <= 22050) return 22050;
    if (freq <= 24000) return 24000;
    if (freq <= 32000) return 32000;
    if (freq <= 44100) return 44100;
    
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
        ERRORF("Bitrate %d kbps not legal for %i Hz output sampling frequency.\n",
                bRate, samplerate);
        return(-1);     /* Error! */
    }
}

/* convert samp freq in Hz to index */

int SmpFrqIndex ( int sample_freq, int* const version )
{
    switch ( sample_freq ) {
    case 44100: *version = 1; return  0;
    case 48000: *version = 1; return  1;
    case 32000: *version = 1; return  2;
    case 22050: *version = 0; return  0;
    case 24000: *version = 0; return  1;
    case 16000: *version = 0; return  2;
    case 11025: *version = 0; return  0;
    case 12000: *version = 0; return  1;
    case  8000: *version = 0; return  2;
    default:    ERRORF ( "SmpFrqIndex: %d Hz is not a legal sample frequency\n", sample_freq );
		*version = 0; return -1;
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

int gcd ( int i, int j )
{
//    assert ( i > 0  &&  j > 0 );
    return j ? gcd(j, i % j) : i;
}


int  fill_buffer_resample (
        lame_internal_flags * const  gfc,
        sample_t* const  outbuf,
        const int        desired_len,
        const sample_t*  inbuf,
        const int        len,           /* must be at least > sometwenty, otherwise nonsense will be produced */
        int* const       num_used,
        const int        channels )
{
    int        BLACKSIZE;
    FLOAT      offset;
    FLOAT      xvalue;
    int        i;
    int        j = 0;  /* why ?? */
    int        k;
    int        filter_l;
    FLOAT      fcn;
    FLOAT      intratio;
    sample_t*  inbuf_old;
    int        bpc;                    /* number of convolution functions to pre-compute */
    
    assert( len > 20 );
    
    bpc = gfc->gfp->out_samplerate 
        / gcd ( gfc->gfp->out_samplerate, gfc->gfp->in_samplerate );
    
    if ( bpc > BPC ) 
        bpc = BPC;

    intratio = fabs (gfc->resample_ratio - floor (gfc->resample_ratio + 0.5)) < 1.e-4;
    fcn = 0.90 / gfc->resample_ratio;
    if (fcn > 0.90) 
        fcn = 0.90;
        
    BLACKSIZE = 20;
    filter_l  = 19;
    if (0 == filter_l % 2 ) 
        filter_l -= 1;         /* must be odd */

    /* if resample_ratio = int, filter_l should be even */
    filter_l += intratio;
  
    if ( ! gfc->fill_buffer_resample_init ) {
        gfc->fill_buffer_resample_init = 1;
        
        gfc->inbuf_old [0] = calloc ( BLACKSIZE+1, sizeof (gfc->inbuf_old[0][0]) );
        gfc->inbuf_old [1] = calloc ( BLACKSIZE+1, sizeof (gfc->inbuf_old[0][0]) );
        
        if (gfc->inbuf_old[0] == NULL || gfc->inbuf_old[1] == NULL) {
            ERRORF ("PANIC: can't allocate inbuf_old buffer\n");
            exit (-1); /* PANIC */
        }
        for ( i = 0; i < 2*bpc+1; i++ ) {
            gfc->blackfilt[i] = calloc ( BLACKSIZE+2, sizeof (gfc->blackfilt[0][0]) );
            if (gfc->blackfilt[i] == NULL) {
                ERRORF ("PANIC: can't allocate blackfilt buffer\n");
                exit (-1); /* PANIC */
            }
        }
        for ( ; i < 2*BPC+1; i++ ) 
            gfc->blackfilt[i] = NULL;

        gfc->itime[0] = 0;
        gfc->itime[1] = 0;

        /* precompute blackman filter coefficients */
        for (j = 0; j <= 2*bpc; j++ ) {
            FLOAT  sum;
            sum    = 0;
            offset = (j-bpc) / (2.*bpc);
            for ( i = 0; i <= filter_l; i++ ) {
                sum += gfc->blackfilt [j][i] = blackman ( i, offset, fcn, filter_l );
            }
            gfc->blackfilt [j][i] /= sum;    /* Error ????????? */
        }
    }
    
    inbuf_old = gfc->inbuf_old [channels];

    /*
     * time of j'th element in inbuf  = itime + j/ifreq;
     * time of k'th element in outbuf = j/ofreq
     */
     
    for ( k = 0; k < desired_len; k++ ) {
        FLOAT  time0;
        int    joff;

        time0 = k * gfc->resample_ratio;       /* time of k'th output sample */
        j     = floor ( time0 - gfc->itime [channels] );

        /* check if we need more input data */
        if ( (j + filter_l/2) >= len ) 
            break;

        /* blackman filter.  by default, window centered at j+.5(filter_l%2) */
        /* but we want a window centered at time0.   */
        offset = ( time0 -gfc->itime[channels] - (j + .5*(filter_l%2)));
        assert (fabs(offset) <= 0.500001 );
        joff   = floor ( (offset*2*bpc) + bpc + 0.5 );

	if ( j >= filter_l/2 ) {
	    // Broken PRECOMPUTE
/* BUG fixed? problem: sometimes j-filter_l/2 == len
RH */
// was      xvalue = scalar20 ( inbuf + j-filter_l/2, gfc->blackfilt [joff] );
            xvalue = scalar20 ( inbuf + j-(filter_l+1)/2, gfc->blackfilt [joff] );
	} else {
	    xvalue = 0.;
            for ( i = 0 ; i <= filter_l; i++) {
/* BUG fixed? problem: sometimes j-filter_l/2 == len
RH */
// was          int       j2 = i + j-filter_l/2;   <- will result in j2==len!
                int       j2 = i + j-(filter_l+1)/2;
                sample_t  y;
            
                if (j2 < 0) {
                    assert (BLACKSIZE+j2 >= 0);
                    y = inbuf_old [BLACKSIZE+j2];
                } else {
                    assert (j2 < len);
                    y = inbuf [j2];
                }
#ifdef PRECOMPUTE
                xvalue += y*gfc->blackfilt [joff][i];
#else
                xvalue += y*blackman(i,offset,fcn,filter_l);  /* very slow! */
#endif
            }
	}
        outbuf [k] = xvalue;
    }

    /* k = number of samples added to outbuf
     * last k sample used data from j ... j+filter_l/2
     * remove num_used samples from inbuf:
     */
    
    *num_used = Min ( len, j + filter_l/2 );
    gfc->itime[channels] += *num_used - k*gfc->resample_ratio;
    for ( i = 0; i < BLACKSIZE; i++ )
        inbuf_old [i] = inbuf[ *num_used + i - BLACKSIZE ];
    if (len < BLACKSIZE)
        fprintf ( stderr, "Warning: len(%u) < BLACKSIZE(%u)\n", len, BLACKSIZE );
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
#ifdef HAVE_NASM 
    extern int has_3DNow_nasm(void);
    return has_3DNow_nasm();
#else
    return 0;   /* don't know, assume not */
#endif
}    

int has_MMX (void)
{
#ifdef HAVE_NASM 
    extern int has_MMX_nasm(void);
    return has_MMX_nasm();
#else
    return 0;   /* don't know, assume not */
#endif
}    

int has_SIMD (void)
{
#ifdef HAVE_NASM 
    extern int has_SIMD_nasm(void);
    return has_SIMD_nasm();
#else
    return 0;   /* don't know, assume not */
#endif
}    


/***********************************************************************
 *
 *  some simple statistics
 *
 *  bitrate index 0: free bitrate -> not allowed in VBR mode
 *  : bitrates, kbps depending on MPEG version
 *  bitrate index 15: forbidden
 *
 *  mode_ext:
 *  0:  LR
 *  1:  LR-i
 *  2:  MS
 *  3:  MS-i
 *
 ***********************************************************************/
 
void updateStats( lame_internal_flags * const gfc )
{
    assert ( gfc->bitrate_index < 16u );
    assert ( gfc->mode_ext      <  4u );
    
    /* count bitrate indices */
    gfc->bitrate_stereoMode_Hist [gfc->bitrate_index] [4] ++;
    
    /* count 'em for every mode extension in case of stereo encoding */
    if (gfc->stereo == 2)
        gfc->bitrate_stereoMode_Hist [gfc->bitrate_index] [gfc->mode_ext]++;
}


static FLOAT  std_scalar4  ( const sample_t* p, const sample_t* q )
{
    return  p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3];
}

static FLOAT  std_scalar8  ( const sample_t* p, const sample_t* q )
{
    return  p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3]
          + p[4]*q[4] + p[5]*q[5] + p[6]*q[6] + p[7]*q[7];
}

static FLOAT  std_scalar12 ( const sample_t* p, const sample_t* q )
{
    return  p[0]*q[0] + p[1]*q[1] + p[ 2]*q[ 2] + p[ 3]*q[ 3]
          + p[4]*q[4] + p[5]*q[5] + p[ 6]*q[ 6] + p[ 7]*q[ 7]
          + p[8]*q[8] + p[9]*q[9] + p[10]*q[10] + p[11]*q[11];
}

static FLOAT  std_scalar16 ( const sample_t* p, const sample_t* q )
{
    return  p[ 0]*q[ 0] + p[ 1]*q[ 1] + p[ 2]*q[ 2] + p[ 3]*q[ 3]
          + p[ 4]*q[ 4] + p[ 5]*q[ 5] + p[ 6]*q[ 6] + p[ 7]*q[ 7]
          + p[ 8]*q[ 8] + p[ 9]*q[ 9] + p[10]*q[10] + p[11]*q[11]
          + p[12]*q[12] + p[13]*q[13] + p[14]*q[14] + p[15]*q[15];
}

static FLOAT  std_scalar20 ( const sample_t* p, const sample_t* q )
{
    return  p[ 0]*q[ 0] + p[ 1]*q[ 1] + p[ 2]*q[ 2] + p[ 3]*q[ 3]
          + p[ 4]*q[ 4] + p[ 5]*q[ 5] + p[ 6]*q[ 6] + p[ 7]*q[ 7]
          + p[ 8]*q[ 8] + p[ 9]*q[ 9] + p[10]*q[10] + p[11]*q[11]
          + p[12]*q[12] + p[13]*q[13] + p[14]*q[14] + p[15]*q[15]
          + p[16]*q[16] + p[17]*q[17] + p[18]*q[18] + p[19]*q[19];
}


/* currently len must be a multiple of 4 and >= 8. This is necessary for SIMD1 */

static FLOAT  std_scalar ( const sample_t* p, const sample_t* q, size_t len )
{
    double sum = p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3];

    assert (len >= 8);
        
    do {
        p   += 4;
        q   += 4;
        len -= 4;
        sum += p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3];
    } while ( len > 4 );
    
    assert (len == 0);
    
    return sum;
}

static FLOAT  std_scalar64 ( const sample_t* p, const sample_t* q )
{
    return std_scalar ( p, q, 64 );
}

scalar_t   scalar4;
scalar_t   scalar8;
scalar_t   scalar12;
scalar_t   scalar16;
scalar_t   scalar20;
scalar_t   scalar64;
scalarn_t  scalar;

void init_scalar_functions ( lame_internal_flags *gfc )
{
    scalar4  = std_scalar4;
    scalar8  = std_scalar8;
    scalar12 = std_scalar12;
    scalar16 = std_scalar16;
    scalar20 = std_scalar20;
    scalar64 = std_scalar64;
    scalar   = std_scalar;
}

/* end of util.c */
