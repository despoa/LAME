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

/* maximum size of mp3buffer needed if you encode at most 1152 samples for
   each call to lame_encode_buffer.  see lame_encode_buffer() below  */
#define LAME_MAXMP3BUFFER 16384


typedef enum sound_file_format_e {
	sf_unknown, sf_wave, sf_aiff, sf_mp3, sf_raw
} sound_file_format;
typedef struct
{
	int valid;
	char title[31];
	char artist[31];
	char album[31];
	char year[5];
	char comment[31];
	char tagtext[128];
	char genre[1];
	unsigned char track;

}   ID3TAGDATA;




/***********************************************************************
*
*  Control Parameters set by User
*
*  substantiated by calling program
*
*  Initilized and default values set by lame_init(&gf)
*
*
***********************************************************************/
typedef struct  {
  /* input file description */
  unsigned long num_samples;  /* number of samples. default=2^32-1    */
  int num_channels;           /* input number of channels. default=2  */
  int in_samplerate;          /* input_samp_rate. default=44.1kHz     */
  int out_samplerate;         /* output_samp_rate. (usually determined automatically)   */ 

  /* general control params */
  int gtkflag;                /* run frame analyzer?       */
  int bWriteVbrTag;           /* add Xing VBR tag?         */
  int decode_only;            /* use lame/mpglib to convert mp3 to wav */

  int quality;                /* quality setting 0=best,  9=worst  */
  int silent;                 /* disable some status output */
  int brhist_disp;            /* enable VBR bitrate histogram display */
  int mode;                       /* 0,1,2,3 stereo,jstereo,dual channel,mono */
  int mode_fixed;                 /* use specified the mode, do not use lame's opinion of the best mode */
  int force_ms;                   /* force M/S mode.  requires mode=1 */
  int brate;                      /* bitrate */
  int user_comp_ratio;            /* user specified compression ratio, instead of brate */
  int free_format;                /* use free format? */

  /* frame params */
  int copyright;                  /* mark as copyright. default=0 */
  int original;                   /* mark as original. default=1 */
  int error_protection;           /* use 2 bytes per frame for a CRC checksum. default=0*/
  int padding_type;               /* 0=no padding, 1=always pad, 2=adjust padding */
  int extension;                  /* the MP3 'private extension' bit.  meaningless */
  int strict_ISO;                 /* enforce ISO spec as much as possible */

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
  int VBR_hard_min;             /* strictly enforce VBR_min_bitrate*/
                                /* normaly, it will be violated for analog silence */


  /* resampling and filtering */
  int lowpassfreq;                /* freq in Hz. 0=lame choses. -1=no filter */
  int highpassfreq;               /* freq in Hz. 0=lame choses. -1=no filter */
  int lowpasswidth;               /* freq width of filter, in Hz (default=15%)*/
  int highpasswidth;              /* freq width of filter, in Hz (default=15%)*/


  /* I/O - not used if calling program does the i/o */
  sound_file_format input_format;   
  FILE * musicin;             /* file pointer to input file */
  int swapbytes;              /* force byte swapping   default=0*/
#define         MAX_NAME_SIZE           1000
  char inPath[MAX_NAME_SIZE];
  /* Note: outPath must be set if you want Xing VBR or id3 tags written */
  char outPath[MAX_NAME_SIZE];



  /* optional id3 tags  */
  int id3tag_used;
  ID3TAGDATA id3tag;


  /* psycho acoustics and other aguments which you should not change 
   * unless you know what you are doing  */
  int ATHonly;                    /* only use ATH */
  int ATHshort;                   /* only use ATH for short blocks */
  int noATH;                      /* disable ATH */
  float cwlimit;                  /* predictability limit */
  int allow_diff_short;       /* allow blocktypes to differ between channels ? */
  int no_short_blocks;        /* disable short blocks       */
  int emphasis;                   /* obsolete */




  /************************************************************************/
  /* internal variables, do not set... */
  /************************************************************************/
  int version;                /* 0=MPEG2  1=MPEG1 */

  /* VBR tags */
  int nZeroStreamSize;
  int TotalFrameSize;
  int* pVbrFrames;
  int nVbrNumFrames;
  int nVbrFrameBufferSize;


  /************************************************************************/
  /* more internal variables, which will not exist after lame_encode_finish() */
  /************************************************************************/
  void *internal_flags;

} lame_global_flags;






/*

The LAME API

 */


/* REQUIRED: initialize the encoder.  sets default for all encoder paramters,
 * returns -1 if some malloc()'s failed
 * otherwise returns 0
 * 
 */
int lame_init(lame_global_flags *);




/*********************************************************************
 * command line argument parsing & option setting.  Only supported
 * if libmp3lame compiled with LAMEPARSE defined 
 *********************************************************************/
/* OPTIONAL: call this to print an error with a brief command line usage guide and quit 
 * only supported if libmp3lame compiled with LAMEPARSE defined.  
 */
void lame_usage(lame_global_flags *, char *);

/* OPTIONAL: call this to print a command line interface usage guide and quit   */
void lame_help(lame_global_flags *, char *);

/* OPTIONAL: get the version number, in a string. of the form:  "3.63 (beta)" or 
   just "3.63".  Max allows length is 20 characters  */
void lame_version(lame_global_flags *, char *);


/* OPTIONAL: set internal options via command line argument parsing 
 * You can skip this call if you like the default values, or if
 * set the encoder parameters your self 
 */
void lame_parse_args(lame_global_flags *, int argc, char **argv);





/* REQUIRED:  sets more internal configuration based on data provided
 * above
 */
void lame_init_params(lame_global_flags *);


/* OPTONAL:  print internal lame configuration on stderr*/
void lame_print_config(lame_global_flags *);




/* input pcm data, output (maybe) mp3 frames.
 * This routine handles all buffering, resampling and filtering for you.
 * 
 * leftpcm[]       array of 16bit pcm data, left channel
 * rightpcm[]      array of 16bit pcm data, right channel
 * num_samples     number of samples in leftpcm[] and rightpcm[] (if stereo)
 * mp3buffer       pointer to buffer where mp3 output is written
 * mp3buffer_size  size of mp3buffer, in bytes
 * return code     number of bytes output in mp3buffer.  can be 0 
 *                 -1:  mp3buffer was too small
 *                 -2:  malloc() problem
 *                 -3:  lame_init_params() not called
 *
 * The required mp3buffer_size can be computed from num_samples, 
 * samplerate and encoding rate, but here is a worst case estimate:
 *
 * mp3buffer_size in bytes = 1.25*num_samples + 7200
 *
 * I think a tighter bound could be:  (mt, March 2000)
 * MPEG1:
 *    num_samples*(bitrate/8)/samplerate + 4*1152*(bitrate/8)/samplerate + 512
 * MPEG2:
 *    num_samples*(bitrate/8)/samplerate + 4*576*(bitrate/8)/samplerate + 256
 *
 * but test first if you use that!
 *
 * set mp3buffer_size = 0 and LAME will not check if mp3buffer_size is
 * large enough.
 *
 * NOTE: if gfp->num_channels=2, but gfp->mode = 3 (mono), the L & R channels
 * will be averaged into the L channel before encoding only the L channel
 * This will overwrite the data in leftpcm[] and rightpcm[].
 * 
*/
int lame_encode_buffer(lame_global_flags *,short int leftpcm[], short int rightpcm[],int num_samples,
char *mp3buffer,int  mp3buffer_size);

/* as above, but input has L & R channel data interleaved.  Note: 
 * num_samples = number of samples in the L (or R)
 * channel, not the total number of samples in pcm[]  
 */
int lame_encode_buffer_interleaved(lame_global_flags *,short int pcm[], 
int num_samples, char *mp3buffer,int  mp3buffer_size);



/* input 1 pcm frame, output (maybe) 1 mp3 frame.  
 * return code = number of bytes output in mp3buffer.  can be 0 
 * NOTE: this interface is outdated, please use lame_encode_buffer() instead 
 * declair mp3buffer with:  char mp3buffer[LAME_MAXMP3BUFFER] 
 * if return code = -1:  mp3buffer was too small 
 */
int lame_encode(lame_global_flags *,short int Buffer[2][1152],char *mp3buffer,int mp3buffer_size);



/* REQUIRED:  lame_encode_finish will flush the buffers and may return a 
 * final few mp3 frames.  mp3buffer should be at least 7200 bytes.
 *
 * return code = number of bytes output to mp3buffer.  can be 0
 */
int lame_encode_finish(lame_global_flags *,char *mp3buffer, int size);


/* OPTIONAL:  lame_mp3_tags will append id3 and Xing VBR tags to
the mp3 file with name given by gf->outPath.  These calls open the file,
write tags, and close the file, so make sure the the encoding is finished
before calling these routines.  
Note: if VBR and id3 tags are turned off by the user, or turned off
by LAME because the output is not a regular file, this call does nothing
*/
void lame_mp3_tags(lame_global_flags *);




/*********************************************************************
 * lame file i/o.  Only supported
 * if libmp3lame compiled with LAMESNDFILE or LIBSNDFILE
 *********************************************************************/
/* OPTIONAL: open the input file, and parse headers if possible 
 * you can skip this call if you will do your own PCM input 
 */
void lame_init_infile(lame_global_flags *);

/* OPTIONAL:  read one frame of PCM data from audio input file opened by 
 * lame_init_infile.  Input file can be wav, aiff, raw pcm, anything
 * supported by libsndfile, or an mp3 file
 */
int lame_readframe(lame_global_flags *,short int Buffer[2][1152]);

/* OPTIONAL: close the sound input file if lame_init_infile() was used */
void lame_close_infile(lame_global_flags *);





/*********************************************************************
 * a simple interface to mpglib, part of mpg123, is also included if
 * libmp3lame is compiled with HAVEMPGLIB
 * input 1 mp3 frame, output (maybe) pcm data.  
 * lame_decode return code:  -1: error.  0: need more data.  n>0: size of pcm output
 *********************************************************************/
int lame_decode_init(void);
int lame_decode(char *mp3buf,int len,short pcm_l[],short pcm_r[]);

/* same as lame_decode, but returns at most one frame */
int lame_decode1(char *mp3buf,int len,short pcm_l[],short pcm_r[]);


/* read mp3 file until mpglib returns one frame of PCM data */
#ifdef AMIGA_MPEGA
int lame_decode_initfile(const char *fullname,int *stereo,int *samp,int *bitrate, unsigned long *nsamp);
int lame_decode_fromfile(FILE *fd,short int pcm_l[], short int pcm_r[]);
#else
int lame_decode_initfile(FILE *fd,int *stereo,int *samp,int *bitrate, unsigned long *nsamp);
int lame_decode_fromfile(FILE *fd,short int pcm_l[],short int pcm_r[]);
#endif




#endif
