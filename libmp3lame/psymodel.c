/*
 *	psymodel.c
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


/*
PSYCHO ACOUSTICS


This routine computes the psycho acoustics, delayed by one granule.  

Input: buffer of PCM data (1024 samples).  

This window should be centered over the 576 sample granule window.
The routine will compute the psycho acoustics for
this granule, but return the psycho acoustics computed
for the *previous* granule.  This is because the block
type of the previous granule can only be determined
after we have computed the psycho acoustics for the following
granule.  

Output:  maskings and energies for each scalefactor band.
block type, PE, and some correlation measures.  
The PE is used by CBR modes to determine if extra bits
from the bit reservoir should be used.  The correlation
measures are used to determine mid/side or regular stereo.
*/
/*
Notation:

barks:  a non-linear frequency scale.  Mapping from frequency to
        barks is given by freq2bark()

scalefactor bands: The spectrum (frequencies) are broken into 
                   SBMAX "scalefactor bands".  Thes bands
                   are determined by the MPEG ISO spec.  In
                   the noise shaping/quantization code, we allocate
                   bits among the partition bands to achieve the
                   best possible quality

partition bands:   The spectrum is also broken into about
                   64 "partition bands".  Each partition 
                   band is about .34 barks wide.  There are about 2-5
                   partition bands for each scalefactor band.

LAME computes all psycho acoustic information for each partition
band.  Then at the end of the computations, this information
is mapped to scalefactor bands.  The energy in each scalefactor
band is taken as the sum of the energy in all partition bands
which overlap the scalefactor band.  The maskings can be computed
in the same way (and thus represent the average masking in that band)
or by taking the minmum value multiplied by the number of
partition bands used (which represents a minimum masking in that band).
*/
/*
The general outline is as follows:

1. compute the energy in each partition band
2. compute the tonality in each partition band
3. compute the strength of each partion band "masker"
4. compute the masking (via the spreading function applied to each masker)
5. Modifications for mid/side masking.  

Each partition band is considiered a "masker".  The strength
of the i'th masker in band j is given by:

    s3(bark(i)-bark(j))*strength(i)

The strength of the masker is a function of the energy and tonality.
The more tonal, the less masking.  LAME uses a simple linear formula
(controlled by NMT and TMN) which says the strength is given by the
energy divided by a linear function of the tonality.
*/
/*
s3() is the "spreading function".  It is given by a formula
determined via listening tests.  

The total masking in the j'th partition band is the sum over
all maskings i.  It is thus given by the convolution of
the strength with s3(), the "spreading function."

masking(j) = sum_over_i  s3(i-j)*strength(i)  = s3 o strength

where "o" = convolution operator.  s3 is given by a formula determined
via listening tests.  It is normalized so that s3 o 1 = 1.

Note: instead of a simple convolution, LAME also has the
option of using "additive masking"

The most critical part is step 2, computing the tonality of each
partition band.  LAME has two tonality estimators.  The first
is based on the ISO spec, and measures how predictiable the
signal is over time.  The more predictable, the more tonal.
The second measure is based on looking at the spectrum of
a single granule.  The more peaky the spectrum, the more
tonal.  By most indications, the latter approach is better.

Finally, in step 5, the maskings for the mid and side
channel are possibly increased.  Under certain circumstances,
noise in the mid & side channels is assumed to also
be masked by strong maskers in the L or R channels.


Other data computed by the psy-model:

ms_ratio        side-channel / mid-channel masking ratio (for previous granule)
ms_ratio_next   side-channel / mid-channel masking ratio for this granule

percep_entropy[2]     L and R values (prev granule) of PE - A measure of how 
                      much pre-echo is in the previous granule
percep_entropy_MS[2]  mid and side channel values (prev granule) of percep_entropy
energy[4]             L,R,M,S energy in each channel, prev granule
blocktype_d[2]        block type to use for previous granule
*/




#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "util.h"
#include "encoder.h"
#include "psymodel.h"
#include "l3side.h"
#include <assert.h>
#include "tables.h"
#include "fft.h"
#include "machine.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#define NSFIRLEN 21
#define rpelev 2
#define rpelev2 16
#define rpelev_s 2
#define rpelev2_s 16

/* size of each partition band, in barks: */
#define DELBARK .34
#define CW_LOWER_INDEX 6


#if 1
    /* AAC values, results in more masking over MP3 values */
# define TMN 18
# define NMT 6
#else
    /* MP3 values */
# define TMN 29
# define NMT 6
#endif

#ifdef M_LN10
#define		LN_TO_LOG10		(M_LN10/10)
#else
#define         LN_TO_LOG10             0.2302585093
#endif

#ifdef NON_LINEAR_PSY

static const float non_linear_psy_constant = .3;

#define NON_LINEAR_SCALE_ITEM(x)   pow((x), non_linear_psy_constant)
#define NON_LINEAR_SCALE_SUM(x)    pow((x), 1/non_linear_psy_constant)

#if 0
#define NON_LINEAR_SCALE_ENERGY(x) pow(10, (x)/10)
#else
#define NON_LINEAR_SCALE_ENERGY(x) (x)
#endif

#else

#define NON_LINEAR_SCALE_ITEM(x)   (x)
#define NON_LINEAR_SCALE_SUM(x)    (x)
#define NON_LINEAR_SCALE_ENERGY(x) (x)

#endif


/*
   L3psycho_anal.  Compute psycho acoustics.

   Data returned to the calling program must be delayed by one 
   granule. 

   This is done in two places.  
   If we do not need to know the blocktype, the copying
   can be done here at the top of the program: we copy the data for
   the last granule (computed during the last call) before it is
   overwritten with the new data.  It looks like this:
  
   0. static psymodel_data 
   1. calling_program_data = psymodel_data
   2. compute psymodel_data
    
   For data which needs to know the blocktype, the copying must be
   done at the end of this loop, and the old values must be saved:
   
   0. static psymodel_data_old 
   1. compute psymodel_data
   2. compute possible block type of this granule
   3. compute final block type of previous granule based on #2.
   4. calling_program_data = psymodel_data_old
   5. psymodel_data_old = psymodel_data
*/





/* psycho_loudness_approx
   jd - 2001 mar 12
in:  energy   - BLKSIZE/2 elements of frequency magnitudes ^ 2
     gfp      - uses out_samplerate, ATHtype (also needed for ATHformula)
returns: loudness^2 approximation, a positive value roughly tuned for a value
         of 1.0 for signals near clipping.
notes:   When calibrated, feeding this function binary white noise at sample
         values +32767 or -32768 should return values that approach 3.
         ATHformula is used to approximate an equal loudness curve.
future:  Data indicates that the shape of the equal loudness curve varies
         with intensity.  This function might be improved by using an equal
         loudness curve shaped for typical playback levels (instead of the
         ATH, that is shaped for the threshold).  A flexible realization might
         simply bend the existing ATH curve to achieve the desired shape.
         However, the potential gain may not be enough to justify an effort.
*/
static FLOAT
psycho_loudness_approx( FLOAT *energy, lame_global_flags *gfp )
{
  int i;
  static int eql_type = -1;
  static FLOAT eql_w[BLKSIZE/2];/* equal loudness weights (based on ATH) */
  const FLOAT vo_scale= 1./( 14752 ); /* tuned for output level */
				      /* (sensitive to energy scale) */
  FLOAT loudness_power;

  if( eql_type != gfp->ATHtype ) { 
				/* compute equal loudness weights (eql_w) */
    FLOAT freq;
    FLOAT freq_inc = gfp->out_samplerate / (BLKSIZE);
    FLOAT eql_balance = 0.0;
    eql_type = gfp->ATHtype;
    freq = 0.0;
    for( i = 0; i < BLKSIZE/2; ++i ) {
      freq += freq_inc;
				/* convert ATH dB to relative power (not dB) */
				/*  to determine eql_w */
      eql_w[i] = 1. / pow( 10, ATHformula( freq, gfp ) / 10 );
      eql_balance += eql_w[i];
    }
    eql_balance = 1 / eql_balance;
    for( i = BLKSIZE/2; --i >= 0; ) { /* scale weights */
      eql_w[i] *= eql_balance;
    }
  }

  loudness_power = 0.0;
  for( i = 0; i < BLKSIZE/2; ++i ) { /* apply weights to power in freq. bands*/
    loudness_power += NON_LINEAR_SCALE_ITEM(energy[i]) * eql_w[i];
  }
  loudness_power = NON_LINEAR_SCALE_SUM(loudness_power);
  loudness_power /= (BLKSIZE/2);
  loudness_power *= vo_scale * vo_scale;

  return( loudness_power );
}

static void
compute_ffts(
    lame_global_flags *gfp,
    FLOAT fftenergy[HBLKSIZE],
    FLOAT (*fftenergy_s)[HBLKSIZE_s],
    FLOAT (*wsamp_l)[BLKSIZE],
    FLOAT (*wsamp_s)[3][BLKSIZE_s],
    int gr_out,
    int chn,
    const sample_t *buffer[2]
    )
{
    int b, j;
    lame_internal_flags *gfc=gfp->internal_flags;
    if (chn<2) {
	fft_long ( gfc, *wsamp_l, chn, buffer);
	fft_short( gfc, *wsamp_s, chn, buffer);
    }
    /* FFT data for mid and side channel is derived from L & R */
    else if (chn == 2) {
	for (j = BLKSIZE-1; j >=0 ; --j) {
	    FLOAT l = wsamp_l[0][j];
	    FLOAT r = wsamp_l[1][j];
	    wsamp_l[0][j] = (l+r)*(FLOAT)(SQRT2*0.5);
	    wsamp_l[1][j] = (l-r)*(FLOAT)(SQRT2*0.5);
	}
	for (b = 2; b >= 0; --b) {
	    for (j = BLKSIZE_s-1; j >= 0 ; --j) {
		FLOAT l = wsamp_s[0][b][j];
		FLOAT r = wsamp_s[1][b][j];
		wsamp_s[0][b][j] = (l+r)*(FLOAT)(SQRT2*0.5);
		wsamp_s[1][b][j] = (l-r)*(FLOAT)(SQRT2*0.5);
	    }
	}
    }
	
    /*********************************************************************
     *  compute energies
     *********************************************************************/
    fftenergy[0]  = NON_LINEAR_SCALE_ENERGY(wsamp_l[0][0]);
    fftenergy[0] *= fftenergy[0];

    for (j=BLKSIZE/2-1; j >= 0; --j) {
	FLOAT re = (*wsamp_l)[BLKSIZE/2-j];
	FLOAT im = (*wsamp_l)[BLKSIZE/2+j];
	fftenergy[BLKSIZE/2-j] = NON_LINEAR_SCALE_ENERGY((re * re + im * im) * 0.5f);
    }
    for (b = 2; b >= 0; --b) {
	fftenergy_s[b][0]  = (*wsamp_s)[b][0];
	fftenergy_s[b][0] *=  fftenergy_s [b][0];
	for (j=BLKSIZE_s/2-1; j >= 0; --j) {
	    FLOAT re = (*wsamp_s)[b][BLKSIZE_s/2-j];
	    FLOAT im = (*wsamp_s)[b][BLKSIZE_s/2+j];
	    fftenergy_s[b][BLKSIZE_s/2-j] = NON_LINEAR_SCALE_ENERGY((re * re + im * im) * 0.5f);
	}
    }
    /* total energy */
    {FLOAT8 totalenergy=0.0;
    for (j=11;j < HBLKSIZE; j++)
	totalenergy += fftenergy[j];

    gfc->tot_ener[chn] = totalenergy;
    }

#if defined(HAVE_GTK)
    if (gfp->analysis) {
	FLOAT mn,mx,ma=0,mb=0,mc=0;
	for (j=0; j<HBLKSIZE ; j++) {
	    gfc->pinfo->energy[gr_out][chn][j]=gfc->energy_save[chn][j];
	    gfc->energy_save[chn][j]=fftenergy[j];
	}
	for ( j = HBLKSIZE_s/2; j < HBLKSIZE_s; j ++) {
	    ma += fftenergy_s[0][j];
	    mb += fftenergy_s[1][j];
	    mc += fftenergy_s[2][j];
	}
	mn = Min(ma,mb);
	mn = Min(mn,mc);
	mx = Max(ma,mb);
	mx = Max(mx,mc);

	gfc->pinfo->ers[gr_out][chn]=gfc->ers_save[chn];
	gfc->ers_save[chn]=(mx/(1e-12+mn));
 	gfc->pinfo->pe[gr_out][chn]=gfc->pe[chn];
    }
#endif
    /*********************************************************************
     * compute loudness approximation (used for ATH auto-level adjustment) 
     *********************************************************************/
    if (gfp->athaa_loudapprox == 2 && chn < 2) {/*no loudness for mid/side ch*/
	gfc->loudness_sq[gr_out][chn] = gfc->loudness_sq_save[chn];
	gfc->loudness_sq_save[chn]
	    = psycho_loudness_approx(fftenergy, gfp);
    }
}

/*************************************************************** 
 * compute interchannel masking effects
 ***************************************************************/
static void
calc_interchannel_masking(
    lame_global_flags * gfp,
    FLOAT8 ratio
    )
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int sb, sblock;
    FLOAT8 l, r;
    for ( sb = 0; sb < SBMAX_l; sb++ ) {
	l = gfc->thm[0].l[sb];
	r = gfc->thm[1].l[sb];
	gfc->thm[0].l[sb] += r*ratio;
	gfc->thm[1].l[sb] += l*ratio;
    }
    for ( sb = 0; sb < SBMAX_s; sb++ ) {
	for ( sblock = 0; sblock < 3; sblock++ ) {
	    l = gfc->thm[0].s[sb][sblock];
	    r = gfc->thm[1].s[sb][sblock];
	    gfc->thm[0].s[sb][sblock] += r*ratio;
	    gfc->thm[1].s[sb][sblock] += l*ratio;
	}
    }
}



/*************************************************************** 
 * compute M/S thresholds from Johnston & Ferreira 1992 ICASSP paper
 ***************************************************************/
static void
msfix1(
    lame_internal_flags *gfc
    )
{
    int sb, sblock;
    FLOAT8 rside,rmid,mld;
#define chmid 2
#define chside 3
    for ( sb = 0; sb < SBMAX_l; sb++ ) {
	/* use this fix if L & R masking differs by 2db or less */
	/* if db = 10*log10(x2/x1) < 2 */
	/* if (x2 < 1.58*x1) { */
	if (gfc->thm[0].l[sb] > 1.58*gfc->thm[1].l[sb]
	 || gfc->thm[1].l[sb] > 1.58*gfc->thm[0].l[sb])
	    continue;

	mld = gfc->mld_l[sb]*gfc->en[chside].l[sb];
	rmid = Max(gfc->thm[chmid].l[sb], Min(gfc->thm[chside].l[sb],mld));

	mld = gfc->mld_l[sb]*gfc->en[chmid].l[sb];
	rside = Max(gfc->thm[chside].l[sb], Min(gfc->thm[chmid].l[sb],mld));
	gfc->thm[chmid].l[sb]=rmid;
	gfc->thm[chside].l[sb]=rside;
    }

    for ( sb = 0; sb < SBMAX_s; sb++ ) {
	for ( sblock = 0; sblock < 3; sblock++ ) {
	    if (gfc->thm[0].s[sb][sblock] > 1.58*gfc->thm[1].s[sb][sblock]
	     || gfc->thm[1].s[sb][sblock] > 1.58*gfc->thm[0].s[sb][sblock])
		continue;

	    mld = gfc->mld_s[sb]*gfc->en[chside].s[sb][sblock];
	    rmid = Max(gfc->thm[chmid].s[sb][sblock],
		       Min(gfc->thm[chside].s[sb][sblock],mld));

	    mld = gfc->mld_s[sb]*gfc->en[chmid].s[sb][sblock];
	    rside = Max(gfc->thm[chside].s[sb][sblock],
			Min(gfc->thm[chmid].s[sb][sblock],mld));

	    gfc->thm[chmid].s[sb][sblock]=rmid;
	    gfc->thm[chside].s[sb][sblock]=rside;
	}
    }
}

/*************************************************************** 
 * Adjust M/S maskings if user set "msfix"
 ***************************************************************/
/* Naoki Shibata 2000 */
static void
ns_msfix(
    lame_internal_flags *gfc,
    FLOAT8 msfix,
    FLOAT8 athadjust
    )
{
    int sb, sblock;
    FLOAT8 msfix2 = msfix;
    FLOAT8 athlower = pow(10, athadjust);
    if (gfc->presetTune.use)
	msfix2 = gfc->presetTune.ms_maskadjust;

    msfix *= 2.0;
    msfix2 *= 2.0;
    for ( sb = 0; sb < SBMAX_l; sb++ ) {
	FLOAT8 thmL,thmR,thmM,thmS,ath;
	ath  = (gfc->ATH->cb[gfc->bm_l[sb]])*athlower;
	thmL = Max(gfc->thm[0].l[sb],ath);
	thmR = Max(gfc->thm[1].l[sb],ath);
	thmM = Max(gfc->thm[2].l[sb],ath);
	thmS = Max(gfc->thm[3].l[sb],ath);

	if (thmL*msfix < thmM+thmS) {
	    FLOAT8 f = thmL * msfix2 / (thmM+thmS);
	    thmM *= f;
	    thmS *= f;
	}
	if (thmR*msfix < thmM+thmS) {
	    FLOAT8 f = thmR * msfix2 / (thmM+thmS);
	    thmM *= f;
	    thmS *= f;
	}
	gfc->thm[2].l[sb] = Min(thmM,gfc->thm[2].l[sb]);
	gfc->thm[3].l[sb] = Min(thmS,gfc->thm[3].l[sb]);
    }

    for ( sb = 0; sb < SBMAX_s; sb++ ) {
	for ( sblock = 0; sblock < 3; sblock++ ) {
	    FLOAT8 thmL,thmR,thmM,thmS,ath;
	    ath  = (gfc->ATH->cb[gfc->bm_s[sb]])*athlower;
	    thmL = Max(gfc->thm[0].s[sb][sblock],ath);
	    thmR = Max(gfc->thm[1].s[sb][sblock],ath);
	    thmM = Max(gfc->thm[2].s[sb][sblock],ath);
	    thmS = Max(gfc->thm[3].s[sb][sblock],ath);

	    if (thmL*msfix < thmM+thmS) {
		FLOAT8 f = thmL*msfix / (thmM+thmS);
		thmM *= f;
		thmS *= f;
	    }
	    if (thmR*msfix < thmM+thmS) {
		FLOAT8 f = thmR*msfix / (thmM+thmS);
		thmM *= f;
		thmS *= f;
	    }
	    gfc->thm[2].s[sb][sblock] = Min(gfc->thm[2].s[sb][sblock],thmM);
	    gfc->thm[3].s[sb][sblock] = Min(gfc->thm[3].s[sb][sblock],thmS);
	}
    }
}

static FLOAT8 calc_mixed_ratio(
    lame_internal_flags *gfc,
    int chn
    )
{
    int sb;
    FLOAT8 m0 = 1.0;
    for (sb = 0; sb < 8; sb++) {
	if (gfc->en[chn].l[sb] > gfc->thm[chn].l[sb]
	    && gfc->thm[chn].l[sb] > 0.0) {
	    m0 *= gfc-> en[chn].l[sb] / gfc->thm[chn].l[sb];
	}
    }

    for (sb = 0; sb < 3; sb++) {
	if (gfc->en[chn].s[sb][0] > gfc->thm[chn].s[sb][0]
	    && gfc->thm[chn].s[sb][0] > 0.0) {
	    m0 *= gfc->thm[chn].s[sb][0] / gfc-> en[chn].s[sb][0];
	}
	if (gfc->en[chn].s[sb][1] > gfc->thm[chn].s[sb][1]
	    && gfc->thm[chn].s[sb][1] > 0.0) {
	    m0 *= gfc->thm[chn].s[sb][1] / gfc-> en[chn].s[sb][1];
	}
	if (gfc->en[chn].s[sb][2] > gfc->thm[chn].s[sb][2]
	    && gfc->thm[chn].s[sb][2] > 0.0) {
	    m0 *= gfc->thm[chn].s[sb][2] / gfc-> en[chn].s[sb][2];
	}
    }
    return m0;
}

/* longblock threshold calculation (part 2) */
static void convert_partition2scalefac_l(
    lame_internal_flags *gfc,
    FLOAT8 *eb,
    FLOAT8 *thr,
    int chn
    )
{
    FLOAT8 enn, thmm;
    int sb, b;
    enn = thmm = 0.0;
    sb = b = 0;
    for (;;) {
	while (b < gfc->bo_l[sb]) {
	    enn  += eb[b];
	    thmm += thr[b];
	    b++;
	}

	if (sb == SBMAX_l - 1)
	    break;

	gfc->en [chn].l[sb] = enn  + 0.5 * eb [b];
	gfc->thm[chn].l[sb] = thmm + 0.5 * thr[b];

	enn  = 0.5 *  eb[b];
	thmm = 0.5 * thr[b];
	b++;
	sb++;
    }

    gfc->en [chn].l[SBMAX_l-1] = enn;
    gfc->thm[chn].l[SBMAX_l-1] = thmm;
}

static void
compute_masking_s(
    lame_internal_flags *gfc,
    FLOAT (*fftenergy_s)[HBLKSIZE_s],
    FLOAT8 *eb,
    FLOAT8 *thr,
    int chn,
    int sblock
    )
{
    int j, b;
    for (j = b = 0; b < gfc->npart_s; b++) {
	FLOAT ecb = fftenergy_s[sblock][j++];
	int kk = gfc->numlines_s[b];
	while (--kk > 0)
	    ecb += fftenergy_s[sblock][j++];
	eb[b] = ecb;
    }
    for (j = b = 0; b < gfc->npart_s; b++) {
	int kk = gfc->s3ind_s[b][0];
	FLOAT8 ecb = gfc->s3_ss[j++] * eb[kk++];
	while (kk <= gfc->s3ind_s[b][1])
	    ecb += gfc->s3_ss[j++] * eb[kk++];

	thr[b] = Min( ecb, rpelev_s  * gfc->nb_s1[chn][b] );
	if (gfc->blocktype_old[chn & 1] == SHORT_TYPE ) {
	    thr[b] = Min(thr[b], rpelev2_s * gfc->nb_s2[chn][b]);
	}
	thr[b] = Max( thr[b], 1e-37 );
	gfc->nb_s2[chn][b] = gfc->nb_s1[chn][b];
	gfc->nb_s1[chn][b] = ecb;
    }
}

static void
block_type_set(
    lame_global_flags * gfp,
    int *uselongblock,
    int *blocktype_d,
    int *blocktype
    )
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int chn;

    if (gfp->short_blocks == short_block_coupled
	/* force both channels to use the same block type */
	/* this is necessary if the frame is to be encoded in ms_stereo.  */
	/* But even without ms_stereo, FhG  does this */
	&& !(uselongblock[0] && uselongblock[1]))
        uselongblock[0] = uselongblock[1] = 0;

    /* update the blocktype of the previous granule, since it depends on what
     * happend in this granule */
    for (chn=0; chn<gfc->channels_out; chn++) {
	blocktype[chn] = NORM_TYPE;
	/* disable short blocks */
	if (gfp->short_blocks == short_block_dispensed)
	    uselongblock[chn]=1;
	if (gfp->short_blocks == short_block_forced)
	    uselongblock[chn]=0;

	if (uselongblock[chn]) {
	    /* no attack : use long blocks */
	    assert( gfc->blocktype_old[chn] != START_TYPE );
	    if (gfc->blocktype_old[chn] == SHORT_TYPE)
		blocktype[chn] = STOP_TYPE;
	} else {
	    /* attack : use short blocks */
	    blocktype[chn] = SHORT_TYPE;
	    if (gfc->blocktype_old[chn] == NORM_TYPE) {
		int oldblocktype = START_TYPE;
		if (calc_mixed_ratio(gfc, chn) > 1000) {
		    blocktype[chn] = -SHORT_TYPE;
		    oldblocktype = -oldblocktype;
		}
		gfc->blocktype_old[chn] = oldblocktype;
	    }
	    if (gfc->blocktype_old[chn] == STOP_TYPE)
		gfc->blocktype_old[chn] = SHORT_TYPE;
	}

	blocktype_d[chn] = gfc->blocktype_old[chn];  /* value returned to calling program */
	gfc->blocktype_old[chn] = blocktype[chn];    /* save for next call to l3psy_anal */
    }
}

int L3psycho_anal( lame_global_flags * gfp,
                    const sample_t *buffer[2], int gr_out, 
                    FLOAT8 *ms_ratio,
                    FLOAT8 *ms_ratio_next,
		    III_psy_ratio masking_ratio[2][2],
		    III_psy_ratio masking_MS_ratio[2][2],
		    FLOAT8 percep_entropy[2],FLOAT8 percep_MS_entropy[2], 
                    FLOAT8 energy[4],
                    int blocktype_d[2])
{
    lame_internal_flags *gfc=gfp->internal_flags;

    /* fft and energy calculation   */
    FLOAT wsamp_L[2][BLKSIZE];
    FLOAT wsamp_S[2][3][BLKSIZE_s];
    FLOAT fftenergy[HBLKSIZE];
    FLOAT fftenergy_s[3][HBLKSIZE_s];

    /* convolution   */
    FLOAT8 eb[CBANDS];
    FLOAT8 cb[CBANDS];
    FLOAT8 thr[CBANDS];

    /* ratios    */
    FLOAT8 ms_ratio_l=0, ms_ratio_s=0;

    /* block type  */
    int blocktype[2],uselongblock[2];

    /* usual variables like loop indices, etc..    */
    int numchn, chn;
    int b, i, j, k;
    int sb,sblock;

    numchn = gfc->channels_out;
    /* chn=2 and 3 = Mid and Side channels */
    if (gfp->mode == JOINT_STEREO) numchn=4;

    for (chn=0; chn<numchn; chn++) {
	FLOAT (*wsamp_l)[BLKSIZE];
	FLOAT (*wsamp_s)[3][BLKSIZE_s];
	energy[chn] = gfc->tot_ener[chn];

	/* there is a one granule delay.  Copy maskings computed last call
	 * into masking_ratio to return to calling program.
	 */
	if (chn < 2) {
	    /* LR maskings  */
	    percep_entropy            [chn]       = gfc -> pe  [chn];
	    masking_ratio    [gr_out] [chn]  .en  = gfc -> en  [chn];
	    masking_ratio    [gr_out] [chn]  .thm = gfc -> thm [chn];
	} else {
	    /* MS maskings  */
	    percep_MS_entropy         [chn-2]     = gfc -> pe  [chn]; 
	    masking_MS_ratio [gr_out] [chn-2].en  = gfc -> en  [chn];
	    masking_MS_ratio [gr_out] [chn-2].thm = gfc -> thm [chn];
	}

	/*********************************************************************
	 *  compute FFTs
	 *********************************************************************/
	wsamp_s = wsamp_S+(chn & 1);
	wsamp_l = wsamp_L+(chn & 1);
	compute_ffts(gfp, fftenergy, fftenergy_s,
		     wsamp_l, wsamp_s, gr_out, chn, buffer);

	/*********************************************************************
	 *    compute unpredicatability of first six spectral lines
	 *********************************************************************/
	for ( j = 0; j < CW_LOWER_INDEX; j++ ) {
	    /* calculate unpredictability measure cw */
	    FLOAT a2, b2, r1, r2;
	    FLOAT numre, numim, den;

	    a2 = gfc-> ax_sav[chn][1][j];
	    b2 = gfc-> bx_sav[chn][1][j];

	    r2 = gfc-> rx_sav[chn][1][j];
	    r1 = gfc-> rx_sav[chn][1][j] = gfc-> rx_sav[chn][0][j];

	    /* square (x1,y1) */
	    if (r1 != 0.0) {
		FLOAT a1 = gfc-> ax_sav[chn][1][j] = gfc-> ax_sav[chn][0][j];
		FLOAT b1 = gfc-> bx_sav[chn][1][j] = gfc-> bx_sav[chn][0][j];
		den = r1*r1;
		numre = a1*b1;
		numim = den-b1*b1;
	    } else {
		/* no aging is needed for ax_sav[chn][0][j] and that of bx
		   because if r1=0, r2 should be 0 for next time. */
		den = numre = 1.0;
		numim = 0.0;
	    }

	    /* multiply by (x2,-y2) */
	    if (r2 != 0.0) {
		FLOAT tmp2 = (numim+numre)*(a2+b2)*0.5f;
		FLOAT tmp1 = -a2*numre+tmp2;
		numre =      -b2*numim+tmp2;
		numim = tmp1;
		den *= r2;
	    }

	    r1 = 2.0f*r1-r2;
	    r2 = gfc-> rx_sav[chn][0][j] = sqrt(fftenergy[j]);
	    r2 = r2+fabs(r1);
	    if (r2 != 0) {
		FLOAT an = gfc-> ax_sav[chn][0][j] = wsamp_l[0][j];
		FLOAT bn = gfc-> bx_sav[chn][0][j] = j==0 ? wsamp_l[0][0] : wsamp_l[0][BLKSIZE-j];  
		den = r1/den*2.0;
		numre = (an+bn)-numre*den;
		numim = (an-bn)-numim*den;
		r2 = sqrt(numre*numre+numim*numim)/(r2*2.0);
	    }
	    gfc->cw[j] = r2;
	}

	/**********************************************************************
	 *     compute unpredicatibility of next 200 spectral lines
	 *********************************************************************/
	for (; j < gfc->cw_upper_index; j += 4 ) {
	    /* calculate unpredictability measure cw */
	    FLOAT rn, r1, r2;
	    FLOAT numre, numim, den;
	    k = (j+2) / 4;
	    /* square (x1,y1) */
	    r1 = fftenergy_s[0][k];
	    if (r1 != 0.0) {
		FLOAT a1 = (*wsamp_s)[0][k]; 
		FLOAT b1 = (*wsamp_s)[0][BLKSIZE_s-k]; /* k is never 0 */
		numre = a1*b1;
		numim = r1-b1*b1;
		den = r1;
		r1 = sqrt(r1);
	    } else {
		den = numre = 1.0;
		numim = 0.0;
	    }

	    /* multiply by (x2,-y2) */
	    r2 = fftenergy_s[2][k];
	    if (r2 != 0.0) {
		FLOAT a2 = (*wsamp_s)[2][k]; 
		FLOAT b2 = (*wsamp_s)[2][BLKSIZE_s-k];

		FLOAT tmp2 = (numim+numre)*(a2+b2)*0.5f;
		FLOAT tmp1 = tmp2-a2*numre;
		numre =      tmp2-b2*numim;
		numim = tmp1;

		r2 = sqrt(r2);
		den *= r2;
	    }

	    /* r-prime factor */
	    rn = sqrt(fftenergy_s[1][k])+fabs(2*r1-r2);
	    if (rn != 0) {
		FLOAT an = (*wsamp_s)[1][k]; 
		FLOAT bn = (*wsamp_s)[1][BLKSIZE_s-k];
		den = (2*r1-r2)/den*2.0f;
		numre = (an+bn)-numre*den;
		numim = (an-bn)-numim*den;
		rn = sqrt(numre*numre+numim*numim)/(rn*2.0f);
	    }
	    gfc->cw[j+1] = gfc->cw[j+2] = gfc->cw[j+3] = gfc->cw[j] = rn;
	}

	/**********************************************************************
	 *    Calculate the energy and the unpredictability in the threshold
	 *    calculation partitions
	 *********************************************************************/
	b = 0;
	for (j = 0; j < gfc->cw_upper_index
		 && gfc->numlines_l[b] && b < gfc->npart_l; ) {
	    FLOAT8 ebb, cbb;

	    ebb = NON_LINEAR_SCALE_ITEM(fftenergy[j]);
	    cbb = NON_LINEAR_SCALE_ITEM(fftenergy[j] * gfc->cw[j]);
	    j++;

	    for (i = gfc->numlines_l[b] - 1; i > 0; i--) {
		ebb += NON_LINEAR_SCALE_ITEM(fftenergy[j]);
		/* XXX: should "* gfc->cw[j])" be outside of the scaling? */
		cbb += NON_LINEAR_SCALE_ITEM(fftenergy[j] * gfc->cw[j]);
		j++;
	    }
	    eb[b] = NON_LINEAR_SCALE_SUM(ebb);
	    cb[b] = NON_LINEAR_SCALE_SUM(cbb);
	    b++;
	}

	for (; b < gfc->npart_l; b++ ) {
	    FLOAT8 ebb = NON_LINEAR_SCALE_ITEM(fftenergy[j++]);
	    assert(gfc->numlines_l[b]);
	    for (i = gfc->numlines_l[b] - 1; i > 0; i--) {
		ebb += NON_LINEAR_SCALE_ITEM(fftenergy[j++]);
	    }
	    eb[b] = NON_LINEAR_SCALE_SUM(ebb);
	    /* XXX: should the "* .4" be outside of the scaling? */
	    cb[b] = NON_LINEAR_SCALE_SUM(ebb * 0.4);
	}

	/**********************************************************************
	 *      convolve the partitioned energy and unpredictability
	 *      with the spreading function, s3_l[b][k](packed into s3_ll)
	 *********************************************************************/
	/*  calculate percetual entropy */
	gfc->pe[chn] = 0;
	k = 0;
	for ( b = 0;b < gfc->npart_l; b++ ) {
	    FLOAT8 tbb,ecb,ctb;
	    int kk;
	    ecb = ctb = 0.;
	    for (kk = gfc->s3ind[b][0]; kk <= gfc->s3ind[b][1]; kk++ ) {
		/* sprdngf for Layer III */
		ecb += gfc->s3_ll[k] * eb[kk];
		ctb += gfc->s3_ll[k] * cb[kk];
		k++;
	    }

/* calculate the tonality of each threshold calculation partition 
 * calculate the SNR in each threshold calculation partition 
 * tonality = -0.299 - .43*log(ctb/ecb);
 * tonality = 0:           use NMT   (lots of masking)
 * tonality = 1:           use TMN   (little masking)
 */

/* ISO values */
#define CONV1 (-.299)
#define CONV2 (-.43)

	    tbb = ecb;
	    if (tbb != 0.0) {
		tbb = ctb / tbb;
		/* convert to tonality index */
		/* tonality small:   tbb=1 */
		/* tonality large:   tbb=-.299 */
		tbb = CONV1 + CONV2*FAST_LOG(tbb);
		if (tbb < 0.0) tbb = exp(-LN_TO_LOG10*NMT);
		else if (tbb > 1.0) tbb = exp(-LN_TO_LOG10*TMN);
		else tbb = exp(-LN_TO_LOG10 * ( (TMN-NMT)*tbb + NMT ));
	    }

/* at this point, tbb represents the amount the spreading function
 * will be reduced.  The smaller the value, the less masking.
 * minval[] = 1 (0db)     says just use tbb.
 * minval[]= .01 (-20db)  says reduce spreading function by at least 20db.
 */
	    tbb = Min(gfc->minval[b], tbb);
	    /* stabilize tonality estimation */
	    if (gfc->PSY->tonalityPatch && b > 5) {
		FLOAT8 const x = 1.8699422;
		FLOAT8 w = gfc->PSY->prvTonRed[b/2] * x;
		if (tbb > w) 
		    tbb = w;
		gfc->PSY->prvTonRed[b] = tbb;
	    }
	    ecb *= tbb;

	    /* long block pre-echo control.   */
	    /* rpelev=2.0, rpelev2=16.0 */
	    /* note: all surges in PE are because of this pre-echo formula
	     * for thr[b].  If it this is not used, PE is always around 600
	     */
	    /* dont use long block pre-echo control if previous granule was
	     * a short block.  This is to avoid the situation:   
	     * frame0:  quiet (very low masking)  
	     * frame1:  surge  (triggers short blocks)
	     * frame2:  regular frame. looks like pre-echo when compared to
	     *          frame0, but all pre-echo was in frame1.
	     */
	    /* chn=0,1   L and R channels
	       chn=2,3   S and M channels.  
	    */
	    thr[b] = Min(ecb, rpelev*gfc->nb_1[chn][b]);
	    if (gfc->blocktype_old[chn & 1] != SHORT_TYPE
		&& thr[b] > rpelev2*gfc->nb_2[chn][b])
		thr[b] = rpelev2*gfc->nb_2[chn][b];

	    gfc->nb_2[chn][b] = gfc->nb_1[chn][b];
	    gfc->nb_1[chn][b] = ecb;

	    ecb = Max(thr[b], gfc->ATH->cb[b]);
	    if (ecb < eb[b])
		gfc->pe[chn] -= gfc->numlines_l[b] * FAST_LOG(ecb / eb[b]);
	}

	/*************************************************************** 
	 * determine the block type (window type) based on L & R channels
	 ***************************************************************/
	{  /* compute PE for all 4 channels */
	    FLOAT mn,mx,ma=0,mb=0,mc=0;
            for ( j = HBLKSIZE_s/2; j < HBLKSIZE_s; j ++) {
		ma += fftenergy_s[0][j];
		mb += fftenergy_s[1][j];
		mc += fftenergy_s[2][j];
	    }
	    mn = Min(ma,mb);
	    mn = Min(mn,mc);
	    mx = Max(ma,mb);
	    mx = Max(mx,mc);

	    /* bit allocation is based on pe.  */
	    if (mx>mn) {
		FLOAT8 tmp = 400*FAST_LOG(mx/(1e-12+mn));
		if (tmp>gfc->pe[chn]) gfc->pe[chn]=tmp;
	    }

	    /* block type is based just on L or R channel */      
	    if (chn<2) {
		uselongblock[chn] = 1;

		/* tuned for t1.wav.  doesnt effect most other samples */
		if (gfc->pe[chn] > 3000) 
		    uselongblock[chn]=0;
	
		if ( mx > 30*mn ) 
		{/* big surge of energy - always use short blocks */
		    uselongblock[chn] = 0;
		}
		else if ((mx > 10*mn) && (gfc->pe[chn] > 1000))
		{/* medium surge, medium pe - use short blocks */
		    uselongblock[chn] = 0;
		}
	    }
	}

	/* compute masking thresholds for long blocks */
	convert_partition2scalefac_l(gfc, eb, thr, chn);

	/* compute masking thresholds for short blocks */
	for (sblock = 0; sblock < 3; sblock++) {
	    FLOAT8 enn, thmm;
	    compute_masking_s(gfc, fftenergy_s, eb, thr, chn, sblock);
	    b = -1;
	    enn = thmm = 0.0;
	    for (sb = 0; sb < SBMAX_s; sb++) {
		while (++b < gfc->bo_s[sb]) {
		    enn  += eb[b];
		    thmm += thr[b];
		}
		enn  += 0.5 * eb[b];
		thmm += 0.5 * thr[b];
		gfc->en [chn].s[sb][sblock] = enn;
		gfc->thm[chn].s[sb][sblock] = thmm;
		enn  = 0.5 * eb[b];
		thmm = 0.5 * thr[b];
	    }
	    gfc->en [chn].s[sb-1][sblock] += enn;
	    gfc->thm[chn].s[sb-1][sblock] += thmm;
	}
    } /* end loop over chn */

    if (gfp->interChRatio != 0.0)
	calc_interchannel_masking(gfp, gfp->interChRatio);

    if (gfp->mode == JOINT_STEREO) {
	FLOAT8 db,x1,x2,sidetot=0,tot=0;
	msfix1(gfc);
	if (gfp->msfix != 0.0)
	    ns_msfix(gfc, gfp->msfix, gfp->ATHlower);

	/* determin ms_ratio from masking thresholds*/
	/* use ms_stereo (ms_ratio < .35) if average thresh. diff < 5 db */
	for (sb= SBMAX_l/4 ; sb< SBMAX_l; sb ++ ) {
	    x1 = Min(gfc->thm[0].l[sb],gfc->thm[1].l[sb]);
	    x2 = Max(gfc->thm[0].l[sb],gfc->thm[1].l[sb]);
	    /* thresholds difference in db */
	    if (x2 >= 1000*x1)  db=3;
	    else db = FAST_LOG10(x2/x1);  
	    /*  DEBUGF(gfc,"db = %f %e %e  \n",db,gfc->thm[0].l[sb],gfc->thm[1].l[sb]);*/
	    sidetot += db;
	    tot++;
	}
	ms_ratio_l= (sidetot/tot)*0.7; /* was .35*(sidetot/tot)/5.0*10 */
	ms_ratio_l = Min(ms_ratio_l,0.5);

	sidetot=0; tot=0;
	for ( sblock = 0; sblock < 3; sblock++ )
	    for ( sb = SBMAX_s/4; sb < SBMAX_s; sb++ ) {
		x1 = Min(gfc->thm[0].s[sb][sblock],gfc->thm[1].s[sb][sblock]);
		x2 = Max(gfc->thm[0].s[sb][sblock],gfc->thm[1].s[sb][sblock]);
		/* thresholds difference in db */
		if (x2 >= 1000*x1)  db=3;
		else db = FAST_LOG10(x2/x1);
		sidetot += db;
		tot++;
	    }
	ms_ratio_s = (sidetot/tot)*0.7; /* was .35*(sidetot/tot)/5.0*10 */
	ms_ratio_s = Min(ms_ratio_s,.5);
    }

    /*************************************************************** 
     * determine final block type
     ***************************************************************/
    block_type_set(gfp, uselongblock, blocktype_d, blocktype);

    if (blocktype_d[0]==SHORT_TYPE && blocktype_d[1]==SHORT_TYPE)
	*ms_ratio = gfc->ms_ratio_s_old;
    else
	*ms_ratio = gfc->ms_ratio_l_old;

    gfc->ms_ratio_s_old = ms_ratio_s;
    gfc->ms_ratio_l_old = ms_ratio_l;

    /* we dont know the block type of this frame yet - assume long */
    *ms_ratio_next = ms_ratio_l;

    return 0;
}



/* mask_add optimization */
/* init the limit values used to avoid computing log in mask_add when it is not necessary */

/* For example, with i = 10*log10(m2/m1)/10*16         (= log10(m2/m1)*16)
 *
 * abs(i)>8 is equivalent (as i is an integer) to
 * abs(i)>=9
 * i>=9 || i<=-9
 * equivalent to (as i is the biggest integer smaller than log10(m2/m1)*16 
 * or the smallest integer bigger than log10(m2/m1)*16 depending on the sign of log10(m2/m1)*16)
 * log10(m2/m1)>=9/16 || log10(m2/m1)<=-9/16
 * exp10 is strictly increasing thus this is equivalent to
 * m2/m1 >= 10^(9/16) || m2/m1<=10^(-9/16) which are comparisons to constants
 */


#define I1LIMIT 8   /* as in if(i>8)  */ 
#define I2LIMIT 24  /* as in if(i>24) */ 
#define MLIMIT  15  /* as in if(m<15) */ 

static FLOAT8 ma_max_i1;
static FLOAT8 ma_min_i1;
static FLOAT8 ma_max_i2;
static FLOAT8 ma_min_i2;
static FLOAT8 ma_max_m;



static void init_mask_add_max_values(void)
{
    ma_max_i1 = pow(10,(I1LIMIT+1)/16.0);
    ma_min_i1 = pow(10,-(I1LIMIT+1)/16.0);
    ma_max_i2 = pow(10,(I2LIMIT+1)/16.0);
    ma_min_i2 = pow(10,-(I2LIMIT+1)/16.0);
    ma_max_m  = pow(10,(MLIMIT)/10.0);
}






/* addition of simultaneous masking   Naoki Shibata 2000/7 */
inline static FLOAT8 mask_add(FLOAT8 m1,FLOAT8 m2,int k,int b, lame_internal_flags * const gfc)
{
  static const FLOAT8 table1[] = {
    3.3246 *3.3246 ,3.23837*3.23837,3.15437*3.15437,3.00412*3.00412,2.86103*2.86103,2.65407*2.65407,2.46209*2.46209,2.284  *2.284  ,
    2.11879*2.11879,1.96552*1.96552,1.82335*1.82335,1.69146*1.69146,1.56911*1.56911,1.46658*1.46658,1.37074*1.37074,1.31036*1.31036,
    1.25264*1.25264,1.20648*1.20648,1.16203*1.16203,1.12765*1.12765,1.09428*1.09428,1.0659 *1.0659 ,1.03826*1.03826,1.01895*1.01895,
    1
  };

  static const FLOAT8 table2[] = {
    1.33352*1.33352,1.35879*1.35879,1.38454*1.38454,1.39497*1.39497,1.40548*1.40548,1.3537 *1.3537 ,1.30382*1.30382,1.22321*1.22321,
    1.14758*1.14758
  };

  static const FLOAT8 table3[] = {
    2.35364*2.35364,2.29259*2.29259,2.23313*2.23313,2.12675*2.12675,2.02545*2.02545,1.87894*1.87894,1.74303*1.74303,1.61695*1.61695,
    1.49999*1.49999,1.39148*1.39148,1.29083*1.29083,1.19746*1.19746,1.11084*1.11084,1.03826*1.03826
  };


  int i;
  FLOAT8 m;
  FLOAT ratio;

  if (m1 == 0) return m2;

  ratio = m2/m1;

  if (b < 0) b = -b;

  /*i = abs(10*log10(m2 / m1)/10*16);
  m = 10*log10((m1+m2)/gfc->ATH->cb[k]);*/


  /* Should always be true, just checking */
  assert(m1>=0);
  assert(m2>=0);
  assert(gfc->ATH->cb[k]>=0);


  if (b <= 3) {  /* approximately, 1 bark = 3 partitions */
    /* 65% of the cases */

    /* originally 'if(i > 8)' */
    if(ratio>=ma_max_i1 || ratio <=ma_min_i1) {
      /* 43% of the total */
      return m1+m2;
    }

    /* 22% of the total */
    i = fabs(FAST_LOG10(ratio))*16;
    if (i > I1LIMIT)
      return m1+m2; /* just in case  8.99999 <= log10(m2/m1)*16 < 9 */

    return (m1+m2)*table2[i];
  }


  /* m<15 equ log10((m1+m2)/gfc->ATH->cb[k])<1.5
   * equ (m1+m2)/gfc->ATH->cb[k]<10^1.5
   * equ (m1+m2)<10^1.5 * gfc->ATH->cb[k]
   */

  if((m1+m2)<ma_max_m*gfc->ATH->cb[k])  {
    /* 3% of the total */
    i = fabs(FAST_LOG10(ratio))*16;

    /* Originally if (m > 0) { */
    if(m1+m2>gfc->ATH->cb[k]) {
      FLOAT8 f=1.0,r;

      if (i > 24) return m1+m2;

      if (i > 13) f = 1; else f = table3[i];

      m = 10*FAST_LOG10((m1+m2)/gfc->ATH->cb[k]);
      r = (m-0)/15;

      return (m1+m2)*(table1[i]*r+f*(1-r));
    }

    if (i > 13) return m1+m2;

    return (m1+m2)*table3[i];
  }


  /* orginally 'if (i > 24) {' */
  if(ratio>=ma_max_i2 || ratio <=ma_min_i2) {
    /* 22% of total */
    return m1+m2;
  }

  
  /* 10% of total */
  i = fabs(FAST_LOG10(ratio))*16;
  if (i > I2LIMIT)
    return m1+m2; /* just in case... */

  return (m1+m2)*table1[i];
}



static inline FLOAT8 NS_INTERP(FLOAT8 x, FLOAT8 y, FLOAT8 r)
{
    /* was pow((x),(r))*pow((y),1-(r))*/
    if(r==1.0)
        return x;              /* 99.7% of the time */ 
    if(y!=0.0)
        return pow(x/y,r)*y;   /* rest of the time */ 
    return 0.0;                /* never happens */ 
}



static void nsPsy2dataRead(
    FILE *fp,
    FLOAT8 *eb2,
    FLOAT8 *eb,
    int chn,
    int npart_l
    )
{
    int b;
    for(;;) {
	static const char chname[] = {'L','R','M','S'};
	char c;

	fscanf(fp, "%c",&c);
	for (b=0; b < npart_l; b++) {
	    double e;
	    fscanf(fp, "%lf",&e);
	    eb2[b] = e;
	}

	if (feof(fp)) abort();
	if (c == chname[chn]) break;
	abort();
    }

    eb2[62] = eb2[61];
    for (b=0; b < npart_l; b++ )
	eb2[b] = eb2[b] * eb[b];
}

static FLOAT
pecalc_s(
    III_psy_ratio *mr,
    FLOAT8 masking_lower
    )
{
    FLOAT8 pe_s;
    const static FLOAT8 regcoef_s[] = {
	0, 0, 0, /* I don't know why there're 0 -tt- */
	0.434542,25.0738,
	0, 0, 0,
	19.5442,19.7486,60,100,0
    };
    int sb, sblock;

    pe_s = 1236.28/4;
    for(sblock=0;sblock<3;sblock++) {
	for ( sb = 0; sb < SBMAX_s; sb++ ) {
	    FLOAT x;
	    if (regcoef_s[sb] == 0.0
		|| mr->thm.s[sb][sblock] <= 0.0
		|| mr->en.s[sb][sblock]
		<= (x = mr->thm.s[sb][sblock] * masking_lower))
		continue;

	    if (mr->en.s[sb][sblock] > x*1e10)
		pe_s += regcoef_s[sb] * (10.0 * LOG10);
	    else
		pe_s += regcoef_s[sb] * FAST_LOG10(mr->en.s[sb][sblock] / x);
	}
    }
    return pe_s;
}

static FLOAT
pecalc_l(
    III_psy_ratio *mr,
    FLOAT8 masking_lower
    )
{
    FLOAT8 pe_l;
    const static FLOAT8 regcoef_l[] = {
	10.0583,10.7484,7.29006,16.2714,6.2345,4.09743,3.05468,3.33196,
	2.54688, 3.68168,5.83109,2.93817,-8.03277,-10.8458,8.48777,
	9.13182,2.05212,8.6674,50.3937,73.267,97.5664,0
    };
    int sb;

    pe_l = 1124.23/4;
    for ( sb = 0; sb < SBMAX_l; sb++ ) {
	FLOAT x;
	if (mr->thm.l[sb] <= 0.0
	    || mr->en.l[sb] <= (x = mr->thm.l[sb]*masking_lower))
	    continue;

	if (mr->en.l[sb] > x*1e10)
	    pe_l += regcoef_l[sb] * (10.0 * LOG10);
	else
	    pe_l += regcoef_l[sb] * FAST_LOG10(mr->en.l[sb] / x);
    }

    return pe_l;
}





int L3psycho_anal_ns( lame_global_flags * gfp,
                    const sample_t *buffer[2], int gr_out, 
                    FLOAT8 *ms_ratio,
                    FLOAT8 *ms_ratio_next,
		    III_psy_ratio masking_ratio[2][2],
		    III_psy_ratio masking_MS_ratio[2][2],
		    FLOAT8 percep_entropy[2],FLOAT8 percep_MS_entropy[2], 
		    FLOAT8 energy[4], 
                    int blocktype_d[2])
{
/* to get a good cache performance, one has to think about
 * the sequence, in which the variables are used.  
 * (Note: these static variables have been moved to the gfc-> struct,
 * and their order in memory is layed out in util.h)
 */
    lame_internal_flags *gfc=gfp->internal_flags;

    /* fft and energy calculation   */
    FLOAT wsamp_L[2][BLKSIZE];
    FLOAT wsamp_S[2][3][BLKSIZE_s];

    /* block type  */
    int blocktype[2],uselongblock[4];

    /* usual variables like loop indices, etc..    */
    int numchn, chn;
    int b, i, j, k;
    int	sb,sblock;

    /* variables used for --nspsytune */
    FLOAT ns_hpfsmpl[2][576];
    FLOAT pcfact;

    numchn = gfc->channels_out;
    /* chn=2 and 3 = Mid and Side channels */
    if (gfp->mode == JOINT_STEREO) numchn=4;

    if (gfp->VBR==vbr_off) pcfact = gfc->ResvMax == 0 ? 0 : ((FLOAT)gfc->ResvSize)/gfc->ResvMax*0.5;
    else if (gfp->VBR == vbr_rh  ||  gfp->VBR == vbr_mtrh  ||  gfp->VBR == vbr_mt) {
	static const FLOAT pcQns[10]={1.0,1.0,1.0,0.8,0.6,0.5,0.4,0.3,0.2,0.1};
	pcfact = pcQns[gfp->VBR_q];
    } else pcfact = 1.0;

    /**********************************************************************
     *  Apply HPF of fs/4 to the input signal.
     *  This is used for attack detection / handling.
     **********************************************************************/
    /* Don't copy the input buffer into a temporary buffer */
    /* unroll the loop 2 times */
    for(chn=0;chn<gfc->channels_out;chn++) {
	static const FLOAT fircoef[] = {
	    -8.65163e-18,-0.00851586,-6.74764e-18, 0.0209036,
	    -3.36639e-17,-0.0438162 ,-1.54175e-17, 0.0931738,
	    -5.52212e-17,-0.313819
	};

	/* apply high pass filter of fs/4 */
	const sample_t * const firbuf = &buffer[chn][576-350-NSFIRLEN+192];
	for (i=0;i<576;i++) {
	    FLOAT sum1, sum2;
	    sum1 = 0.5 * firbuf[i + 10];
	    sum2 = 0.0;
	    for (j=0;j<(NSFIRLEN-1)/2;j+=2) {
		sum1 += fircoef[j  ] * (firbuf[i+j  ]+firbuf[i+NSFIRLEN-j  ]);
		sum2 += fircoef[j+1] * (firbuf[i+j+1]+firbuf[i+NSFIRLEN-j-1]);
	    }
	    ns_hpfsmpl[chn][i] = sum1 + sum2;
	}
    }

    for (chn=0; chn<numchn; chn++) {
	FLOAT (*wsamp_l)[BLKSIZE];
	FLOAT (*wsamp_s)[3][BLKSIZE_s];
	FLOAT en_subshort[12];
	FLOAT attack_intensity[12];
	int ns_uselongblock = 1;
	FLOAT attackThreshold;
	FLOAT8 max[CBANDS],avg[CBANDS];
	int ns_attacks[4] = {0};
	FLOAT fftenergy[HBLKSIZE];
	FLOAT fftenergy_s[3][HBLKSIZE_s];
	/* convolution   */
	FLOAT8 eb[CBANDS],eb2[CBANDS];
	FLOAT8 thr[CBANDS];
	static FLOAT8 tab[] = {
	    1.0,     0.79433, 0.63096, 0.63096,
	    0.63096, 0.63096, 0.63096, 0.25119,
	    0.11749
	};

	/* there is a one granule delay.  Copy maskings computed last call
	 * into masking_ratio to return to calling program.
	 */
	energy[chn]=gfc->tot_ener[chn];

	if (chn < 2) {    
	    /* LR maskings  */
	    //percep_entropy            [chn]       = gfc -> pe  [chn]; 
	    masking_ratio    [gr_out] [chn]  .en  = gfc -> en  [chn];
	    masking_ratio    [gr_out] [chn]  .thm = gfc -> thm [chn];
	} else {
	    /* MS maskings  */
	    //percep_MS_entropy         [chn-2]     = gfc -> pe  [chn]; 
	    masking_MS_ratio [gr_out] [chn-2].en  = gfc -> en  [chn];
	    masking_MS_ratio [gr_out] [chn-2].thm = gfc -> thm [chn];
	}

	/*********************************************************************
	 *  compute FFTs
	 *********************************************************************/
	wsamp_s = wsamp_S+(chn & 1);
	wsamp_l = wsamp_L+(chn & 1);
	compute_ffts(gfp, fftenergy, fftenergy_s,
		     wsamp_l, wsamp_s, gr_out, chn, buffer);

	/*********************************************************************
	 *    Calculate the energy and the tonality of each partition.
	 *********************************************************************/
	for (b = j = 0; b<gfc->npart_l; b++) {
	    FLOAT ebb,m;
	    m = ebb = fftenergy[j++];
	    for (i = gfc->numlines_l[b] - 1; i > 0; --i) {
		FLOAT8 el = fftenergy[j++];
		ebb += el;
		if (m < el)
		    m = el;
	    }
	    eb[b] = ebb;
	    max[b] = m;
	    avg[b] = ebb * gfc->rnumlines_l[b];
	}

	if (gfc->nsPsy.use2)
	    nsPsy2dataRead(gfc->nsPsy.pass1fp, eb2, eb, chn, gfc->npart_l);
	else {
	    FLOAT8 m,a;
	    a = avg[0] + avg[1];
	    if (a != 0.0) {
		m = max[0]; if (m < max[1]) m = max[1];
		a = 20.0 * (m*2.0-a)
		    / (a*(gfc->numlines_l[0] + gfc->numlines_l[1] - 1));
		k = (int) a;
		if (k > sizeof(tab)/sizeof(tab[0]) - 1)
		    k = sizeof(tab)/sizeof(tab[0]) - 1;
		a = eb[0] * tab[k];
	    }
	    eb2[0] = a;

	    for (b = 1; b < gfc->npart_l-1; b++) {
		a = avg[b-1] + avg[b] + avg[b+1];
		if (a != 0.0) {
		    m = max[b-1];
		    if (m < max[b  ]) m = max[b];
		    if (m < max[b+1]) m = max[b+1];
		    a = 20.0 * (m*3.0-a)
			/ (a*(gfc->numlines_l[b-1] + gfc->numlines_l[b] + gfc->numlines_l[b+1] - 1));
		    k = (int) a;
		    if (k > sizeof(tab)/sizeof(tab[0]) - 1)
			k = sizeof(tab)/sizeof(tab[0]) - 1;
		    a = eb[b] * tab[k];
		}
		eb2[b] = a;
	    }

	    a = avg[gfc->npart_l-2] + avg[gfc->npart_l-1];
	    if (a != 0.0) {
		m = max[gfc->npart_l-2];
		if (m < max[gfc->npart_l-1])
		    m = max[gfc->npart_l-1];

		a = 20.0 * (m*2.0-a)
		    / (a*(gfc->numlines_l[gfc->npart_l-2] + gfc->numlines_l[gfc->npart_l-1] - 1));
		k = (int) a;
		if (k > sizeof(tab)/sizeof(tab[0]) - 1)
		    k = sizeof(tab)/sizeof(tab[0]) - 1;
		a = eb[b] * tab[k];
	    }
	    eb2[b] = a;
	}

	/*********************************************************************
	 *      convolve the partitioned energy and unpredictability
	 *      with the spreading function, s3_l[b][k]
	 ******************************************************************* */
	k = 0;
	for ( b = 0;b < gfc->npart_l; b++ ) {
	    FLOAT8 ecb;
	    /* convolve the partitioned energy with the spreading function */
	    int kk = gfc->s3ind[b][0];
	    ecb = gfc->s3_ll[k++] * eb2[kk];
	    while (++kk <= gfc->s3ind[b][1])
		ecb = mask_add(ecb, gfc->s3_ll[k++] * eb2[kk], kk, kk-b, gfc);

	    ecb *= 0.158489319246111; // pow(10,-0.8)

	    /****   long block pre-echo control   ****/
	    /* dont use long block pre-echo control if previous granule was 
	     * a short block.  This is to avoid the situation:   
	     * frame0:  quiet (very low masking)  
	     * frame1:  surge  (triggers short blocks)
	     * frame2:  regular frame.  looks like pre-echo when compared to 
	     *          frame0, but all pre-echo was in frame1.
	     */
	    /* chn=0,1   L and R channels
	       chn=2,3   S and M channels.
	    */

	    if (gfc->blocktype_old[chn & 1] == SHORT_TYPE)
		thr[b] = ecb; /* Min(ecb, rpelev*gfc->nb_1[chn][b]); */
	    else
		thr[b] = NS_INTERP(Min(ecb,
				       Min(rpelev*gfc->nb_1[chn][b],
					   rpelev2*gfc->nb_2[chn][b])),
				   ecb, pcfact);

	    gfc->nb_2[chn][b] = gfc->nb_1[chn][b];
	    gfc->nb_1[chn][b] = ecb;
	}

	/*************************************************************** 
	 * determine the block type (window type)
	 ***************************************************************/
	/* calculate energies of each sub-shortblocks */
	for (i=0; i<3; i++) {
	    en_subshort[i] = gfc->nsPsy.last_en_subshort[chn][i+6];
	    attack_intensity[i]
		= en_subshort[i] / gfc->nsPsy.last_en_subshort[chn][i+4];
	}

	if (chn == 2) {
	    for(i=0;i<576;i++) {
		FLOAT l, r;
		l = ns_hpfsmpl[0][i];
		r = ns_hpfsmpl[1][i];
		ns_hpfsmpl[0][i] = l+r;
		ns_hpfsmpl[1][i] = l-r;
	    }
	}
	{
	    FLOAT *pf = ns_hpfsmpl[chn & 1];
	    for (i=0;i<9;i++) {
		double p = 0.0;
		FLOAT *pfe = pf + 576/9;
		for (; pf < pfe; pf++)
		    p += *pf * *pf;

		if (p < 100.0)
		    p = 100.0;
		gfc->nsPsy.last_en_subshort[chn][i] = en_subshort[i+3] = p;
		attack_intensity[i+3] = p / en_subshort[i+3-2];
	    }
	}

	/* compare energies between sub-shortblocks */
	attackThreshold = (chn == 3)
	    ? gfc->nsPsy.attackthre_s : gfc->nsPsy.attackthre;
	for (i=0;i<12;i++)
	    if (!ns_attacks[i/3] && attack_intensity[i] > attackThreshold)
		ns_attacks[i/3] = (i % 3)+1;

	if (ns_attacks[0] && gfc->nsPsy.last_attacks[chn][2])
	    ns_attacks[0] = 0;

	if (gfc->nsPsy.last_attacks[chn][2] == 3 ||
	    ns_attacks[0] || ns_attacks[1] || ns_attacks[2] || ns_attacks[3]) {
	    ns_uselongblock = 0;

	    if (ns_attacks[1] && ns_attacks[0]) ns_attacks[1] = 0;
	    if (ns_attacks[2] && ns_attacks[1]) ns_attacks[2] = 0;
	    if (ns_attacks[3] && ns_attacks[2]) ns_attacks[3] = 0;
	}
	uselongblock[chn] = ns_uselongblock;

	/* compute masking thresholds for long blocks */
	convert_partition2scalefac_l(gfc, eb, thr, chn);

	/* compute masking thresholds for short blocks */
	for (sblock = 0; sblock < 3; sblock++) {
	    FLOAT8 enn, thmm;
	    compute_masking_s(gfc, fftenergy_s, eb, thr, chn, sblock);
	    b = -1;
	    for (sb = 0; sb < SBMAX_s; sb++) {
		enn = thmm = 0.0;
		while (++b < gfc->bo_s[sb]) {
		    enn  += eb[b];
		    thmm += thr[b];
		}
		enn  += 0.5 * eb[b];
		thmm += 0.5 * thr[b];
		gfc->en [chn].s[sb][sblock] = enn;

		/****   short block pre-echo control   ****/
#define NS_PREECHO_ATT0 0.8
#define NS_PREECHO_ATT1 0.6
#define NS_PREECHO_ATT2 0.3

		thmm *= NS_PREECHO_ATT0;
		if (ns_attacks[sblock] >= 2 || ns_attacks[sblock+1] == 1) {
		    int idx = (sblock != 0) ? sblock-1 : 2;
		    double p = NS_INTERP(gfc->nsPsy.last_thm[chn][sb][idx],
					 thmm, NS_PREECHO_ATT1*pcfact);
		    thmm = Min(thmm,p);
		}

		if (ns_attacks[sblock] == 1) {
		    int idx = (sblock != 0) ? sblock-1 : 2;
		    double p = NS_INTERP(gfc->nsPsy.last_thm[chn][sb][idx],
					 thmm,NS_PREECHO_ATT2*pcfact);
		    thmm = Min(thmm,p);
		} else if ((sblock != 0 && ns_attacks[sblock-1] == 3)
			|| (sblock == 0 && gfc->nsPsy.last_attacks[chn][2] == 3)) {
		    int idx = (sblock != 2) ? sblock+1 : 0;
		    double p = NS_INTERP(gfc->nsPsy.last_thm[chn][sb][idx],
					 thmm,NS_PREECHO_ATT2*pcfact);
		    thmm = Min(thmm,p);
		}

		if (1) {
		    /* pulse like signal detection for fatboy.wav */
		    FLOAT8 avg = en_subshort[sblock*3+3] + en_subshort[sblock*3+4]
			+ en_subshort[sblock*3+5];
		    int pulsive =
			(en_subshort[sblock*3+3]*6 < avg)
			+ (en_subshort[sblock*3+4]*6 < avg)
			+ (en_subshort[sblock*3+5]*6 < avg);
		    
		    if (pulsive == 1)
			thmm *= 0.5;
		    else if (pulsive == 2)
			thmm *= 0.25;
		}

		gfc->thm[chn].s[sb][sblock] = thmm;

		/************************************************************ 
		 * save some values for analysis of the next granule
		 *************************************************************/
		gfc->nsPsy.last_thm[chn][sb][sblock]
		    = gfc->thm[chn].s[sb][sblock];
	    }
	    gfc->nsPsy.last_attacks[chn][sblock] = ns_attacks[sblock];
	}
    } /* end loop over chn */

    if (gfp->interChRatio != 0.0)
	calc_interchannel_masking(gfp, gfp->interChRatio);

    if (gfp->mode == JOINT_STEREO) {
	FLOAT8 msfix;
	msfix1(gfc);
	msfix = gfp->msfix;
	if (gfc->ATH->adjust >= gfc->presetTune.athadjust_switch_level)
	    msfix = gfc->nsPsy.athadjust_msfix;

	if (msfix != 0.0)
	    ns_msfix(gfc, msfix, gfp->ATHlower);
    }

    /*************************************************************** 
     * determine final block type
     ***************************************************************/
    block_type_set(gfp, uselongblock, blocktype_d, blocktype);

    /*********************************************************************
     * compute the value of PE to return (one granule delay)
     *********************************************************************/
    for(chn=0;chn<numchn;chn++) {
	FLOAT8 *ppe;
	int type;
	III_psy_ratio *mr;

	if (chn > 1) {
	    ppe = percep_MS_entropy - 2;
	    type = NORM_TYPE;
	    if (blocktype_d[0] == SHORT_TYPE || blocktype_d[1] == SHORT_TYPE)
		type = SHORT_TYPE;
	    mr = &masking_MS_ratio[gr_out][chn-2];
	} else {
	    ppe = percep_entropy;
	    type = blocktype_d[chn];
	    mr = &masking_ratio[gr_out][chn];
	}

	if (type == SHORT_TYPE)
	    ppe[chn] = pecalc_s(mr, gfc->masking_lower);
	else
	    ppe[chn] = pecalc_l(mr, gfc->masking_lower);

#if defined(HAVE_GTK)
	if (gfp->analysis) gfc->pinfo->pe[gr_out][chn] = ppe[chn];
#endif
    }
    return 0;
}





/* 
 *   The spreading function.  Values returned in units of energy
 */
static FLOAT8 s3_func(FLOAT8 bark) {
    FLOAT8 tempx,x,tempy,temp;
    tempx = bark;
    if (tempx>=0) tempx *= 3;
    else tempx *=1.5; 

    if (tempx>=0.5 && tempx<=2.5)
      {
	temp = tempx - 0.5;
	x = 8.0 * (temp*temp - 2.0 * temp);
      }
    else x = 0.0;
    tempx += 0.474;
    tempy = 15.811389 + 7.5*tempx - 17.5*sqrt(1.0+tempx*tempx);

    if (tempy <= -60.0) return  0.0;

    tempx = exp( (x + tempy)*LN_TO_LOG10 ); 

    /* Normalization.  The spreading function should be normalized so that:
         +inf
           /
           |  s3 [ bark ]  d(bark)   =  1
           /
         -inf
    */
    tempx /= .6609193;
    return tempx;
}

static int
init_numline(
    int *numlines, int *bo, int *bm,
    FLOAT8 *bval, FLOAT8 *bval_width, FLOAT8 *mld,

    FLOAT8 sfreq, int blksize, int *scalepos,
    FLOAT8 deltafreq, int sbmax
    )
{
    int partition[HBLKSIZE];
    int i, j, k;
    int sfb;

    sfreq /= blksize;
    j = 0;
    /* compute numlines, the number of spectral lines in each partition band */
    /* each partition band should be about DELBARK wide. */
    for (i=0;i<CBANDS;i++) {
	FLOAT8 bark1;
	int j2;
	bark1 = freq2bark(sfreq*j);
	for (j2 = j; freq2bark(sfreq*j2) - bark1 < DELBARK && j2 <= blksize/2;
	     j2++)
	    ;

	numlines[i] = j2 - j;
	while (j<j2)
	    partition[j++]=i;
	if (j > blksize/2) break;
    }

    for ( sfb = 0; sfb < sbmax; sfb++ ) {
	int i1,i2,start,end;
	FLOAT8 arg;
	start = scalepos[sfb];
	end   = scalepos[sfb+1];

	i1 = floor(.5 + deltafreq*(start-.5));
	if (i1<0) i1=0;
	i2 = floor(.5 + deltafreq*(end-.5));
	if (i2>blksize/2) i2=blksize/2;

	bm[sfb] = (partition[i1]+partition[i2])/2;
	bo[sfb] = partition[i2];

	/* setup stereo demasking thresholds */
	/* formula reverse enginerred from plot in paper */
	arg = freq2bark(sfreq*scalepos[sfb]*deltafreq);
	arg = (Min(arg, 15.5)/15.5);

	mld[sfb] = pow(10.0, 1.25*(1-cos(PI*arg))-2.5);
    }

    /* compute bark values of each critical band */
    j = 0;
    for (k = 0; k < i+1; k++) {
	int w = numlines[k];
	FLOAT8  bark1,bark2;

	bark1 = freq2bark (sfreq*(j    ));
	bark2 = freq2bark (sfreq*(j+w-1));
	bval[k] = .5*(bark1+bark2);

	bark1 = freq2bark (sfreq*(j  -.5));
	bark2 = freq2bark (sfreq*(j+w-.5));
	bval_width[k] = bark2-bark1;
	j += w;
    }

    return i+1;
}

static int
init_s3_values(
    lame_internal_flags *gfc,
    FLOAT8 **p,
    int (*s3ind)[2],
    int npart,
    FLOAT8 *bval,
    FLOAT8 *bval_width,
    FLOAT8 *norm
    )
{
    FLOAT8 s3[CBANDS][CBANDS];
    int i, j, k;
    int numberOfNoneZero = 0;

    /* s[j][i], the value of the spreading function,
     * centered at band j, for band i
     *
     * i.e.: sum over j to spread into signal barkval=i
     * NOTE: i and j are used opposite as in the ISO docs
     */
    for (i = 0; i < npart; i++)
	for (j = 0; j < npart; j++)
	    s3[i][j] = s3_func(bval[i] - bval[j]) * bval_width[j] * norm[i];

    for (i = 0; i < npart; i++) {
	for (j = 0; j < npart; j++) {
	    if (s3[i][j] != 0.0)
		break;
	}
	s3ind[i][0] = j;

	for (j = npart - 1; j > 0; j--) {
	    if (s3[i][j] != 0.0)
		break;
	}
	s3ind[i][1] = j;
	numberOfNoneZero += (s3ind[i][1] - s3ind[i][0] + 1);
    }
    *p = malloc(sizeof(FLOAT8)*numberOfNoneZero);
    if (!*p)
	return -1;

    k = 0;
    for (i = 0; i < npart; i++)
	for (j = s3ind[i][0]; j <= s3ind[i][1]; j++)
	    (*p)[k++] = s3[i][j];

    return 0;
}

int psymodel_init(lame_global_flags *gfp)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int i,j,b,sb,k;

    FLOAT8 bval[CBANDS];
    FLOAT8 bval_width[CBANDS];
    FLOAT8 norm[CBANDS];
    FLOAT8 sfreq = gfp->out_samplerate;

    gfc->ms_ener_ratio_old=.25;
    gfc->blocktype_old[0] = gfc->blocktype_old[1] = SHORT_TYPE;

    for (i=0; i<4; ++i) {
	for (j=0; j<CBANDS; ++j) {
	    gfc->nb_1[i][j]=1e20;
	    gfc->nb_2[i][j]=1e20;
	    gfc->nb_s1[i][j] = gfc->nb_s2[i][j] = 1.0;
	}
	for ( sb = 0; sb < SBMAX_l; sb++ ) {
	    gfc->en[i].l[sb] = 1e20;
	    gfc->thm[i].l[sb] = 1e20;
	}
	for (j=0; j<3; ++j) {
	    for ( sb = 0; sb < SBMAX_s; sb++ ) {
		gfc->en[i].s[sb][j] = 1e20;
		gfc->thm[i].s[sb][j] = 1e20;
		gfc->nsPsy.last_thm[i][sb][j] = 1e20;
	    }
	    gfc->nsPsy.last_attacks[i][j] = 0;
	}
	for(j=0;j<9;j++)
	    gfc->nsPsy.last_en_subshort[i][j] = 100;
    }



    j = gfc->PSY->cwlimit/(sfreq/BLKSIZE);
    if (j > HBLKSIZE-4) /* j+3 < HBLKSIZE-1 */
	j = HBLKSIZE-4;
    if (j < CW_LOWER_INDEX)
	j = CW_LOWER_INDEX;
    gfc->cw_upper_index = j;

    for (j = 0; j < HBLKSIZE; j++)
	gfc->cw[j] = 0.4f;

    /* init. for loudness approx. -jd 2001 mar 27*/
    gfc->loudness_sq_save[0] = gfc->loudness_sq_save[1] = 0.0;




    /*************************************************************************
     * now compute the psychoacoustic model specific constants
     ************************************************************************/
    /* compute numlines, bo, bm, bval, bval_width, mld */
    gfc->npart_l
	= init_numline(gfc->numlines_l, gfc->bo_l, gfc->bm_l,
		       bval, bval_width, gfc->mld_l,
		       sfreq, BLKSIZE, 
		       gfc->scalefac_band.l, BLKSIZE/(2.0*576), SBMAX_l);
    assert(gfc->npart_l <= CBANDS);
    /* compute the spreading function */
    for(i=0;i<gfc->npart_l;i++) {
	norm[i]=1.0;
	gfc->rnumlines_l[i] = 1.0 / gfc->numlines_l[i];
    }
    i = init_s3_values(gfc, &gfc->s3_ll, gfc->s3ind,
		       gfc->npart_l, bval, bval_width, norm);
    if (i)
	return i;

    /* compute long block specific values, ATH and MINVAL */
    j = 0;
    for ( i = 0; i < gfc->npart_l; i++ ) {
	double x;

	/* ATH */
	x = FLOAT_MAX;
	for (k=0; k < gfc->numlines_l[i]; k++, j++) {
	    FLOAT8  freq = sfreq*j/(1000.0*BLKSIZE);
	    FLOAT8  level;
	    assert( freq <= 24 );              // or only '<'
	    //	freq = Min(.1,freq);       // ATH below 100 Hz constant, not further climbing
	    level  = ATHformula (freq*1000, gfp) - 20;   // scale to FFT units; returned value is in dB
	    level  = pow ( 10., 0.1*level );   // convert from dB -> energy
	    level *= gfc->numlines_l [i];
	    if (x > level)
		x = level;
	}
	gfc->ATH->cb[i] = x;

	/* MINVAL.
	   For low freq, the strength of the masking is limited by minval
	   this is an ISO MPEG1 thing, dont know if it is really needed */
	x = (-20+bval[i]*20.0/10.0);
	if (bval[i]>10) x = 0;
	gfc->minval[i]=pow(10.0,x/10);
	gfc->PSY->prvTonRed[i] = gfc->minval[i];
    }


    /************************************************************************
     * do the same things for short blocks
     ************************************************************************/
    gfc->npart_s
	= init_numline(gfc->numlines_s, gfc->bo_s, gfc->bm_s,
		       bval, bval_width, gfc->mld_s,
		       sfreq, BLKSIZE_s,
		       gfc->scalefac_band.s, BLKSIZE_s/(2.0*192), SBMAX_s);
    assert(gfc->npart_s <= CBANDS);

    /* SNR formula. short block is normalized by SNR. is it still right ? */
    for(i=0;i<gfc->npart_s;i++) {
	double snr=-8.25;
	if (bval[i]>=13)
	    snr = -4.5 * (bval[i]-13)/(24.0-13.0)
		-8.25*(bval[i]-24)/(13.0-24.0);

	norm[i]=pow(10.0,snr/10.0);
    }
    i = init_s3_values(gfc, &gfc->s3_ss, gfc->s3ind_s,
		       gfc->npart_s, bval, bval_width, norm);
    if (i)
	return i;


    init_mask_add_max_values();
    init_fft(gfc);

    /* setup temporal masking */
#define temporalmask_sustain_sec 0.01
    gfc->decay = exp(-1.0*LOG10/(temporalmask_sustain_sec*sfreq/192.0));

    if (gfc->nsPsy.use) {
	FLOAT8 msfix;

#define NS_MSFIX 3.5
#define NSATTACKTHRE 150
#define NSATTACKTHRE_S 300

	msfix = NS_MSFIX;
	if (gfp->exp_nspsytune & 2) msfix = 1.0;
	if (gfp->msfix != 0.0) msfix = gfp->msfix;
	gfp->msfix = msfix;
	if (!gfc->presetTune.use || gfc->nsPsy.athadjust_msfix <= 0.0)
	    gfc->nsPsy.athadjust_msfix = gfp->msfix;

	if (!gfc->presetTune.use) {
	    gfc->nsPsy.attackthre   = NSATTACKTHRE;
	    gfc->nsPsy.attackthre_s = NSATTACKTHRE_S;
	}

	/* spread only from npart_l bands.  Normally, we use the spreading
	 * function to convolve from npart_l down to npart_l bands 
	 */
	for (b=0;b<gfc->npart_l;b++)
	    if (gfc->s3ind[b][1] > gfc->npart_l-1)
		gfc->s3ind[b][1] = gfc->npart_l-1;
    }

    /*  prepare for ATH auto adjustment:
     *  we want to decrease the ATH by 12 dB per second
     */
    {
        FLOAT8 frame_duration = 576. * gfc->mode_gr / sfreq;
        gfc->ATH->decay = pow(10., -12./10. * frame_duration);
        gfc->ATH->adjust = 0.01; /* minimum, for leading low loudness */
        gfc->ATH->adjust_limit = 1.0; /* on lead, allow adjust up to maximum */
    }

    gfc->bo_s[SBMAX_s-1]--;
    assert(gfc->bo_l[SBMAX_l-1] <= gfc->npart_l);
    assert(gfc->bo_s[SBMAX_s-1] <= gfc->npart_s);
    return 0;
}
