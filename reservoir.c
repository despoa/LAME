/**********************************************************************
 * ISO MPEG Audio Subgroup Software Simulation Group (1996)
 * ISO 13818-3 MPEG-2 Audio Encoder - Lower Sampling Frequency Extension
 *
 * $Id$
 *
 * $Log$
 * Revision 1.2  1999/12/17 04:24:07  markt
 * added the --nores option to disable the bitreservoir.  only usefull
 * in special circumstances
 *
 * Revision 1.1.1.1  1999/11/24 08:43:39  markt
 * initial checkin of LAME
 * Starting with LAME 3.57beta with some modifications
 *
 * Revision 1.1  1996/02/14 04:04:23  rowlands
 * Initial revision
 *
 * Received from Mike Coleman
 **********************************************************************/
/*
  Revision History:

  Date        Programmer                Comment
  ==========  ========================= ===============================
  1995/09/06  mc@fivebats.com           created

*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "util.h"
#ifdef HAVEGTK
#include "gtkanal.h"
#endif
#include "globalflags.h"

/*
  Layer3 bit reservoir:
  Described in C.1.5.4.2.2 of the IS
*/

static int ResvSize = 0; /* in bits */
static int ResvMax  = 0; /* in bits */

/*
  ResvFrameBegin:
  Called (repeatedly) at the beginning of a frame. Updates the maximum
  size of the reservoir, and checks to make sure main_data_begin
  was set properly by the formatter
*/
int
ResvFrameBegin( frame_params *fr_ps, III_side_info_t *l3_side, int mean_bits, int frameLength )
{
    layer *info;
    int fullFrameBits, mode_gr;
    int expectedResvSize, resvLimit;

    if (frameNum==0) {
      ResvSize=0;
    }


    info = fr_ps->header;
    if ( info->version == 1 )
    {
	mode_gr = 2;
	resvLimit = 4088; /* main_data_begin has 9 bits in MPEG 1 */
    }
    else
    {
	mode_gr = 1;
	resvLimit = 2040; /* main_data_begin has 8 bits in MPEG 2 */
    }

    /*
      main_data_begin was set by the formatter to the
      expected value for the next call -- this should
      agree with our reservoir size
    */

    expectedResvSize = l3_side->main_data_begin * 8;
#ifdef DEBUG
    fprintf( stderr, ">>> ResvSize = %d\n", ResvSize );
#endif
    assert( expectedResvSize == ResvSize );
    fullFrameBits = mean_bits * mode_gr + ResvSize;

    /*
      determine maximum size of reservoir:
      ResvMax + frameLength <= 7680;
    */
    if ( frameLength > 7680 )
	ResvMax = 0;
    else
	ResvMax = 7680 - frameLength;
    if (gf.disable_reservoir) ResvMax=0;


    /*
      limit max size to resvLimit bits because
      main_data_begin cannot indicate a
      larger value
      */
    if ( ResvMax > resvLimit )
	ResvMax = resvLimit;

#ifdef HAVEGTK
  if (gtkflag){
    pinfo->mean_bits=mean_bits/2;  /* expected bits per channel per granule */
    pinfo->resvsize=ResvSize;
  }
#endif

    return fullFrameBits;
}


/*
  ResvMaxBits2:
  As above, but now it *really* is bits per granule (both channels).  
  Mark Taylor 4/99
*/
void ResvMaxBits2(int mean_bits, int *targ_bits, int *extra_bits, int gr)
{
  int add_bits;
  *targ_bits = mean_bits ;
  /* extra bits if the reservoir is almost full */
  if (ResvSize > ((ResvMax * 9) / 10)) {
    add_bits= ResvSize-((ResvMax * 9) / 10);
    *targ_bits += add_bits;
  }else {
    add_bits =0 ;
    /* build up reservoir.  this builds the reservoir a little slower
     * than FhG.  It could simple be mean_bits/15, but this was rigged
     * to always produce 100 (the old value) at 128kbs */
    *targ_bits -= (int) (mean_bits/15.2);
  }

  
  /* amount from the reservoir we are allowed to use. ISO says 6/10 */
  *extra_bits =    
    (ResvSize  < (ResvMax*6)/10  ? ResvSize : (ResvMax*6)/10);
  *extra_bits -= add_bits;
  
  if (*extra_bits < 0) *extra_bits=0;

  
}

/*
  ResvAdjust:
  Called after a granule's bit allocation. Readjusts the size of
  the reservoir to reflect the granule's usage.
*/
void
ResvAdjust( frame_params *fr_ps, gr_info *gi, III_side_info_t *l3_side, int mean_bits )
{
    ResvSize += (mean_bits / fr_ps->stereo) - gi->part2_3_length;
}


/*
  ResvFrameEnd:
  Called after all granules in a frame have been allocated. Makes sure
  that the reservoir size is within limits, possibly by adding stuffing
  bits. Note that stuffing bits are added by increasing a granule's
  part2_3_length. The bitstream formatter will detect this and write the
  appropriate stuffing bits to the bitstream.
*/
void
ResvFrameEnd( frame_params *fr_ps, III_side_info_t *l3_side, int mean_bits )
{
    int stereo, stuffingBits;
    int over_bits;

    stereo = fr_ps->stereo;

#if 1
    /* just in case mean_bits is odd, this is necessary... */
    if ( (stereo == 2) && (mean_bits & 1) )
	ResvSize += 1;
#endif

    over_bits = ResvSize - ResvMax;
    if ( over_bits < 0 )
	over_bits = 0;
    
    ResvSize -= over_bits;
    stuffingBits = over_bits;

    /* we must be byte aligned */
    if ( (over_bits = ResvSize % 8) )
    {
	stuffingBits += over_bits;
	ResvSize -= over_bits;
    }


    l3_side->resvDrain = stuffingBits;
    return;

}


