#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "lame.h"
#include "rtp.h"


/*

encode (via LAME) to mp3 with RTP streaming of the output.

Author:  Felix von Leitner <leitner@vim.org>

mp3rtp  ip:port:ttl  [lame encoding options]  infile outfile

example:

arecord -b 16 -s 22050 -w | ./mp3rtp 224.17.23.42:5004:2 -b 56 - /dev/null



*/


struct rtpheader RTPheader;
struct sockaddr_in rtpsi;
int rtpsocket;

void rtp_output(char *mp3buffer,int mp3size)
{
  sendrtp(rtpsocket,&rtpsi,&RTPheader,mp3buffer,mp3size);
  RTPheader.timestamp+=5;
  RTPheader.b.sequence++;
}

void rtp_usage(void) {
    fprintf(stderr,"usage: mp3rtp ip:port:ttl  [encoder options] <infile> <outfile>\n");
    exit(1);
}



char mp3buffer[LAME_MAXMP3BUFFER];


/************************************************************************
*
* main
*
* PURPOSE:  MPEG-1,2 Layer III encoder with GPSYCHO 
* psychoacoustic model.
*
************************************************************************/


int main(int argc, char **argv)
{

  int port,ttl;
  char *tmp,*Arg;
  lame_global_flags *gf;
  int iread,imp3;
  FILE *outf;
  int samples_to_encode;
  short int Buffer[2][1152];

  if(argc<=2) {
    rtp_usage();
    exit(1);
  }

  /* process args */
  Arg = argv[1];
  tmp=strchr(Arg,':');

  if (!tmp) {
    rtp_usage();
    exit(1);
  }
  *tmp++=0;
  port=atoi(tmp);
  if (port<=0) {
    rtp_usage();
    exit(1);
  }
  tmp=strchr(tmp,':');
  if (!tmp) {
    rtp_usage();
    exit(1);
  }
  *tmp++=0;
  ttl=atoi(tmp);
  if (tmp<=0) {
    rtp_usage();
    exit(1);
  }
  rtpsocket=makesocket(Arg,port,ttl,&rtpsi);
  srand(getpid() ^ time(0));
  initrtp(&RTPheader);


  /* initialize encoder */
  gf=lame_init();

  /* Remove the argumets that are rtp related, and then 
   * parse the command line arguments, setting various flags in the
   * struct pointed to by 'gf'.  If you want to parse your own arguments,
   * or call libmp3lame from a program which uses a GUI to set arguments,
   * skip this call and set the values of interest in the gf-> struct.  
   */
  lame_parse_args(argc-1, &argv[1]); 

  /* open the output file.  Filename parsed into gf->inPath */
  if (!strcmp(gf->outPath, "-")) {
    outf = stdout;
  } else {
    if ((outf = fopen(gf->outPath, "wb")) == NULL) {
      fprintf(stderr,"Could not create \"%s\".\n", gf->outPath);
      exit(1);
    }
  }


  /* open the wav/aiff/raw pcm or mp3 input file.  This call will
   * open the file with name gf->inFile, try to parse the headers and
   * set gf->samplerate, gf->num_channels, gf->num_samples.
   * if you want to do your own file input, skip this call and set
   * these values yourself.  
   */
  lame_init_infile();


  /* Now that all the options are set, lame needs to analyze them and
   * set some more options 
   */
  lame_init_params();
  lame_print_config();   /* print usefull information about options being used */


  samples_to_encode = gf->encoder_delay + 288;
  /* encode until we hit eof */
  do {
    /* read in gf->framesize samples.  If you are doing your own file input
     * replace this by a call to fill Buffer with exactly gf->framesize sampels */
    iread=lame_readframe(Buffer);
    imp3=lame_encode(Buffer,mp3buffer);  /* encode the frame */
    fwrite(mp3buffer,1,imp3,outf);       /* write the MP3 output to file  */
    rtp_output(mp3buffer,imp3);          /* write MP3 output to RTP port */    
    samples_to_encode += iread - gf->framesize;
  } while (iread);
  
  /* encode until we flush internal buffers.  (Buffer=0 at this point */
  while (samples_to_encode > 0) {
    imp3=lame_encode(Buffer,mp3buffer);
    fwrite(mp3buffer,1,imp3,outf);
    rtp_output(mp3buffer,imp3);
    samples_to_encode -= gf->framesize;
  }
  

  imp3=lame_encode_finish(mp3buffer);   /* may return one more mp3 frame */
  fwrite(mp3buffer,1,imp3,outf);  
  rtp_output(mp3buffer,imp3);
  fclose(outf);
  lame_mp3_tags();                /* add id3 or VBR tags to mp3 file */
  return 0;
}

