#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "lame.h"
#include "rtp.h"


/*

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




char mp3buffer[LAME_MAXMP3BUFFER];
lame_mp3info  mp3info;


/**********************************************************************
 * read one frame and encode it 
 **********************************************************************/
int makeframe(void)
{
  int mp3out,iread;
  static short int Buffer[2][1152];
  static int last_nonzero_iread=0;
  int samples_to_encode;

  iread=lame_readframe(Buffer);
  if (iread>0) {
    last_nonzero_iread=iread;
    mp3out=lame_encode(Buffer,mp3buffer);
    if (mp3out) rtp_output(mp3buffer,mp3out);
    
  }else{
    /* if iread=0, call lame_encode a few more times to flush buffers 
     * amount of buffering in lame_encode = mp3info.encoder_delay + last_frame 
     * and number of samples in last_frame = last_nonzero_iread
     *
     * ALSO, because of the 50% overlap, a 576 MDCT granule decodes to 
     * 1152 samples.  To synthesize the 576 samples centered under this granule
     * we need the previous granule for the first 288 samples (no problem), and
     * the next granule for the next 288 samples (not possible if this is last 
     * granule).  So we need to pad with 288 samples to make sure we can 
     * encode the 576 samples we are interested in.
     */
    samples_to_encode = mp3info.encoder_delay + last_nonzero_iread + 288;
    /* minus samples encoded on last call to lame_encode() */
    samples_to_encode -= mp3info.framesize;

    while (samples_to_encode > 0) {
      mp3out=lame_encode(Buffer,mp3buffer);
      if (mp3out) rtp_output(mp3buffer,mp3out);
      samples_to_encode -= mp3info.framesize;
    }


  }
  return iread;
}




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

  if(argc<=2) {
    fprintf(stderr,"usage: mp3rtp ip:port:ttl  [encoder options] <infile> <outfile>\n");
    exit(1);
  }

  lame_init(0);

  /* process args */
  Arg = argv[1];
  tmp=strchr(Arg,':');

  if (!tmp) {
    fprintf(stderr,"usage: mp3rtp ip:port:ttl\n");
    exit(1);
  }
  *tmp++=0;
  port=atoi(tmp);
  if (port<=0) {
    fprintf(stderr,"usage: mp3rtp  ip:port:ttl\n");
    exit(1);
  }
  tmp=strchr(tmp,':');
  if (!tmp) {
    fprintf(stderr,"usage: mp3rtp ip:port:ttl\n");
    exit(1);
  }
  *tmp++=0;
  ttl=atoi(tmp);
  if (tmp<=0) {
    fprintf(stderr,"usage: mp3rtp ip:port:ttl\n");
    exit(1);
  }
  rtpsocket=makesocket(Arg,port,ttl,&rtpsi);
  srand(getpid() ^ time(0));
  initrtp(&RTPheader);


  lame_parse_args(argc-1, &argv[1]); 
  lame_print_config();
  lame_getmp3info(&mp3info);

  while (makeframe());


  lame_cleanup(mp3buffer);
  return 0;
}

