/*
 *	LAME MP3 encoding engine
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


#include <assert.h>

#ifdef HAVEGTK
#include "gtkanal.h"
#include <gtk/gtk.h>
#endif
#include "lame.h"
#include "util.h"
#include "timestatus.h"
#include "globalflags.h"
#include "psymodel.h"
#include "newmdct.h"
#include "quantize.h"
#include "quantize-pvt.h"
#include "l3bitstream.h"
#include "formatBitstream.h"
#include "version.h"
#include "VbrTag.h"
#include "id3tag.h"
#include "tables.h"
#include "brhist.h"
#include "get_audio.h"

#ifdef __riscos__
#include "asmstuff.h"
#endif

/* Global flags substantiated here.  defined extern in globalflags.h */
/* default values set in lame_init() */
lame_global_flags gf;


/* Global variable definitions for lame.c */
static Bit_stream_struc   bs;
static III_side_info_t l3_side;
static frame_params fr_ps;
static int target_bitrate;
#define MFSIZE (1152+1152+ENCDELAY-MDCTDELAY)
static short int mfbuf[2][MFSIZE];
static int mf_size;
static int mf_samples_to_encode;



/********************************************************************
 *   initialize internal params based on data in gf
 *   (globalflags struct filled in by calling program)
 *
 ********************************************************************/
void lame_init_params(void)
{
  int i;
  FLOAT compression_ratio;

  /* Clear info structure */
  memset(&bs, 0, sizeof(Bit_stream_struc));
  memset(&fr_ps, 0, sizeof(frame_params));
  memset(&l3_side,0x00,sizeof(III_side_info_t));

  fr_ps.tab_num = -1;             /* no table loaded */
  fr_ps.alloc = NULL;

  gf.frameNum=0;
  gf.force_ms=0;
  InitFormatBitStream();
  if (gf.num_channels==1) {
    gf.mode = MPG_MD_MONO;
  }
  gf.stereo=2;
  if (gf.mode == MPG_MD_MONO) gf.stereo=1;

#ifdef BRHIST
  if (gf.silent) {
    disp_brhist=0;  /* turn of VBR historgram */
  }
  if (!gf.VBR) {
    disp_brhist=0;  /* turn of VBR historgram */
  }
#endif

  /* set the output sampling rate, and resample options if necessary
     samplerate = input sample rate
     resamplerate = ouput sample rate
  */
  if (gf.out_samplerate==0) {
    /* user did not specify output sample rate */
    gf.out_samplerate=gf.in_samplerate;   /* default */


    /* if resamplerate is not valid, find a valid value */
    if (gf.out_samplerate>=48000) gf.out_samplerate=48000;
    else if (gf.out_samplerate>=44100) gf.out_samplerate=44100;
    else if (gf.out_samplerate>=32000) gf.out_samplerate=32000;
    else if (gf.out_samplerate>=24000) gf.out_samplerate=24000;
    else if (gf.out_samplerate>=22050) gf.out_samplerate=22050;
    else gf.out_samplerate=16000;


    if (gf.brate>0) {
      /* check if user specified bitrate requires downsampling */
      compression_ratio = gf.out_samplerate*16*gf.stereo/(1000.0*gf.brate);
      if (!gf.VBR && compression_ratio > 13 ) {
	/* automatic downsample, if possible */
	gf.out_samplerate = (10*1000.0*gf.brate)/(16*gf.stereo);
	if (gf.out_samplerate<=16000) gf.out_samplerate=16000;
	else if (gf.out_samplerate<=22050) gf.out_samplerate=22050;
	else if (gf.out_samplerate<=24000) gf.out_samplerate=24000;
	else if (gf.out_samplerate<=32000) gf.out_samplerate=32000;
	else if (gf.out_samplerate<=44100) gf.out_samplerate=44100;
	else gf.out_samplerate=48000;
      }
    }
  }

  gf.mode_gr = (gf.out_samplerate <= 24000) ? 1 : 2;  /* mode_gr = 2 */
  gf.encoder_delay = ENCDELAY;
  gf.framesize = gf.mode_gr*576;

  if (gf.brate==0) { /* user didn't specify a bitrate, use default */
    gf.brate=128;
    if (gf.mode_gr==1) gf.brate=64;
  }


  gf.resample_ratio=1;
  if (gf.out_samplerate != gf.in_samplerate) gf.resample_ratio = (FLOAT)gf.in_samplerate/(FLOAT)gf.out_samplerate;

  /* estimate total frames.  must be done after setting sampling rate so
   * we know the framesize.  */
  gf.totalframes=0;
  gf.totalframes = 2+ gf.num_samples/(gf.resample_ratio*gf.framesize);



  /* 44.1kHz at 56kbs/channel: compression factor of 12.6
     44.1kHz at 64kbs/channel: compression factor of 11.025
     44.1kHz at 80kbs/channel: compression factor of 8.82
     22.05kHz at 24kbs:  14.7
     22.05kHz at 32kbs:  11.025
     22.05kHz at 40kbs:  8.82
     16kHz at 16kbs:  16.0
     16kHz at 24kbs:  10.7

     compression_ratio
        11                                .70?
        12                   sox resample .66
        14.7                 sox resample .45

  */
  if (gf.brate >= 320) gf.VBR=0;  /* dont bother with VBR at 320kbs */
  compression_ratio = gf.out_samplerate*16*gf.stereo/(1000.0*gf.brate);


  /* for VBR, take a guess at the compression_ratio */
  /* VBR_q           compression       like
     0                4.4             320kbs
     1                5.4             256kbs
     3                7.4             192kbs
     4                8.8             160kbs
     6                10.4            128kbs
  */
  if (gf.VBR && compression_ratio>11) {
    compression_ratio = 4.4 + gf.VBR_q;
  }


  /* At higher quality (lower compression) use STEREO instead of JSTEREO.
   * (unless the user explicitly specified a mode ) */
  if ( (!gf.mode_fixed) && (gf.mode !=MPG_MD_MONO)) {
    if (compression_ratio < 9 ) {
      gf.mode = MPG_MD_STEREO;
    }
  }



  /****************************************************************/
  /* if a filter has not been enabled, see if we should add one: */
  /****************************************************************/
  if (gf.lowpassfreq == 0) {
    /* If the user has not selected their own filter, add a lowpass
     * filter based on the compression ratio.  Formula based on
          44.1   /160    4.4x
          44.1   /128    5.5x      keep all bands
          44.1   /96kbs  7.3x      keep band 28
          44.1   /80kbs  8.8x      keep band 25
          44.1khz/64kbs  11x       keep band 21  22?

	  16khz/24kbs  10.7x       keep band 21
	  22kHz/32kbs  11x         keep band ?
	  22kHz/24kbs  14.7x       keep band 16
          16    16     16x         keep band 14
    */


    /* Should we use some lowpass filters? */
    int band = 1+floor(.5 + 14-18*log(compression_ratio/16.0));
    if (band < 31) {
      gf.lowpass1 = band/31.0;
      gf.lowpass2 = band/31.0;
    }
  }




  /****************************************************************/
  /* apply user driven filters*/
  /****************************************************************/
  if ( gf.highpassfreq > 0 ) {
    gf.highpass1 = 2.0*gf.highpassfreq/gf.out_samplerate; /* will always be >=0 */
    if ( gf.highpasswidth >= 0 ) {
      gf.highpass2 = 2.0*(gf.highpassfreq+gf.highpasswidth)/gf.out_samplerate;
    } else {
      /* 15% above on default */
      /* gf.highpass2 = 1.15*2.0*gf.highpassfreq/gf.out_samplerate;  */
      gf.highpass2 = 1.00*2.0*gf.highpassfreq/gf.out_samplerate; 
    }
    gf.highpass1 = Min( 1, gf.highpass1 );
    gf.highpass2 = Min( 1, gf.highpass2 );
  }

  if ( gf.lowpassfreq > 0 ) {
    gf.lowpass2 = 2.0*gf.lowpassfreq/gf.out_samplerate; /* will always be >=0 */
    if ( gf.lowpasswidth >= 0 ) {
      gf.lowpass1 = 2.0*(gf.lowpassfreq-gf.lowpasswidth)/gf.out_samplerate;
      if ( gf.lowpass1 < 0 ) { /* has to be >= 0 */
	gf.lowpass1 = 0;
      }
    } else {
      /* 15% below on default */
      /* gf.lowpass1 = 0.85*2.0*gf.lowpassfreq/gf.out_samplerate;  */
      gf.lowpass1 = 1.00*2.0*gf.lowpassfreq/gf.out_samplerate;
    }
    gf.lowpass1 = Min( 1, gf.lowpass1 );
    gf.lowpass2 = Min( 1, gf.lowpass2 );
  }


  /***************************************************************/
  /* compute info needed for polyphase filter                    */
  /***************************************************************/
  if (gf.filter_type==0) {
    int band,maxband,minband;
    FLOAT8 amp,freq;
    if (gf.lowpass1 > 0) {
      minband=999;
      maxband=-1;
      for (band=0;  band <=31 ; ++band) { 
	freq = band/31.0;
	amp = 1;
	/* this band and above will be zeroed: */
	if (freq >= gf.lowpass2) {
	  gf.lowpass_band= Min(gf.lowpass_band,band);
	  amp=0;
	}
	if (gf.lowpass1 < freq && freq < gf.lowpass2) {
          minband = Min(minband,band);
          maxband = Max(maxband,band);
	  amp = cos((PI/2)*(gf.lowpass1-freq)/(gf.lowpass2-gf.lowpass1));
	}
	/* printf("lowpass band=%i  amp=%f \n",band,amp);*/
      }
      /* compute the *actual* passband implemented by the polyphase filter */
      if (minband==999) gf.lowpass1 = (gf.lowpass_band-.75)/31.0;
      else gf.lowpass1 = (minband-.75)/31.0;
      gf.lowpass2 = gf.lowpass_band/31.0;
    }

    /* make sure highpass filter is within 90% of whan the effective highpass
     * frequency will be */
    if (gf.highpass2 > 0) 
      if (gf.highpass2 <  .9*(.75/31.0) ) {
	gf.highpass1=0; gf.highpass2=0;
	fprintf(stderr,"Warning: highpass filter disabled.  highpass frequency to small\n");
      }
    

    if (gf.highpass2 > 0) {
      minband=999;
      maxband=-1;
      for (band=0;  band <=31; ++band) { 
	freq = band/31.0;
	amp = 1;
	/* this band and below will be zereod */
	if (freq <= gf.highpass1) {
	  gf.highpass_band = Max(gf.highpass_band,band);
	  amp=0;
	}
	if (gf.highpass1 < freq && freq < gf.highpass2) {
          minband = Min(minband,band);
          maxband = Max(maxband,band);
	  amp = cos((PI/2)*(gf.highpass2-freq)/(gf.highpass2-gf.highpass1));
	}
	/*	printf("highpass band=%i  amp=%f \n",band,amp);*/
      }
      /* compute the *actual* passband implemented by the polyphase filter */
      gf.highpass1 = gf.highpass_band/31.0;
      if (maxband==-1) gf.highpass2 = (gf.highpass_band+.75)/31.0;
      else gf.highpass2 = (maxband+.75)/31.0;
    }
    /*
    printf("lowpass band with amp=0:  %i \n",gf.lowpass_band);
    printf("highpass band with amp=0:  %i \n",gf.highpass_band);
    */
  }



  /***************************************************************/
  /* compute info needed for FIR filter */
  /***************************************************************/
  if (gf.filter_type==1) {
  }




  gf.mode_ext=MPG_MD_LR_LR;
  fr_ps.actual_mode = gf.mode;
  gf.stereo = (gf.mode == MPG_MD_MONO) ? 1 : 2;


  gf.samplerate_index = SmpFrqIndex((long)gf.out_samplerate, &gf.version);
  if( gf.samplerate_index < 0) {
    display_bitrates(stderr);
    exit(1);
  }
  if( (gf.bitrate_index = BitrateIndex(gf.brate, gf.version,gf.out_samplerate)) < 0) {
    display_bitrates(stderr);
    exit(1);
  }


  /* choose a min/max bitrate for VBR */
  if (gf.VBR) {
    /* if the user didn't specify VBR_max_bitrate: */
    if (0==gf.VBR_max_bitrate_kbps) {
      /* default max bitrate is 256kbs */
      /* we do not normally allow 320bps frams with VBR, unless: */
      gf.VBR_max_bitrate=13;   /* default: allow 256kbs */
      if (gf.VBR_min_bitrate_kbps>=256) gf.VBR_max_bitrate=14;
      if (gf.VBR_q == 0) gf.VBR_max_bitrate=14;   /* allow 320kbs */
      if (gf.VBR_q >= 4) gf.VBR_max_bitrate=12;   /* max = 224kbs */
      if (gf.VBR_q >= 8) gf.VBR_max_bitrate=9;    /* low quality, max = 128kbs */
    }else{
      if( (gf.VBR_max_bitrate  = BitrateIndex(gf.VBR_max_bitrate_kbps, gf.version,gf.out_samplerate)) < 0) {
	display_bitrates(stderr);
	exit(1);
      }
    }
    if (0==gf.VBR_min_bitrate_kbps) {
      gf.VBR_min_bitrate=1;  /* 32 kbps */
    }else{
      if( (gf.VBR_min_bitrate  = BitrateIndex(gf.VBR_min_bitrate_kbps, gf.version,gf.out_samplerate)) < 0) {
	display_bitrates(stderr);
	exit(1);
      }
    }

  }


  if (gf.VBR) gf.quality=Min(gf.quality,2);    /* always use quality <=2  with VBR */
  /* dont allow forced mid/side stereo for mono output */
  if (gf.mode == MPG_MD_MONO) gf.force_ms=0;


  /* Do not write VBR tag if VBR flag is not specified */
  if (gf.VBR==0) gf.bWriteVbrTag=0;

  /* some file options not allowed if output is: not specified or stdout */
  if (gf.outPath==NULL || gf.outPath[0]=='-' ) {
    gf.bWriteVbrTag=0; /* turn off VBR tag */
    id3tag.used=0;         /* turn of id3 tagging */
  }



  if (gf.gtkflag) {
    gf.bWriteVbrTag=0;  /* disable Xing VBR tag */
  }

  if (gf.mode_gr==1) {
    gf.bWriteVbrTag=0;      /* no MPEG2 Xing VBR tags yet */
  }

  init_bit_stream_w(&bs);


#ifdef BRHIST
  if (gf.VBR) {
    if (disp_brhist)
      brhist_init(1, 14);
  } else
    disp_brhist = 0;
#endif

  /* set internal feature flags.  USER should not access these since
   * some combinations will produce strange results */
  if (gf.quality>=9) {
    /* 9 = worst quality */
    gf.filter_type=0;
    gf.quantization=0;
    gf.psymodel=0;
    gf.noise_shaping=0;
    gf.noise_shaping_stop=0;
    gf.ms_masking=0;
    gf.use_best_huffman=0;
  } else if (gf.quality>=5) {
    /* 5..8 quality, the default  */
    gf.filter_type=0;
    gf.quantization=0;
    gf.psymodel=1;
    gf.noise_shaping=1;
    gf.noise_shaping_stop=0;
    gf.ms_masking=0;
    gf.use_best_huffman=0;
  } else if (gf.quality>=2) {
    /* 2..4 quality */
    gf.filter_type=0;
    gf.quantization=1;
    gf.psymodel=1;
    gf.noise_shaping=1;
    gf.noise_shaping_stop=0;
    gf.ms_masking=1;
    gf.use_best_huffman=1;
  } else {
    /* 0..1 quality */
    gf.filter_type=1;         /* not yet coded */
    gf.quantization=1;
    gf.psymodel=1;
    gf.noise_shaping=3;       /* not yet coded */
    gf.noise_shaping_stop=2;  /* not yet coded */
    gf.ms_masking=1;
    gf.use_best_huffman=2;   /* not yet coded */
    exit(-99);
  }

  gf.filter_type=0;

  /* best_quant algorithms not based on over=0 require this: */
  if (gf.experimentalX) gf.noise_shaping_stop=1;


  for (i = 0; i < SBMAX_l + 1; i++) {
    scalefac_band.l[i] =
      sfBandIndex[gf.samplerate_index + (gf.version * 3)].l[i];
  }
  for (i = 0; i < SBMAX_s + 1; i++) {
    scalefac_band.s[i] =
      sfBandIndex[gf.samplerate_index + (gf.version * 3)].s[i];
  }



  if (gf.bWriteVbrTag)
    {
      /* Write initial VBR Header to bitstream */
      InitVbrTag(&bs,gf.version-1,gf.mode,gf.samplerate_index);
    }

  return;
}









/************************************************************************
 *
 * print_config
 *
 * PURPOSE:  Prints the encoding parameters used
 *
 ************************************************************************/
void lame_print_config(void)
{
  char *mode_names[4] = { "stereo", "j-stereo", "dual-ch", "single-ch" };
  FLOAT out_samplerate=gf.out_samplerate/1000.0;
  FLOAT in_samplerate = gf.resample_ratio*out_samplerate;
  FLOAT compression=
    (FLOAT)(gf.stereo*16*out_samplerate)/(FLOAT)(gf.brate);

  lame_print_version(stderr);
  if (gf.num_channels==2 && gf.stereo==1) {
    fprintf(stderr, "Autoconverting from stereo to mono. Setting encoding to mono mode.\n");
  }
  if (gf.resample_ratio!=1) {
    fprintf(stderr,"Resampling:  input=%ikHz  output=%ikHz\n",
	    (int)in_samplerate,(int)out_samplerate);
  }
  if (gf.highpass2>0.0)
    fprintf(stderr, "Using polyphase highpass filter, passband: %.0f Hz -  %.0f Hz\n",
	    gf.highpass1*out_samplerate*500,
	    gf.highpass2*out_samplerate*500);
  if (gf.lowpass1>0.0)
    fprintf(stderr, "Using polyphase lowpass filter,  passband:  %.0f Hz - %.0f Hz\n",
	    gf.lowpass1*out_samplerate*500,
	    gf.lowpass2*out_samplerate*500);

  if (gf.gtkflag) {
    fprintf(stderr, "Analyzing %s \n",gf.inPath);
  }
  else {
    fprintf(stderr, "Encoding %s to %s\n",
	    (strcmp(gf.inPath, "-")? gf.inPath : "stdin"),
	    (strcmp(gf.outPath, "-")? gf.outPath : "stdout"));
    if (gf.VBR)
      fprintf(stderr, "Encoding as %.1fkHz VBR(q=%i) %s MPEG%i LayerIII  qual=%i\n",
	      gf.out_samplerate/1000.0,
	      gf.VBR_q,mode_names[gf.mode],2-gf.version,gf.quality);
    else
      fprintf(stderr, "Encoding as %.1f kHz %d kbps %s MPEG%i LayerIII (%4.1fx)  qual=%i\n",
	      gf.out_samplerate/1000.0,gf.brate,
	      mode_names[gf.mode],2-gf.version,compression,gf.quality);
  }
  fflush(stderr);
}












/************************************************************************
*
* encodeframe()           Layer 3
*
* encode a single frame
*
************************************************************************
lame_encode_frame(inbuf,mpg123bs)


                       gr 0            gr 1
inbuf:           |--------------|---------------|-------------|
MDCT output:  |--------------|---------------|-------------|

FFT's                    <---------1024---------->
                                         <---------1024-------->



    inbuf = buffer of PCM data size=MP3 framesize
    encoder acts on inbuf[ch][0], but output is delayed by MDCTDELAY
    so the MDCT coefficints are from inbuf[ch][-MDCTDELAY]

    psy-model FFT has a 1 granule day, so we feed it data for the next granule.
    FFT is centered over granule:  224+576+224
    So FFT starts at:   576-224-MDCTDELAY

    MPEG2:  FFT ends at:  BLKSIZE+576-224-MDCTDELAY
    MPEG1:  FFT ends at:  BLKSIZE+2*576-224-MDCTDELAY    (1904)

    FFT starts at 576-224-MDCTDELAY (304)  = 576-FFTOFFSET

*/
int lame_encode_frame(short int inbuf_l[],short int inbuf_r[],int mf_size,char *mpg123bs)
{
  static unsigned long frameBits;
  static unsigned long bitsPerSlot;
  static FLOAT8 frac_SpF;
  static FLOAT8 slot_lag;
  static unsigned long sentBits = 0;
  FLOAT8 xr[2][2][576];
  int l3_enc[2][2][576];
  int mpg123count;
  III_psy_ratio masking_ratio[2][2];    /*LR ratios */
  III_psy_ratio masking_MS_ratio[2][2]; /*MS ratios */
  III_psy_ratio (*masking)[2][2];  /*LR ratios and MS ratios*/
  III_scalefac_t scalefac[2][2];
  short int *inbuf[2];

  typedef FLOAT8 pedata[2][2];
  pedata pe,pe_MS;
  pedata *pe_use;

  int ch,gr,mean_bits;
  int bitsPerFrame;

  int check_ms_stereo;
  static FLOAT8 ms_ratio[2]={0,0};
  FLOAT8 ms_ratio_next=0;
  FLOAT8 ms_ratio_prev=0;
  FLOAT8 ms_ener_ratio[2]={0,0};

  memset((char *) masking_ratio, 0, sizeof(masking_ratio));
  memset((char *) masking_MS_ratio, 0, sizeof(masking_MS_ratio));
  memset((char *) scalefac, 0, sizeof(scalefac));
  inbuf[0]=inbuf_l;
  inbuf[1]=inbuf_r;

  gf.mode_ext = MPG_MD_LR_LR;

  if (gf.frameNum==0 )  {
    /* Figure average number of 'slots' per frame. */
    FLOAT8 avg_slots_per_frame;
    FLOAT8 sampfreq =   gf.out_samplerate/1000.0;
    int bit_rate = gf.brate;
    sentBits = 0;
    bitsPerSlot = 8;
    avg_slots_per_frame = (bit_rate*gf.framesize) /
           (sampfreq* bitsPerSlot);
    frac_SpF  = avg_slots_per_frame - (int) avg_slots_per_frame;
    slot_lag  = -frac_SpF;
    gf.padding = 1;
    if (fabs(frac_SpF) < 1e-9) gf.padding = 0;

    /* check FFT will not use a negative starting offset */
    assert(576>=FFTOFFSET);
    /* check if we have enough data for FFT */
    assert(mf_size>=(BLKSIZE+gf.framesize-FFTOFFSET));
  }


  /* use m/s gf.stereo? */
  check_ms_stereo =   ((gf.mode == MPG_MD_JOINT_STEREO) &&
		       (gf.version == 1) &&
		       (gf.stereo==2) );



  /********************** padding *****************************/
  switch (gf.padding_type) {
  case 0:
    gf.padding=0;
    break;
  case 1:
    gf.padding=1;
    break;
  case 2:
  default:
    if (gf.VBR) {
      gf.padding=0;
    } else {
      if (gf.disable_reservoir) {
	gf.padding = 0;
	/* if the user specified --nores, dont very gf.padding either */
	/* tiny changes in frac_SpF rounding will cause file differences */
      }else{
	if (frac_SpF != 0) {
	  if (slot_lag > (frac_SpF-1.0) ) {
	    slot_lag -= frac_SpF;
	    gf.padding = 0;
	  }
	  else {
	    gf.padding = 1;
	    slot_lag += (1-frac_SpF);
	  }
	}
      }
    }
  }


  /********************** status display  *****************************/
  if (!gf.gtkflag && !gf.silent) {
    int mod = gf.version == 0 ? 200 : 50;
    if (gf.frameNum%mod==0) {
      timestatus(gf.out_samplerate,gf.frameNum,gf.totalframes,gf.framesize);
#ifdef BRHIST
      if (disp_brhist)
	{
	  brhist_add_count();
	  brhist_disp();
	}
#endif
    }
  }



  if (gf.psymodel) {
    /* psychoacoustic model
     * psy model has a 1 granule (576) delay that we must compensate for
     * (mt 6/99).
     */
    short int *bufp[2];  /* address of beginning of left & right granule */
    int blocktype[2];

    ms_ratio_prev=ms_ratio[gf.mode_gr-1];
    for (gr=0; gr < gf.mode_gr ; gr++) {

      for ( ch = 0; ch < gf.stereo; ch++ )
	bufp[ch] = &inbuf[ch][576 + gr*576-FFTOFFSET];

      L3psycho_anal( bufp, gr, 
		     (FLOAT8)gf.out_samplerate,
		     check_ms_stereo,
		     &ms_ratio[gr],&ms_ratio_next,&ms_ener_ratio[gr],
		     masking_ratio, masking_MS_ratio,
		     pe[gr],pe_MS[gr],blocktype);

      for ( ch = 0; ch < gf.stereo; ch++ )
	l3_side.gr[gr].ch[ch].tt.block_type=blocktype[ch];

    }
  }else{
    for (gr=0; gr < gf.mode_gr ; gr++)
      for ( ch = 0; ch < gf.stereo; ch++ ) {
	l3_side.gr[gr].ch[ch].tt.block_type=NORM_TYPE;
	pe[gr][ch]=700;
      }
  }


  /* block type flags */
  for( gr = 0; gr < gf.mode_gr; gr++ ) {
    for ( ch = 0; ch < gf.stereo; ch++ ) {
      gr_info *cod_info = &l3_side.gr[gr].ch[ch].tt;
      cod_info->mixed_block_flag = 0;     /* never used by this model */
      if (cod_info->block_type == NORM_TYPE )
	cod_info->window_switching_flag = 0;
      else
	cod_info->window_switching_flag = 1;
    }
  }

  /* polyphase filtering / mdct */
  mdct_sub48(inbuf[0], inbuf[1], xr, &l3_side);

  if (check_ms_stereo) {
    /* make sure block type is the same in each channel */
    check_ms_stereo =
      (l3_side.gr[0].ch[0].tt.block_type==l3_side.gr[0].ch[1].tt.block_type) &&
      (l3_side.gr[1].ch[0].tt.block_type==l3_side.gr[1].ch[1].tt.block_type);
  }
  if (check_ms_stereo) {
    /* ms_ratio = is like the ratio of side_energy/total_energy */
    FLOAT8 ms_ratio_ave;
    /*     ms_ratio_ave = .5*(ms_ratio[0] + ms_ratio[1]);*/
    ms_ratio_ave = .25*(ms_ratio[0] + ms_ratio[1]+
			 ms_ratio_prev + ms_ratio_next);
    if ( ms_ratio_ave <.35) gf.mode_ext = MPG_MD_MS_LR;
  }
  if (gf.force_ms) gf.mode_ext = MPG_MD_MS_LR;


#ifdef HAVEGTK
  if (gf.gtkflag) {
    int j;
    for ( gr = 0; gr < gf.mode_gr; gr++ ) {
      for ( ch = 0; ch < gf.stereo; ch++ ) {
	pinfo->ms_ratio[gr]=ms_ratio[gr];
	pinfo->ms_ener_ratio[gr]=ms_ener_ratio[gr];
	pinfo->blocktype[gr][ch]=
	  l3_side.gr[gr].ch[ch].tt.block_type;
	for ( j = 0; j < 576; j++ ) pinfo->xr[gr][ch][j]=xr[gr][ch][j];
	/* if MS stereo, switch to MS psy data */
	if (gf.ms_masking && (gf.mode_ext==MPG_MD_MS_LR)) {
	  pinfo->pe[gr][ch]=pinfo->pe[gr][ch+2];
	  pinfo->ers[gr][ch]=pinfo->ers[gr][ch+2];
	  memcpy(pinfo->energy[gr][ch],pinfo->energy[gr][ch+2],
		 sizeof(pinfo->energy[gr][ch]));
	}
      }
    }
  }
#endif




  /* bit and noise allocation */
  if ((MPG_MD_MS_LR == gf.mode_ext) && gf.ms_masking) {
    masking = &masking_MS_ratio;    /* use MS masking */
    pe_use=&pe_MS;
  } else {
    masking = &masking_ratio;    /* use LR masking */
    pe_use=&pe;
  }




  if (gf.VBR) {
    VBR_iteration_loop( *pe_use, ms_ratio, xr, *masking, &l3_side, l3_enc,
			scalefac, &fr_ps);
  }else{
    iteration_loop( *pe_use, ms_ratio, xr, *masking, &l3_side, l3_enc,
		    scalefac, &fr_ps);
  }
  /*
  VBR_iteration_loop_new( *pe_use, ms_ratio, xr, masking, &l3_side, l3_enc,
			  &scalefac, &fr_ps);
  */



#ifdef BRHIST
  brhist_temp[gf.bitrate_index]++;
#endif


  /*  write the frame to the bitstream  */
  getframebits(&bitsPerFrame,&mean_bits);
  III_format_bitstream( bitsPerFrame, l3_enc, &l3_side,
			scalefac, &bs);


  frameBits = bs.totbit - sentBits;


  if ( frameBits % bitsPerSlot )   /* a program failure */
    fprintf( stderr, "Sent %ld bits = %ld slots plus %ld\n",
	     frameBits, frameBits/bitsPerSlot,
	     frameBits%bitsPerSlot );
  sentBits += frameBits;

  /* copy mp3 bit buffer into array */
  mpg123count = copy_buffer(mpg123bs,&bs);
  empty_buffer(&bs);  /* empty buffer */

  if (gf.bWriteVbrTag) AddVbrFrame((int)(sentBits/8));

#ifdef HAVEGTK
  if (gf.gtkflag) {
    int j;
    for ( ch = 0; ch < gf.stereo; ch++ ) {
      for ( j = 0; j < FFTOFFSET; j++ )
	pinfo->pcmdata[ch][j] = pinfo->pcmdata[ch][j+gf.framesize];
      for ( j = FFTOFFSET; j < 1600; j++ ) {
	pinfo->pcmdata[ch][j] = inbuf[ch][j-FFTOFFSET];
      }
    }
  }
#endif
  gf.frameNum++;

  return mpg123count;
}




int fill_buffer_resample(short int *outbuf,int desired_len,
        short int *inbuf,int len,int *num_used,int ch) {

  static FLOAT8 itime[2];
#define OLDBUFSIZE 5
  static short int inbuf_old[2][OLDBUFSIZE];
  static int init[2]={0,0};
  int i,j=0,k,linear;

  if (gf.frameNum==0 && !init[ch]) {
    init[ch]=1;
    itime[ch]=0;
    memset((char *) inbuf_old[ch], 0, sizeof(short int)*OLDBUFSIZE);
  }
  if (gf.frameNum!=0) init[ch]=0; /* reset, for next time framenum=0 */


  /* if downsampling by an integer multiple, use linear resampling,
   * otherwise use quadratic */
  linear = ( fabs(gf.resample_ratio - floor(.5+gf.resample_ratio)) < .0001 );

  /* time of j'th element in inbuf = itime + j/ifreq; */
  /* time of k'th element in outbuf   =  j/ofreq */
  for (k=0;k<desired_len;k++) {
    int y0,y1,y2,y3;
    FLOAT8 x0,x1,x2,x3;
    FLOAT8 time0;

    time0 = k*gf.resample_ratio;       /* time of k'th output sample */
    j = floor( time0 -itime[ch]  );
    /* itime[ch] + j;    */            /* time of j'th input sample */
    if (j+2 >= len) break;             /* not enough data in input buffer */

    x1 = time0-(itime[ch]+j);
    x2 = x1-1;
    y1 = (j<0) ? inbuf_old[ch][OLDBUFSIZE+j] : inbuf[j];
    y2 = ((1+j)<0) ? inbuf_old[ch][OLDBUFSIZE+1+j] : inbuf[1+j];

    /* linear resample */
    if (linear) {
      outbuf[k] = floor(.5 +  (y2*x1-y1*x2) );
    } else {
      /* quadratic */
      x0 = x1+1;
      x3 = x1-2;
      y0 = ((j-1)<0) ? inbuf_old[ch][OLDBUFSIZE+(j-1)] : inbuf[j-1];
      y3 = ((j+2)<0) ? inbuf_old[ch][OLDBUFSIZE+(j+2)] : inbuf[j+2];
      outbuf[k] = floor(.5 +
			-y0*x1*x2*x3/6 + y1*x0*x2*x3/2 - y2*x0*x1*x3/2 +y3*x0*x1*x2/6
			);
      /*
      printf("k=%i  new=%i   [ %i %i %i %i ]\n",k,outbuf[k],
	     y0,y1,y2,y3);
      */
    }
  }


  /* k = number of samples added to outbuf */
  /* last k sample used data from j,j+1, or j+1 overflowed buffer */
  /* remove num_used samples from inbuf: */
  *num_used = Min(len,j+2);
  itime[ch] += *num_used - k*gf.resample_ratio;
  for (i=0;i<OLDBUFSIZE;i++)
    inbuf_old[ch][i]=inbuf[*num_used + i -OLDBUFSIZE];
  return k;
}




int fill_buffer(short int *outbuf,int desired_len,short int *inbuf,int len) {
  int j;
  j=Min(desired_len,len);
  memcpy( (char *) outbuf,(char *)inbuf,sizeof(short int)*j);
  return j;
}




/*
 * THE MAIN LAME ENCODING INTERFACE
 * mt 3/00
 *
 * input pcm data, output (maybe) mp3 frames.
 * This routine handles all buffering, resampling and filtering for you.
 * The required mp3buffer_size can be computed from num_samples,
 * samplerate and encoding rate, but here is a worst case estimate:
 *
 * mp3buffer_size in bytes = 1.25*num_samples + 7200
 *
 * return code = number of bytes output in mp3buffer.  can be 0
*/
int lame_encode_buffer(short int buffer_l[], short int buffer_r[],int nbuffer,
   char *mp3buf, int mpg123size)
{
  static int frame_buffered=0;
  int mp3size=0,ret,i,ch,mf_needed;

  short int *in_buffer[2];
  in_buffer[0] = buffer_l;
  in_buffer[1] = buffer_r;

  /* some sanity checks */
  assert(ENCDELAY>=MDCTDELAY);
  assert(BLKSIZE-FFTOFFSET >= 0);
  mf_needed = BLKSIZE+gf.framesize-FFTOFFSET;
  assert(MFSIZE>=mf_needed);

  /* The reason for
   *       int mf_samples_to_encode = ENCDELAY + 288;
   * ENCDELAY = internal encoder delay.  And then we have to add 288
   * because of the 50% MDCT overlap.  A 576 MDCT granule decodes to
   * 1152 samples.  To synthesize the 576 samples centered under this granule
   * we need the previous granule for the first 288 samples (no problem), and
   * the next granule for the next 288 samples (not possible if this is last
   * granule).  So we need to pad with 288 samples to make sure we can
   * encode the 576 samples we are interested in.
   */
  if (gf.frameNum==0 && !frame_buffered) {
    memset((char *) mfbuf, 0, sizeof(mfbuf));
    frame_buffered=1;
    mf_samples_to_encode = ENCDELAY+288;
    mf_size=ENCDELAY-MDCTDELAY;  /* we pad input with this many 0's */
  }
  if (gf.frameNum==1) {
    /* reset, for the next time frameNum==0 */
    frame_buffered=0;
  }

  while (nbuffer > 0) {
    int n_in=0;
    int n_out=0;
    /* copy in new samples */
    for (ch=0; ch<gf.stereo; ch++) {
      if (gf.resample_ratio!=1)  {
	n_out=fill_buffer_resample(&mfbuf[ch][mf_size],gf.framesize,
					  in_buffer[ch],nbuffer,&n_in,ch);
      } else {
	n_out=fill_buffer(&mfbuf[ch][mf_size],gf.framesize,in_buffer[ch],nbuffer);
	n_in = n_out;
      }
      in_buffer[ch] += n_in;
    }
    nbuffer -= n_in;
    mf_size += n_out;
    assert(mf_size<=MFSIZE);
    mf_samples_to_encode += n_out;

    if (mf_size >= mf_needed) {
      /* encode the frame */
      ret = lame_encode_frame(mfbuf[0],mfbuf[1],mf_size,mp3buf);
      mp3buf += ret;
      mp3size += ret;

      /* shift out old samples */
      mf_size -= gf.framesize;
      mf_samples_to_encode -= gf.framesize;
      for (ch=0; ch<gf.stereo; ch++)
	for (i=0; i<mf_size; i++)
	  mfbuf[ch][i]=mfbuf[ch][i+gf.framesize];
    }
  }
  assert(nbuffer==0);
  return mp3size;
}


/* old LAME interface */
/* With this interface, it is the users responsibilty to keep track of the
 * buffered, unencoded samples.  Thus mf_samples_to_encode is not incremented.
 *
 * lame_encode() is also used to flush the PCM input buffer by
 * lame_encode_finish()
 */
int lame_encode(short int in_buffer[2][1152],char *mpg123bs){
  int imp3,save;
  save = mf_samples_to_encode;
  imp3= lame_encode_buffer(in_buffer[0],in_buffer[1],576*gf.mode_gr,
        mpg123bs,LAME_MAXMP3BUFFER);
  mf_samples_to_encode = save;
  return imp3;
}




/* initialize mp3 encoder */
lame_global_flags * lame_init(void)
{

  /*
   *  Disable floating point exepctions
   */
#ifdef __FreeBSD__
# include <floatingpoint.h>
  {
  /* seet floating point mask to the Linux default */
  fp_except_t mask;
  mask=fpgetmask();
  /* if bit is set, we get SIGFPE on that error! */
  fpsetmask(mask & ~(FP_X_INV|FP_X_DZ));
  /*  fprintf(stderr,"FreeBSD mask is 0x%x\n",mask); */
  }
#endif
#if defined(__riscos__) && !defined(ABORTFP)
  /* Disable FPE's under RISC OS */
  /* if bit is set, we disable trapping that error! */
  /*   _FPE_IVO : invalid operation */
  /*   _FPE_DVZ : divide by zero */
  /*   _FPE_OFL : overflow */
  /*   _FPE_UFL : underflow */
  /*   _FPE_INX : inexact */
  DisableFPETraps( _FPE_IVO | _FPE_DVZ | _FPE_OFL );
#endif


  /*
   *  Debugging stuff
   *  The default is to ignore FPE's, unless compiled with -DABORTFP
   *  so add code below to ENABLE FPE's.
   */

#if defined(ABORTFP) && !defined(__riscos__)
#if defined(_MSC_VER)
  {
	#include <float.h>
	unsigned int mask;
	mask=_controlfp( 0, 0 );
	mask&=~(_EM_OVERFLOW|_EM_UNDERFLOW|_EM_ZERODIVIDE|_EM_INVALID);
	mask=_controlfp( mask, _MCW_EM );
	}
#elif defined(__CYGWIN__)
#  define _FPU_GETCW(cw) __asm__ ("fnstcw %0" : "=m" (*&cw))
#  define _FPU_SETCW(cw) __asm__ ("fldcw %0" : : "m" (*&cw))

#  define _EM_INEXACT     0x00000001 /* inexact (precision) */
#  define _EM_UNDERFLOW   0x00000002 /* underflow */
#  define _EM_OVERFLOW    0x00000004 /* overflow */
#  define _EM_ZERODIVIDE  0x00000008 /* zero divide */
#  define _EM_INVALID     0x00000010 /* invalid */
  {
    unsigned int mask;
    _FPU_GETCW(mask);
    /* Set the FPU control word to abort on most FPEs */
    mask &= ~(_EM_UNDERFLOW | _EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID);
    _FPU_SETCW(mask);
  }
# else
  {
#  include <fpu_control.h>
#ifndef _FPU_GETCW
#define _FPU_GETCW(cw) __asm__ ("fnstcw %0" : "=m" (*&cw))
#endif
#ifndef _FPU_SETCW
#define _FPU_SETCW(cw) __asm__ ("fldcw %0" : : "m" (*&cw))
#endif
    unsigned int mask;
    _FPU_GETCW(mask);
    /* Set the Linux mask to abort on most FPE's */
    /* if bit is set, we _mask_ SIGFPE on that error! */
    /*  mask &= ~( _FPU_MASK_IM | _FPU_MASK_ZM | _FPU_MASK_OM | _FPU_MASK_UM );*/
    mask &= ~( _FPU_MASK_IM | _FPU_MASK_ZM | _FPU_MASK_OM );
    _FPU_SETCW(mask);
  }
#endif
#endif /* ABORTFP && !__riscos__ */



  /* Global flags.  set defaults here */
  gf.allow_diff_short=0;
  gf.ATHonly=0;
  gf.noATH=0;
  gf.bWriteVbrTag=1;
  gf.cwlimit=0;
  gf.disable_reservoir=0;
  gf.experimentalX = 0;
  gf.experimentalY = 0;
  gf.experimentalZ = 0;
  gf.frameNum=0;
  gf.gtkflag=0;
  gf.quality=5;
  gf.input_format=sf_unknown;

  gf.filter_type=0;
  gf.lowpassfreq=0;
  gf.highpassfreq=0;
  gf.lowpasswidth=-1;
  gf.highpasswidth=-1;
  gf.lowpass1=0;
  gf.lowpass2=0;
  gf.highpass1=0;
  gf.highpass2=0;
  gf.lowpass_band=32;
  gf.highpass_band=-1;

  gf.no_short_blocks=0;
  gf.resample_ratio=1;
  gf.padding_type=2;
  gf.padding=0;
  gf.swapbytes=0;
  gf.silent=0;
  gf.totalframes=0;
  gf.VBR=0;
  gf.VBR_q=4;
  gf.VBR_min_bitrate_kbps=0;
  gf.VBR_max_bitrate_kbps=0;
  gf.VBR_min_bitrate=1;
  gf.VBR_max_bitrate=13;


  gf.version = 1;   /* =1   Default: MPEG-1 */
  gf.mode = MPG_MD_JOINT_STEREO;
  gf.force_ms=0;
  gf.brate=0;
  gf.copyright=0;
  gf.original=1;
  gf.extension=0;
  gf.error_protection=0;
  gf.emphasis=0;
  gf.in_samplerate=1000*44.1;
  gf.out_samplerate=0;
  gf.num_channels=2;
  gf.num_samples=MAX_U_32_NUM;

  gf.inPath=NULL;
  gf.outPath=NULL;
  id3tag.used=0;

  return &gf;
}



/*****************************************************************/
/* flush internal mp3 buffers,                                   */
/*****************************************************************/
int lame_encode_finish(char *mp3buffer)
{
  int imp3,mp3count;
  short int buffer[2][1152];
  memset((char *)buffer,0,sizeof(buffer));
  mp3count = 0;

  while (mf_samples_to_encode > 0) {
    imp3=lame_encode(buffer,mp3buffer);
    mp3buffer += imp3;
    mp3count += imp3;
    mf_samples_to_encode -= gf.framesize;
  }


  gf.frameNum--;
  if (!gf.gtkflag && !gf.silent) {
      timestatus(gf.out_samplerate,gf.frameNum,gf.totalframes,gf.framesize);
#ifdef BRHIST
      if (disp_brhist)
	{
	  brhist_add_count();
	  brhist_disp();
	  brhist_disp_total();
	}
#endif
      fprintf(stderr,"\n");
      fflush(stderr);
  }


  III_FlushBitstream();
  mp3count += copy_buffer(mp3buffer,&bs);
  empty_buffer(&bs);  /* empty buffer */

  desalloc_buffer(&bs);    /* Deallocate all buffers */

  return mp3count;
}


/*****************************************************************/
/* write VBR Xing header, and ID3 tag, if asked for               */
/*****************************************************************/
void lame_mp3_tags(void)
{
  if (gf.bWriteVbrTag)
    {
      /* Calculate relative quality of VBR stream
       * 0=best, 100=worst */
      int nQuality=gf.VBR_q*100/9;
      /* Write Xing header again */
      PutVbrTag(gf.outPath,nQuality);
    }


  /* write an ID3 tag  */
  if(id3tag.used) {
    id3_buildtag(&id3tag);
    id3_writetag(gf.outPath, &id3tag);
  }
}


void lame_version(char *ostring) {
  strncpy(ostring,get_lame_version(),20);
}

