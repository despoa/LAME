/*
 *	Interface to MP3 LAME encoding engine
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
#ifndef LAME_H_INCLUDE
#define LAME_H_INCLUDE
#include <stdio.h>
#define LAME_MAXMP3BUFFER 16384


typedef enum sound_file_format_e {
	sf_unknown, sf_wave, sf_aiff, sf_mp3, sf_raw
} sound_file_format;


/***********************************************************************
*
*  Global Variables.  
*
*  substantiated in lame.c
*
*  Initilized and default values set by gf=lame_init()
*  gf is a pointer to this struct, which the user may use to 
*  override any of the default values
*
*  a call to lame_set_params() is also needed
*
***********************************************************************/
typedef struct  {
  /* input file description */
  unsigned long num_samples;  /* number of samples         */
  int num_channels;           /* input number of channels  */
  int samplerate;             /* input_samp_rate           */

  /* general control params */
  int gtkflag;                /* frame analyzer?       */
  int bWriteVbrTag;           /* Xing VBR tag?         */
  int quality;                /* quality setting 0=best,  9=worst  */
  int silent;                 /* disable some status output */
  int mode;                       /* mono, stereo, jstereo */
  int mode_fixed;                 /* use specified mode, not lame's opinion of the best mode */
  int force_ms;                   /* force M/S mode */
  int brate;                      /* bitrate */

  /* frame params */
  int copyright;                  /* mark as copyright */
  int original;                   /* mark as original */
  int error_protection;           /* use 2 bytes per frame for a CRC checksum */
  int padding;                    /* 0=no padding, 1=always pad, 2=adjust padding */

  /* quantization/noise shaping */
  int disable_reservoir;          /* use bit reservoir? */
  int experimentalX;            
  int experimentalY;
  int experimentalZ;

  /* VBR control */
  int VBR;
  int VBR_q;
  int VBR_min_bitrate_kbps;
  int VBR_max_bitrate_kbps;


  /* resampling and filtering */
  int resamplerate;               /* output_samp_rate.   */ 
  int lowpassfreq;                /* freq in Hz. 0=lame choses. -1=no filter */
  int highpassfreq;               /* freq in Hz. 0=lame choses. -1=no filter */
  int lowpasswidth;               /* freq width of filter, in Hz (default=15%)*/
  int highpasswidth;              /* freq width of filter, in Hz (default=15%)*/


  /* input file reading - not used if calling program does the i/o */
  sound_file_format input_format;   
  int swapbytes;              /* force byte swapping   default=0*/
  char *inPath;               /* name of input file */
  char *outPath;              /* name of output file. */
  /* Note: outPath must be set if you want Xing VBR or id3 tags
   * written */


  /* psycho acoustics and other aguments which you should not change 
   * unless you know what you are doing  */
  int ATHonly;                    /* only use ATH */
  int noATH;                      /* disable ATH */
  float cwlimit;                  /* predictability limit */
  int allow_diff_short;       /* allow blocktypes to differ between channels ? */
  int no_short_blocks;        /* disable short blocks       */
  int emphasis;                   /* obsolete */
  int sfb21;                      /* soon to be obsolete */



  /*******************************************************************/
  /* internal variables NOT set by calling program, but which might  */
  /* be of interest to the calling program                           */
  /*******************************************************************/
  long int frameNum;              /* frame counter */
  long totalframes;               /* frames: 0..totalframes-1 (estimate)*/
  int encoder_delay;
  int framesize;
  int mode_gr;                    /* granules per frame */
  int stereo;                     /* number of channels */
  int VBR_min_bitrate;            /* min bitrate index */
  int VBR_max_bitrate;            /* max bitrate index */
  float resample_ratio;           /* input_samp_rate/output_samp_rate */
  float lowpass1,lowpass2;        /* internal filter settings */
  float highpass1,highpass2;

  int filter_type;          /* 0=MDCT filter, 1= (expensive) filters */
  int quantization;         /* 0 = ISO formual,  1=best amplitude */
  int noise_shaping;        /* 0 = none 
                               1 = ISO AAC model
                               2 = allow scalefac_select=1  
                             */

  int noise_shaping_stop;   /* 0 = stop at over=0, all scalefacs amplified or
                                   a scalefac has reached max value
                               1 = stop when all scalefacs amplified or        
                                   a scalefac has reached max value
                               2 = stop when all scalefacs amplified 
			    */

  int psymodel;             /* 0 = none   1=gpsycho */
  int ms_masking;           /* 0 = no,  1 = use mid/side maskings */
  int use_best_huffman;     /* 0 = no.  1=outside loop  2=inside loop(slow) */



} lame_global_flags;






/*

The LAME API

 */


/* REQUIRED: initialize the encoder.  sets default for all encoder paramters,
 * returns pointer to encoder parameters listed above 
 */
lame_global_flags *lame_init(void);

/* OPTIONAL: call this to print an error with a brief command line usage guide and quit 
 */
void lame_usage(char *);


/* OPTIONAL: call this to print a command line interface usage guide and quit 
 */
void lame_help(char *);


/* OPTIONAL: set internal options via command line argument parsing 
 * You can skip this call if you like the default values, or if
 * set the encoder parameters your self 
 */
void lame_parse_args(int argc, char **argv);


/* OPTIONAL: open the input file, and parse headers if possible 
 * you can skip this call if you will do your own PCM input 
 */
void lame_init_infile(void);


/* REQUIRED:  sets more internal configuration based on data provided
 * above
 */
void lame_init_params();


/* OPTONAL:  print internal lame configuration on stderr*/
void lame_print_config();


/* OPTIONAL:  read one frame of PCM data from audio input file opened by 
 * lame_init_infile.  Input file can be wav, aiff, raw pcm, anything
 * supported by libsndfile, or an mp3 file
 */
int lame_readframe(short int Buffer[2][1152]);


/* input 1 pcm frame, output (maybe) 1 mp3 frame. */ 
/* return code = number of bytes output in mp3buffer.  can be 0 */
int lame_encode(short int Buffer[2][1152],char *mp3buffer);


/* here's a better interface (not yet written).  It will do the 1152 frame
 * buffering for you, as well as downsampling.  User is required to
 * provide the mp3buffer size (is there any good way around this?)
 *
 * return code = number of bytes output in mp3buffer.  can be 0 
*/
int lame_encode_buffer(short int leftpcm[], short int rightpcm[],int num_samples,
char *mp3buffer,int  mp3buffer_size);




/* REQUIRED:  lame_cleanup will flush the buffers and may return a 
 *final mp3 frame 
*/
int lame_encode_finish(char *mp3buf);

/* OPTIONAL: close the sound input file if lame_init_infile() was used */
void lame_close_infile(void);


/* OPTIONAL:  lame_mp3_tags will append id3 and Xing VBR tags to
the mp3 file with name given by gf->outPath.  These cals open the file,
write tags, and close the file, so make sure the the encoding is finished
before calling these routines.  
Note: if VBR and id3 tags are turned off by the user, or turned off
by LAME because the output is not a regular file, this call does nothing
*/
void lame_mp3_tags(void);



/* a simple interface to mpglib, part of mpg123, is also included:
 * input 1 mp3 frame, output (maybe) 1 pcm frame.   
 * lame_decode return code:  -1: error.  0: need more data.  n>0: size of pcm output
 */
int lame_decode_init(void);
int lame_decode(char *mp3buf,int len,short pcm[][1152]);


#endif
