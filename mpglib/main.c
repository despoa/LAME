#ifdef HAVEMPGLIB

#include "mpg123.h"
#include "mpglib.h"

#ifdef PARENT_IS_SLASH
#include "/lame.h"
#include "/util.h"
#include "/VbrTag.h"
#else
#include "../lame.h"
#include "../util.h"
#include "../VbrTag.h"
#endif 

#include <stdlib.h>

static char buf[16384];
#define FSIZE 8192  
static char out[FSIZE];
struct mpstr mp;
plotting_data *mpg123_pinfo=NULL;


int is_syncword(char *header)
{

/*
unsigned int s0,s1;
s0 = (unsigned char) header[0];
s1 = (unsigned char) header[1] ;
printf(" syncword:  %2X   %2X   \n ",s0, s1);
*/

  int mpeg1=((int) ( header[0] == (char) 0xFF)) &&
    ((int) ( (header[1] & (char) 0xF0) == (char) 0xF0));
  
  int mpeg25=((int) ( header[0] == (char) 0xFF)) &&
    ((int) ( (header[1] & (char) 0xF0) == (char) 0xE0));
  
  return (mpeg1 || mpeg25);
 

}


int lame_decode_initfile(FILE *fd, mp3data_struct *mp3data)
{
  extern int tabsel_123[2][3][16];
  VBRTAGDATA pTagData;
  int ret,size,framesize;
  unsigned long num_frames=0;
  size_t len;
  int xing_header;


  InitMP3(&mp);
  memset(buf, 0, sizeof(buf));


  /* skip RIFF type proprietary headers  */
  /* look for sync word  FFF */
  while (!is_syncword(buf)) {
    buf[0]=buf[1]; 
    if (fread(&buf[1],1,1,fd) == 0) return -1;  /* failed */
  }
  
  /* read the rest of header and enough bytes to check for Xing header */
  len = fread(&buf[2],1,46,fd);
  if (len ==0 ) return -1;
  len +=2;
  
  /* check for Xing header */
  xing_header = GetVbrTag(&pTagData,(unsigned char*)buf);
  if (xing_header) {
    num_frames=pTagData.frames;

    /* look for next sync word in buffer*/
    buf[1]=0;
    while (!is_syncword(buf)) {
      buf[0]=buf[1]; 
      if (fread(&buf[1],1,1,fd) == 0) return -1;  /* fread failed */
    }
    /* read the rest of header */
    len = fread(&buf[2],1,2,fd);
    if (len ==0 ) return -1;
    len +=2;
    

  } else {
    /* rewind file back what we read looking for Xing headers */
    if (fseek(fd, -44, SEEK_CUR) != 0) {
      /* backwards fseek failed.  input is probably a pipe */
    } else {
      len=4;
    }
  }
  
  size=0;
  ret = decodeMP3(&mp,buf,(int)len,out,FSIZE,&size);
  if (ret==MP3_OK && size>0 && !xing_header) {
    fprintf(stderr,"Oops: first frame of mpglib output will be lost \n");
  }
  
  if (ret==MP3_ERR) 
    return -1;
  
  if (!mp.header_parsed) 
    return -1;

  mp3data->stereo = mp.fr.stereo;
  mp3data->samplerate = freqs[mp.fr.sampling_frequency];
  mp3data->bitrate = tabsel_123[mp.fr.lsf][mp.fr.lay-1][mp.fr.bitrate_index];
  framesize = (mp.fr.lsf == 0) ? 1152 : 576;
  mp3data->nsamp=MAX_U_32_NUM;
  
  if (xing_header && num_frames) {
    mp3data->nsamp=framesize * num_frames;
  }

  /*
  printf("ret = %i NEED_MORE=%i \n",ret,MP3_NEED_MORE);
  printf("stereo = %i \n",mp.fr.stereo);
  printf("samp = %i  \n",(int)freqs[mp.fr.sampling_frequency]);
  printf("framesize = %i  \n",framesize);
  printf("num frames = %i  \n",(int)num_frames);
  printf("num samp = %i  \n",(int)*num_samples);
  */
  return 0;
}


int lame_decode_init(void)
{
  InitMP3(&mp);
  memset(buf, 0, sizeof(buf));
  return 0;
}


/*
For lame_decode_fromfile:  return code
  -1     error
   0     ok, but need more data before outputing any samples
   n     number of samples output.  either 576 or 1152 depending on MP3 file.
*/
int lame_decode_fromfile(FILE *fd, short pcm_l[], short pcm_r[],mp3data_struct *mp3data)
{
  int size,stereo;
  int outsize=0,j,i,ret;
  size_t len;

  size=0;
  len = fread(buf,1,64,fd);
  if (len ==0 ) return 0;
  ret = decodeMP3(&mp,buf,(int)len,out,FSIZE,&size);

  /* read more until we get a valid output frame */
  while((ret == MP3_NEED_MORE) || !size) {
    len = fread(buf,1,100,fd);
    if (len ==0 ) return -1;
    ret = decodeMP3(&mp,buf,(int)len,out,FSIZE,&size);
    /* if (ret ==MP3_ERR) return -1;  lets ignore errors and keep reading... */
    /*
    printf("ret = %i size= %i  %i   %i  %i \n",ret,size,
	   MP3_NEED_MORE,MP3_ERR,MP3_OK); 
    */
  }

  stereo=mp.fr.stereo;

  if (ret == MP3_OK) 
  {
    mp3data->stereo = mp.fr.stereo;
    mp3data->samplerate = freqs[mp.fr.sampling_frequency];
    /* bitrate formula works for free bitrate also */
    mp3data->bitrate = .5 + 8*(4+mp.fsizeold)*freqs[mp.fr.sampling_frequency]/
      (1000.0*576*(2-mp.fr.lsf));
    /*    write(1,out,size); */
    outsize = size/(2*(stereo));
    if ((outsize!=576) && (outsize!=1152)) {
      fprintf(stderr,"Oops: mpg123 returned more than one frame!  Cant handle this... \n");
    }

    for (j=0; j<stereo; j++)
      for (i=0; i<outsize; i++) 
	if (j==0) pcm_l[i] = ((short *) out)[mp.fr.stereo*i+j];
	else pcm_r[i] = ((short *) out)[mp.fr.stereo*i+j];

  }
  if (ret==MP3_ERR) return -1;
  else return outsize;
}




/*
For lame_decode:  return code
  -1     error
   0     ok, but need more data before outputing any samples
   n     number of samples output.  either 576 or 1152 depending on MP3 file.
*/
int lame_decode1(char *buffer,int len,short pcm_l[],short pcm_r[])
{
  int size;
  int outsize=0,j,i,ret;


  ret = decodeMP3(&mp,buffer,len,out,FSIZE,&size);
  if (ret==MP3_ERR) return -1;
  
  if (ret==MP3_OK) {
    outsize = size/(2*mp.fr.stereo);
    
    for (j=0; j<mp.fr.stereo; j++)
      for (i=0; i<outsize; i++) 
	if (j==0) pcm_l[i] = ((short *) out)[mp.fr.stereo*i+j];
	else pcm_r[i] = ((short *) out)[mp.fr.stereo*i+j];
  }
  if (ret==MP3_NEED_MORE) 
    outsize=0;
  
  /*
  printf("ok, more, err:  %i %i %i  \n",MP3_OK, MP3_NEED_MORE, MP3_ERR);
  printf("ret = %i out=%i \n",ret,totsize);
  */
  return outsize;
}




/*
For lame_decode:  return code
  -1     error
   0     ok, but need more data before outputing any samples
   n     number of samples output.  a multiple of 576 or 1152 depending on MP3 file.
*/
int lame_decode(char *buffer,int len,short pcm_l[],short pcm_r[])
{
  int size,totsize=0;
  int outsize=0,j,i,ret;

  do {
    ret = decodeMP3(&mp,buffer,len,out,FSIZE,&size);
    if (ret==MP3_ERR) return -1;

    if (ret==MP3_OK) {
      outsize = size/(2*mp.fr.stereo);
      
      for (j=0; j<mp.fr.stereo; j++)
	for (i=0; i<outsize; i++) 
	  if (j==0) pcm_l[totsize + i] = ((short *) out)[mp.fr.stereo*i+j];
	  else pcm_r[totsize + i] = ((short *) out)[mp.fr.stereo*i+j];

      totsize += outsize;
    }
    len=0;  /* future calls to decodeMP3 are just to flush buffers */
  } while (ret!=MP3_NEED_MORE);
  /*
  printf("ok, more, err:  %i %i %i  \n",MP3_OK, MP3_NEED_MORE, MP3_ERR);
  printf("ret = %i out=%i \n",ret,totsize);
  */
  return totsize;
}

#endif /* HAVEMPGLIB */

