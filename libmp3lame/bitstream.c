/*
 *	MP3 bitstream Output interface for LAME
 *
 *	Copyright (c) 1999 Takehiro TOMINAGA
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

#define KLEMM_14

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "tables.h"
#include "bitstream.h"
#include "quantize.h"
#include "quantize_pvt.h"
#include "version.h"

/* This is the scfsi_band table from 2.4.2.7 of the IS */
const int scfsi_band[5] = { 0, 6, 11, 16, 21 };


/* unsigned int is at least this large:  */
/* we work with ints, so when doing bit manipulation, we limit
 * ourselves to MAX_LENGTH-2 just to be on the safe side */
#define MAX_LENGTH      32  



#ifdef DEBUG
static int hoge, hogege;
#endif





void putheader_bits(lame_internal_flags *gfc,int w_ptr)
{
    Bit_stream_struc *bs;
    bs = &gfc->bs;
#ifdef DEBUG
    hoge += gfc->sideinfo_len * 8;
    hogege += gfc->sideinfo_len * 8;
#endif
    memcpy(&bs->buf[bs->buf_byte_idx], gfc->header[gfc->w_ptr].buf,
	   gfc->sideinfo_len);
    bs->buf_byte_idx += gfc->sideinfo_len;
    bs->totbit += gfc->sideinfo_len * 8;
    gfc->w_ptr = (gfc->w_ptr + 1) & (MAX_HEADER_BUF - 1);
}




/*write j bits into the bit stream */
INLINE void
putbits2(lame_global_flags *gfp, int val, int j)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    Bit_stream_struc *bs;
    bs = &gfc->bs;

    assert(j < MAX_LENGTH-2);

    while (j > 0) {
	int k;
	if (bs->buf_bit_idx == 0) {
	    bs->buf_bit_idx = 8;
	    bs->buf_byte_idx++;
	    assert(bs->buf_byte_idx < BUFFER_SIZE);
	    assert(gfc->header[gfc->w_ptr].write_timing >= bs->totbit);
	    if (gfc->header[gfc->w_ptr].write_timing == bs->totbit) {
	      putheader_bits(gfc,gfc->w_ptr);
	    }
	    bs->buf[bs->buf_byte_idx] = 0;
	}

	k = Min(j, bs->buf_bit_idx);
	j -= k;
        
	bs->buf_bit_idx -= k;
        
	assert (j < MAX_LENGTH); /* 32 too large on 32 bit machines */
        assert (bs->buf_bit_idx < MAX_LENGTH); 
	
        bs->buf[bs->buf_byte_idx] |= ((val >> j) << bs->buf_bit_idx);
	bs->totbit += k;
    }
}

/*write j bits into the bit stream, ignoring frame headers */
INLINE void
putbits_noheaders(lame_global_flags *gfp, int val, int j)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    Bit_stream_struc *bs;
    bs = &gfc->bs;

    assert(j < MAX_LENGTH-2);

    while (j > 0) {
	int k;
	if (bs->buf_bit_idx == 0) {
	    bs->buf_bit_idx = 8;
	    bs->buf_byte_idx++;
	    assert(bs->buf_byte_idx < BUFFER_SIZE);
	    bs->buf[bs->buf_byte_idx] = 0;
	}

	k = Min(j, bs->buf_bit_idx);
	j -= k;
        
	bs->buf_bit_idx -= k;
        
	assert (j < MAX_LENGTH); /* 32 too large on 32 bit machines */
        assert (bs->buf_bit_idx < MAX_LENGTH); 
	
        bs->buf[bs->buf_byte_idx] |= ((val >> j) << bs->buf_bit_idx);
	bs->totbit += k;
    }
}

#ifdef KLEMM_14 /*======================================================*/

/*
  Some combinations of bitrate, Fs, and stereo make it impossible to stuff
  out a frame using just main_data, due to the limited number of bits to
  indicate main_data_length. In these situations, we put stuffing bits into
  the ancillary data...
*/

INLINE void drain_into_ancillary ( lame_global_flags* const gfp, int remainingBits )
{
    lame_internal_flags*  gfc = gfp->internal_flags;
    char                  buffer [80];
    char*                 p = buffer;
    
    assert (remainingBits >= 0);
    
    sprintf ( p, "LAME%s", get_lame_short_version () );

    for (; *p != '\0'  &&  remainingBits >= 8; p++, remainingBits -= 8 )
        putbits2 ( gfp, *p, 8 );

    for (; remainingBits >= 1; remainingBits -= 1 ) {
        putbits2 ( gfp, gfc->ancillary_flag, 1 );
        gfc->ancillary_flag ^= 1;
    }

    assert (remainingBits == 0);
}


static void  writeheader (
        lame_internal_flags* const  gfc, 
        const unsigned              value, 
        size_t                      bitcount )
{
    static unsigned  tab [2] = { 0, CRC16_POLYNOMIAL };
    int              i;
    int              k;

    assert (bitcount < MAX_LENGTH-2  &&  value < (1lu << bitcount) );

    // Update CRC value
    for ( i = bitcount; i-- > 0; )
        gfc->crcvalue = (gfc->crcvalue << 1) ^ tab [ ((gfc->crcvalue >> 15) ^ (value >> i)) & 1 ];
      
    // Poke value into bitbuffer
    i = gfc->header[gfc->h_ptr].ptr;
    while ( 1 ) {
        if (bitcount < 8 - (i & 7)) {
	    gfc->header [gfc->h_ptr].buf [i >> 3] |= value << (8 - (i & 7) - bitcount);
	    i += bitcount;
	    break;
	} else {
	    bitcount -= (k = 8 - (i & 7));
	    gfc->header [gfc->h_ptr].buf [i >> 3] |= (value >> bitcount) << (8 - (i & 7) - k);
	    i += k;
	}
    }
    gfc->header[gfc->h_ptr].ptr = i;
}

static void  writedummy (
        lame_internal_flags* const  gfc, 
        size_t                      bitcount )
{
    gfc->header[gfc->h_ptr].ptr += bitcount;   // dummy bits without updating CRC and Buffer
}


INLINE void encodeSideInfo2 ( lame_global_flags* const gfp, int bitsPerFrame )
{
    lame_internal_flags *gfc=gfp->internal_flags;
    III_side_info_t *l3_side;
    int gr, ch, band, old;
    
    l3_side = &gfc->l3_side;
    gfc->header[gfc->h_ptr].ptr = 0;
    memset(gfc->header[gfc->h_ptr].buf, 0, gfc->sideinfo_len);
    
    writeheader ( gfc, gfp->out_samplerate >= 16000 ? 0xfff : 0xffe, 12 );
    writeheader ( gfc, gfp->version          , 1 );
    writeheader ( gfc, 4 - 3                 , 2 );
    writeheader ( gfc, !gfp->error_protection, 1 );

    /* Reset CRC generator */
    gfc->crcvalue = 0xFFFF;
    writeheader ( gfc, gfc->bitrate_index    , 4 );
    writeheader ( gfc, gfc->samplerate_index , 2 );
    writeheader ( gfc, gfc->padding          , 1 );
    writeheader ( gfc, gfp->extension        , 1 );
    writeheader ( gfc, gfp->mode             , 2 );
    writeheader ( gfc, gfc->mode_ext         , 2 );
    writeheader ( gfc, gfp->copyright        , 1 );
    writeheader ( gfc, gfp->original         , 1 );
    writeheader ( gfc, gfp->emphasis         , 2 );
    if (gfp->error_protection)
        writedummy ( gfc, 16 );

    if ( gfp->version == 1 ) { /* MPEG1 */
    
	assert(l3_side->main_data_begin >= 0);
	writeheader ( gfc, l3_side->main_data_begin         , 9 );
	writeheader ( gfc, l3_side->private_bits            , gfc->stereo == 2  ?  3  :  5 );

	for (ch = 0; ch < gfc->stereo; ch++) {
	    for (band = 0; band < 4; band++) {
		writeheader ( gfc, l3_side->scfsi[ch][band] , 1 );
	    }
	}

	for (gr = 0; gr < 2; gr++) {
	    for (ch = 0; ch < gfc->stereo; ch++) {
		gr_info *gi = &l3_side->gr[gr].ch[ch].tt;
		writeheader ( gfc, gi->part2_3_length       ,12 );
		writeheader ( gfc, gi->big_values / 2       , 9 );
		writeheader ( gfc, gi->global_gain          , 8 );
		writeheader ( gfc, gi->scalefac_compress    , 4 );
		writeheader ( gfc, gi->window_switching_flag, 1 );

		if (gi->window_switching_flag) {
		    writeheader ( gfc, gi->block_type       , 2 );
		    writeheader ( gfc, gi->mixed_block_flag , 1 );

		    if (gi->table_select[0] == 14)
			gi->table_select[0] = 16;
		    writeheader ( gfc, gi->table_select[0]  , 5 );
		    if (gi->table_select[1] == 14)
			gi->table_select[1] = 16;
		    writeheader ( gfc, gi->table_select[1]  , 5 );

		    writeheader ( gfc, gi->subblock_gain[0] , 3 );
		    writeheader ( gfc, gi->subblock_gain[1] , 3 );
		    writeheader ( gfc, gi->subblock_gain[2] , 3 );
		} else {
		    assert(gi->block_type == NORM_TYPE);
		    if (gi->table_select[0] == 14)
			gi->table_select[0] = 16;
		    writeheader ( gfc, gi->table_select[0]  , 5 );
		    if (gi->table_select[1] == 14)
			gi->table_select[1] = 16;
		    writeheader ( gfc, gi->table_select[1]  , 5 );
		    if (gi->table_select[2] == 14)
			gi->table_select[2] = 16;
		    writeheader ( gfc, gi->table_select[2]  , 5 );

		    assert(gi->region0_count < 16U);
		    assert(gi->region1_count < 8U);
		    writeheader ( gfc, gi->region0_count    , 4 );
		    writeheader ( gfc, gi->region1_count    , 3 );
		}
		writeheader ( gfc, gi->preflag              , 1 );
		writeheader ( gfc, gi->scalefac_scale       , 1 );
		writeheader ( gfc, gi->count1table_select   , 1 );
	    }
	}
    }
    else { /* MPEG2 */
	
	assert(l3_side->main_data_begin >= 0);
	writeheader ( gfc, l3_side->main_data_begin         , 8 );
	writeheader ( gfc, l3_side->private_bits            , gfc->stereo );

	gr = 0;
	for (ch = 0; ch < gfc->stereo; ch++) {
	    gr_info *gi = &l3_side->gr[gr].ch[ch].tt;
	    writeheader ( gfc, gi->part2_3_length           ,12 );
	    writeheader ( gfc, gi->big_values / 2           , 9 );
	    writeheader ( gfc, gi->global_gain              , 8 );
	    writeheader ( gfc, gi->scalefac_compress        , 9 );
	    writeheader ( gfc, gi->window_switching_flag    , 1 );

	    if (gi->window_switching_flag) {
		writeheader ( gfc, gi->block_type           , 2 );
		writeheader ( gfc, gi->mixed_block_flag     , 1 );

		if (gi->table_select[0] == 14)
		    gi->table_select[0] = 16;
		writeheader ( gfc, gi->table_select[0]      , 5 );
		if (gi->table_select[1] == 14)
		    gi->table_select[1] = 16;
		writeheader ( gfc, gi->table_select [1]     , 5 );

		writeheader ( gfc, gi->subblock_gain [0]    , 3 );
		writeheader ( gfc, gi->subblock_gain [1]    , 3 );
		writeheader ( gfc, gi->subblock_gain [2]    , 3 );
	    } else {
		if (gi->table_select[0] == 14)
		    gi->table_select[0] = 16;
		writeheader ( gfc, gi->table_select [0]     , 5 );
		if (gi->table_select[1] == 14)
		    gi->table_select[1] = 16;
		writeheader ( gfc, gi->table_select [1]     , 5 );
		if (gi->table_select[2] == 14)
		    gi->table_select[2] = 16;
		writeheader ( gfc, gi->table_select [2]     , 5 );

		assert(gi->region0_count < 16U);
		assert(gi->region1_count < 8U);
		writeheader ( gfc, gi->region0_count        , 4 );
		writeheader ( gfc, gi->region1_count        , 3 );
	    }

	    writeheader ( gfc, gi->scalefac_scale           , 1 );
	    writeheader ( gfc, gi->count1table_select       , 1 );
	}
    }

    if ( gfp->error_protection ) {
	/* (jo) error_protection: add crc16 information to header */
	gfc->header[gfc->h_ptr].buf [4] = (gfc->crcvalue >> 8) & 0xFF;
	gfc->header[gfc->h_ptr].buf [5] = (gfc->crcvalue >> 0) & 0xFF;
    }


    old = gfc->h_ptr;
    assert (gfc->header[old].ptr == gfc->sideinfo_len * 8);

    gfc->h_ptr                           = (old + 1) & (MAX_HEADER_BUF - 1);
    gfc->header[gfc->h_ptr].write_timing = gfc->header[old].write_timing + bitsPerFrame;

    if ( gfc->h_ptr == gfc->w_ptr ) { /* yikes! we are out of header buffer space */
        ERRORF("Error: MAX_HEADER_BUF too small in bitstream.c \n");
    }
}


static unsigned char klemm_table [] = {
    0xFF, 0x7E, 0xEF, 0x7D, 0x3C, 0x6D, 0xDF, 0x6E, 0xCF,
    0x7B, 0x3A, 0x6B, 0x39, 0x18, 0x29, 0x5B, 0x2A, 0x4B,
    0xBF, 0x5E, 0xAF, 0x5D, 0x2C, 0x4D, 0x9F, 0x4E, 0x8F,
    0x77, 0x36, 0x67, 0x35, 0x14, 0x25, 0x57, 0x26, 0x47,
    0x33, 0x12, 0x23, 0x11, 0x00, 0x01, 0x13, 0x02, 0x03,
    0x37, 0x16, 0x27, 0x15, 0x04, 0x05, 0x17, 0x06, 0x07,
    0x7F, 0x3E, 0x6F, 0x3D, 0x1C, 0x2D, 0x5F, 0x2E, 0x4F,
    0x3B, 0x1A, 0x2B, 0x19, 0x08, 0x09, 0x1B, 0x0A, 0x0B,
    0x3F, 0x1E, 0x2F, 0x1D, 0x0C, 0x0D, 0x1F, 0x0E, 0x0F,
};

/*
 *  This function codes the part 2 of the quantized values.
 *  This is the part consisting only of (-1,0,+1). The part 3 is the part
 *  which is always [0], the part 1 can contain all values from [-8191,+8191]
 */

INLINE int  huffman_coder_count1 ( lame_global_flags* const gfp, const int* ix, gr_info* gi )
{
#ifdef DEBUG
    lame_internal_flags*  gfc    = gfp->internal_flags;
    int                   gegebo = gfc->bs.totbit;
#endif
    /* Write count1 area */
    const struct huffcodetab* h = ht + gi->count1table_select + 32;
    int  bits = 0;
    int  i;
    int  tmp;

    assert (gi->count1table_select < 2u);

    for ( i = (gi->count1 - gi->big_values) / 4, ix += gi->big_values; i > 0; i--, ix += 4 ) {
        assert ( ix[0]+1 <= 2u  &&  ix[1]+1 <= 2u  &&  ix[2]+1 <= 2u  &&  ix[3]+1 <= 2u );
	tmp   = klemm_table [ 40 + 27*ix[0] + 9*ix[1] + 3*ix[2] + ix[3] ];
	bits += h->hlen [tmp & 15];
	putbits2 ( gfp, (tmp >> 4) + h->table [tmp & 15], h->hlen [tmp & 15] );
    }
    
#ifdef DEBUG
    DEBUGF("%ld %d %d %d\n",gfc->bs.totbit -gegebo, gi->count1bits, gi->big_values, gi->count1);
#endif
    return bits;
}

#else /*================================================================*/

/*
  Some combinations of bitrate, Fs, and stereo make it impossible to stuff
  out a frame using just main_data, due to the limited number of bits to
  indicate main_data_length. In these situations, we put stuffing bits into
  the ancillary data...
*/

INLINE void
drain_into_ancillary(lame_global_flags *gfp,int remainingBits)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int i,bits;
    assert(remainingBits >= 0);

    if (remainingBits >= 8) {
      putbits2(gfp,0x4c,8);
      remainingBits -= 8;
    }
    if (remainingBits >= 8) {
      putbits2(gfp,0x41,8);
      remainingBits -= 8;
    }
    if (remainingBits >= 8) {
      putbits2(gfp,0x4d,8);
      remainingBits -= 8;
    }
    if (remainingBits >= 8) {
      putbits2(gfp,0x45,8);
      remainingBits -= 8;
    }
      
    if (remainingBits >= 32) {
      char *version = get_lame_short_version ();
      if (remainingBits >= 32) 
	for (i=0; i<(int)strlen(version) && remainingBits >=8 ; ++i) {
	  remainingBits -= 8;
	  putbits2(gfp,version[i],8);
	}
    }

    /* drain the rest 16 bits at a time */
    bits = remainingBits & (16 - 1);
    for (i=0; i<bits; ++i) {
      putbits2(gfp,gfc->ancillary_flag,1);
      gfc->ancillary_flag = 1-gfc->ancillary_flag;
    }

    remainingBits /= 16;
    for (; remainingBits > 0; remainingBits--) {
      if (gfc->ancillary_flag)
	putbits2(gfp,0xAAAA, 16);
      else
	putbits2(gfp,0x5555, 16);
    }
}

/*write N bits into the header */
INLINE void
writeheader(lame_internal_flags *gfc,int val, int j)
{
    int ptr = gfc->header[gfc->h_ptr].ptr;

    while (j > 0) {
	int k = Min(j, 8 - (ptr & 7));
	j -= k;
        assert (j < MAX_LENGTH); /* >> 32  too large for 32 bit machines */
	gfc->header[gfc->h_ptr].buf[ptr >> 3]
	    |= ((val >> j)) << (8 - (ptr & 7) - k);
	ptr += k;
    }
    gfc->header[gfc->h_ptr].ptr = ptr;
}

#if 1  /* Set to 0 for new CRC function, but speed improvement is around 0.2% */

/* (jo) this wrapper function for BF_addEntry() updates also the crc */
static void
CRC_writeheader(lame_internal_flags *gfc, int value, int length,int *crc)
{
   int bit = 1 << length;

   assert(length < MAX_LENGTH-2);
   
   while((bit >>= 1)){
      *crc <<= 1;
      if (!(*crc & 0x10000) ^ !(value & bit))
	*crc ^= CRC16_POLYNOMIAL;
   }
   *crc &= 0xffff;
   writeheader(gfc,value, length);
}

void main_CRC_init (void) {}

#else

static unsigned short CRC_table [256];

static void CRC_init ( unsigned short polynom )
{
    int            i;
    int            j;
    unsigned long  val;
    
    i = 0;
    do {
        val = 0;
        j = 0x80;
        do {
            val <<= 1;
            if ( !(val & 0x10000) ^ !(i & j) )
		val ^= polynom;
   	} while ( j >>= 1 );
   	CRC_table [i] = val;
    } while ( ++i < 0x100 );
}

static void CRC_writeheader ( lame_internal_flags*  gfc,
                              unsigned int          val, 
                              unsigned              len,
                              unsigned int*         crc )
{
    unsigned long c = *crc;
    
    assert ( len <= 16 );
    assert ( val < (1 << len) );
    assert ( c < 0x10000 );
    
    if ( len > 8 ) {
        c <<= len-8;
        c  ^= CRC_table [ (c>>16) ^ (val >> 8) ];
        c  &= 0xFFFF;
        c <<= 8;
        c  ^= CRC_table [ (c>>16) ^ (val & 0xFF) ];
    } else {
        c <<= len;
        c  ^= CRC_table [ (c>>16) ^ val ];
    }
    
    *crc = c & 0xFFFF;
    writeheader ( gfc, val, len );
}

void main_CRC_init ( void )
{
    CRC_init ( CRC16_POLYNOMIAL );
}

#endif

INLINE void
encodeSideInfo2(lame_global_flags *gfp,int bitsPerFrame)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    III_side_info_t *l3_side;
    int gr, ch;
    int crc;
    
    l3_side = &gfc->l3_side;
    gfc->header[gfc->h_ptr].ptr = 0;
    memset(gfc->header[gfc->h_ptr].buf, 0, gfc->sideinfo_len);
    crc = 0xffff; /* (jo) init crc16 for error_protection */
    if (gfp->out_samplerate < 16000) 
      writeheader(gfc,0xffe,                12);
    else
      writeheader(gfc,0xfff,                12);
    writeheader(gfc,(gfp->version),            1);
    writeheader(gfc,4 - 3,                 2);
    writeheader(gfc,(!gfp->error_protection),  1);
    /* (jo) from now on call the CRC_writeheader() wrapper to update crc */
    CRC_writeheader(gfc,(gfc->bitrate_index),      4,&crc);
    CRC_writeheader(gfc,(gfc->samplerate_index),   2,&crc);
    CRC_writeheader(gfc,(gfc->padding),            1,&crc);
    CRC_writeheader(gfc,(gfp->extension),          1,&crc);
    CRC_writeheader(gfc,(gfp->mode),               2,&crc);
    CRC_writeheader(gfc,(gfc->mode_ext),           2,&crc);
    CRC_writeheader(gfc,(gfp->copyright),          1,&crc);
    CRC_writeheader(gfc,(gfp->original),           1,&crc);
    CRC_writeheader(gfc,(gfp->emphasis),           2,&crc);
    if (gfp->error_protection) {
	writeheader(gfc,0, 16); /* dummy */
    }

    if (gfp->version == 1) {
	/* MPEG1 */
	assert(l3_side->main_data_begin >= 0);
	CRC_writeheader(gfc,(l3_side->main_data_begin), 9,&crc);

	if (gfc->stereo == 2)
	    CRC_writeheader(gfc,l3_side->private_bits, 3,&crc);
	else
	    CRC_writeheader(gfc,l3_side->private_bits, 5,&crc);

	for (ch = 0; ch < gfc->stereo; ch++) {
	    int band;
	    for (band = 0; band < 4; band++) {
		CRC_writeheader(gfc,l3_side->scfsi[ch][band], 1,&crc);
	    }
	}

	for (gr = 0; gr < 2; gr++) {
	    for (ch = 0; ch < gfc->stereo; ch++) {
		gr_info *gi = &l3_side->gr[gr].ch[ch].tt;
		CRC_writeheader(gfc,gi->part2_3_length,       12,&crc);
		CRC_writeheader(gfc,gi->big_values / 2,        9,&crc);
		CRC_writeheader(gfc,gi->global_gain,           8,&crc);
		CRC_writeheader(gfc,gi->scalefac_compress,     4,&crc);
		CRC_writeheader(gfc,gi->window_switching_flag, 1,&crc);

		if (gi->window_switching_flag) {
		    CRC_writeheader(gfc,gi->block_type,       2,&crc);
		    CRC_writeheader(gfc,gi->mixed_block_flag, 1,&crc);

		    if (gi->table_select[0] == 14)
			gi->table_select[0] = 16;
		    CRC_writeheader(gfc,gi->table_select[0],  5,&crc);
		    if (gi->table_select[1] == 14)
			gi->table_select[1] = 16;
		    CRC_writeheader(gfc,gi->table_select[1],  5,&crc);

		    CRC_writeheader(gfc,gi->subblock_gain[0], 3,&crc);
		    CRC_writeheader(gfc,gi->subblock_gain[1], 3,&crc);
		    CRC_writeheader(gfc,gi->subblock_gain[2], 3,&crc);
		} else {
		    assert(gi->block_type == NORM_TYPE);
		    if (gi->table_select[0] == 14)
			gi->table_select[0] = 16;
		    CRC_writeheader(gfc,gi->table_select[0], 5,&crc);
		    if (gi->table_select[1] == 14)
			gi->table_select[1] = 16;
		    CRC_writeheader(gfc,gi->table_select[1], 5,&crc);
		    if (gi->table_select[2] == 14)
			gi->table_select[2] = 16;
		    CRC_writeheader(gfc,gi->table_select[2], 5,&crc);

		    assert(gi->region0_count < 16U);
		    assert(gi->region1_count < 8U);
		    CRC_writeheader(gfc,gi->region0_count, 4,&crc);
		    CRC_writeheader(gfc,gi->region1_count, 3,&crc);
		}
		CRC_writeheader(gfc,gi->preflag,            1,&crc);
		CRC_writeheader(gfc,gi->scalefac_scale,     1,&crc);
		CRC_writeheader(gfc,gi->count1table_select, 1,&crc);
	    }
	}
    } else {
	/* MPEG2 */
	assert(l3_side->main_data_begin >= 0);
	CRC_writeheader(gfc,(l3_side->main_data_begin), 8,&crc);
	CRC_writeheader(gfc,l3_side->private_bits, gfc->stereo,&crc);

	gr = 0;
	for (ch = 0; ch < gfc->stereo; ch++) {
	    gr_info *gi = &l3_side->gr[gr].ch[ch].tt;
	    CRC_writeheader(gfc,gi->part2_3_length,       12,&crc);
	    CRC_writeheader(gfc,gi->big_values / 2,        9,&crc);
	    CRC_writeheader(gfc,gi->global_gain,           8,&crc);
	    CRC_writeheader(gfc,gi->scalefac_compress,     9,&crc);
	    CRC_writeheader(gfc,gi->window_switching_flag, 1,&crc);

	    if (gi->window_switching_flag) {
		CRC_writeheader(gfc,gi->block_type,       2,&crc);
		CRC_writeheader(gfc,gi->mixed_block_flag, 1,&crc);

		if (gi->table_select[0] == 14)
		    gi->table_select[0] = 16;
		CRC_writeheader(gfc,gi->table_select[0],  5,&crc);
		if (gi->table_select[1] == 14)
		    gi->table_select[1] = 16;
		CRC_writeheader(gfc,gi->table_select[1],  5,&crc);

		CRC_writeheader(gfc,gi->subblock_gain[0], 3,&crc);
		CRC_writeheader(gfc,gi->subblock_gain[1], 3,&crc);
		CRC_writeheader(gfc,gi->subblock_gain[2], 3,&crc);
	    } else {
		if (gi->table_select[0] == 14)
		    gi->table_select[0] = 16;
		CRC_writeheader(gfc,gi->table_select[0], 5,&crc);
		if (gi->table_select[1] == 14)
		    gi->table_select[1] = 16;
		CRC_writeheader(gfc,gi->table_select[1], 5,&crc);
		if (gi->table_select[2] == 14)
		    gi->table_select[2] = 16;
		CRC_writeheader(gfc,gi->table_select[2], 5,&crc);

		assert(gi->region0_count < 16U);
		assert(gi->region1_count < 8U);
		CRC_writeheader(gfc,gi->region0_count, 4,&crc);
		CRC_writeheader(gfc,gi->region1_count, 3,&crc);
	    }

	    CRC_writeheader(gfc,gi->scalefac_scale,     1,&crc);
	    CRC_writeheader(gfc,gi->count1table_select, 1,&crc);
	}
    }

    if (gfp->error_protection) {
	/* (jo) error_protection: add crc16 information to header */
	gfc->header[gfc->h_ptr].buf[4] = crc >> 8;
	gfc->header[gfc->h_ptr].buf[5] = crc & 255;
    }

    {
	int old = gfc->h_ptr;
	assert(gfc->header[old].ptr == gfc->sideinfo_len * 8);

	gfc->h_ptr = (old + 1) & (MAX_HEADER_BUF - 1);
	gfc->header[gfc->h_ptr].write_timing =
	    gfc->header[old].write_timing + bitsPerFrame;

	if (gfc->h_ptr == gfc->w_ptr) {
	  /* yikes! we are out of header buffer space */
	  ERRORF("Error: MAX_HEADER_BUF too small in bitstream.c \n");
	}

    }
}


INLINE int
huffman_coder_count1(lame_global_flags *gfp,int *ix, gr_info *gi)
{
#ifdef DEBUG
    lame_internal_flags *gfc = gfp->internal_flags;
#endif
    /* Write count1 area */
    const struct huffcodetab *h = &ht[gi->count1table_select + 32];
    int i,bits=0;
#ifdef DEBUG
    int gegebo = gfc->bs.totbit;
#endif

    ix += gi->big_values;
    assert(gi->count1table_select < 2);


    for (i = (gi->count1 - gi->big_values) / 4; i > 0; --i) {
	int huffbits = 0;
	int p = 0, v;

	v = ix[0];
	if (v) {
	    p += 8;
	    if (v < 0)
		huffbits++;
	    assert(-1 <= v && v <= 1);
	}

	v = ix[1];
	if (v) {
	    p += 4;
	    huffbits *= 2;
	    if (v < 0)
		huffbits++;
	    assert(-1 <= v && v <= 1);
	}

	v = ix[2];
	if (v) {
	    p += 2;
	    huffbits *= 2;
	    if (v < 0)
		huffbits++;
	    assert(-1 <= v && v <= 1);
	}

	v = ix[3];
	if (v) {
	    p++;
	    huffbits *= 2;
	    if (v < 0)
		huffbits++;
	    assert(-1 <= v && v <= 1);
	}

	ix += 4;
	putbits2(gfp,huffbits + h->table[p], h->hlen[p]);
	bits += h->hlen[p];
    }
#ifdef DEBUG
    DEBUGF("%ld %d %d %d\n",gfc->bs.totbit -gegebo, gi->count1bits, gi->big_values, gi->count1);
#endif
    return bits;
}


#endif /*====================================================================*/

/*
  Implements the pseudocode of page 98 of the IS
  */

INLINE int
HuffmanCode(lame_global_flags *gfp, int table_select, int x, int y)
{
  /*    lame_internal_flags *gfc=gfp->internal_flags;*/
    int code, ext, xlen;
    int cbits, xbits;
    int signx, signy, linbits;
    const struct huffcodetab *h;

    cbits = 0;
    xbits = 0;
    code  = 0;
    signx = signy = 0;

    if (x < 0) {
	signx++;
	x = -x;
    }

    if (y < 0) {
	signy++;
	y = -y;
    }

    assert(table_select>0);
    h       = ht + table_select;
    linbits = h->xlen;
    ext     = signx;
    xlen    = h->xlen;

    if (table_select > 15) {
	/* use ESC-words */
	if (x > 14) {
	    int linbitsx = x - 15;
	    assert(linbitsx <= h->linmax);
	    ext |= linbitsx << 1;
	    xbits = linbits;
	    x = 15;
	}

	if (y > 14) {
	    int linbitsy = y - 15;
	    assert(linbitsy <= h->linmax);
	    ext <<= linbits;
	    ext |= linbitsy;
	    xbits += linbits;
	    y = 15;
	}
	xlen = 16;
    }

    if (x != 0) {
	cbits--;
    }

    if (y != 0) {
	ext <<= 1;
	ext |= signy;
	cbits--;
    }

    xbits -= cbits;

    assert ( (x|y) < 16u );

    x = x * xlen + y;

    code = h->table[x];
    cbits += h->hlen[x];

    assert(cbits <= MAX_LENGTH);
    assert(xbits <= MAX_LENGTH);

    putbits2(gfp,code, cbits);
    putbits2(gfp, ext, xbits);
    return cbits+xbits;
}

static int
Huffmancodebits(lame_global_flags *gfp, int tableindex, int start, int end, int *ix)
{
    int i,bits;

    assert(tableindex < 32);
    if (!tableindex) return 0;

    bits=0;
    for (i = start; i < end; i += 2) {
      bits +=HuffmanCode(gfp,tableindex, ix[i], ix[i + 1]);
    }
    return bits;
}



/*
  Note the discussion of huffmancodebits() on pages 28
  and 29 of the IS, as well as the definitions of the side
  information on pages 26 and 27.
  */
static int
ShortHuffmancodebits(lame_global_flags *gfp,int *ix, gr_info *gi)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int bits;
    int region1Start;
    
    region1Start = 3*gfc->scalefac_band.s[3];
    if (region1Start > gi->big_values) 	
        region1Start = gi->big_values;

    /* short blocks do not have a region2 */
    bits  = Huffmancodebits(gfp,gi->table_select[0], 0, region1Start, ix);
    bits += Huffmancodebits(gfp,gi->table_select[1], region1Start, gi->big_values, ix);
    return bits;
}

static int
LongHuffmancodebits(lame_global_flags *gfp,int *ix, gr_info *gi)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int i, bigvalues,bits=0;
    int region1Start, region2Start;

    bigvalues = gi->big_values;
    assert(0 <= bigvalues && bigvalues <= 576);

    i = gi->region0_count + 1;
    assert(i < 23);
    region1Start = gfc->scalefac_band.l[i];
    i += gi->region1_count + 1;
    assert(i < 23);
    region2Start = gfc->scalefac_band.l[i];

    if (region1Start > bigvalues)
	region1Start = bigvalues;

    if (region2Start > bigvalues)
	region2Start = bigvalues;

    bits +=Huffmancodebits(gfp,gi->table_select[0], 0, region1Start, ix);
    bits +=Huffmancodebits(gfp,gi->table_select[1], region1Start, region2Start, ix);
    bits +=Huffmancodebits(gfp,gi->table_select[2], region2Start, bigvalues, ix);
    return bits;
}

INLINE int
writeMainData ( lame_global_flags * const gfp,
        int              l3_enc   [2] [2] [576],
        III_scalefac_t   scalefac [2] [2] )
{
    int gr, ch, sfb,data_bits,scale_bits,tot_bits=0;
    lame_internal_flags *gfc=gfp->internal_flags;
    III_side_info_t *l3_side;

    l3_side = &gfc->l3_side;
    if (gfp->version == 1) {
	/* MPEG 1 */
	for (gr = 0; gr < 2; gr++) {
	    for (ch = 0; ch < gfc->stereo; ch++) {
		gr_info *gi = &l3_side->gr[gr].ch[ch].tt;
		int slen1 = slen1_tab[gi->scalefac_compress];
		int slen2 = slen2_tab[gi->scalefac_compress];
		data_bits=0;
		scale_bits=0;

#ifdef DEBUG
		hogege = gfc->bs.totbit;
#endif
		if (gi->block_type == SHORT_TYPE) {
		    for (sfb = 0; sfb < SBPSY_s; sfb++) {
			int slen = sfb < 6 ? slen1 : slen2;

			assert(scalefac[gr][ch].s[sfb][0]>=0);
			assert(scalefac[gr][ch].s[sfb][1]>=0);
			assert(scalefac[gr][ch].s[sfb][2]>=0);

			putbits2(gfp,scalefac[gr][ch].s[sfb][0], slen);
			putbits2(gfp,scalefac[gr][ch].s[sfb][1], slen);
			putbits2(gfp,scalefac[gr][ch].s[sfb][2], slen);
			scale_bits += 3*slen;
		    }
		    data_bits += ShortHuffmancodebits(gfp,l3_enc[gr][ch], gi);
		} else {
		    int i;
		    for (i = 0; i < sizeof(scfsi_band) / sizeof(int) - 1;
			 i++) {
			if (gr != 0 && l3_side->scfsi[ch][i])
			    continue;

			for (sfb = scfsi_band[i]; sfb < scfsi_band[i + 1];
			     sfb++) {

			    assert(scalefac[gr][ch].l[sfb]>=0);
			    putbits2(gfp,scalefac[gr][ch].l[sfb],
				    sfb < 11 ? slen1 : slen2);
			    scale_bits += sfb < 11 ? slen1 : slen2;
			}
		    }
		    data_bits +=LongHuffmancodebits(gfp,l3_enc[gr][ch], gi);
		}
		data_bits +=huffman_coder_count1(gfp,l3_enc[gr][ch], gi);
#ifdef DEBUG
		DEBUGF("<%ld> ", gfc->bs.totbit-hogege);
#endif
		/* does bitcount in quantize.c agree with actual bit count?*/
		assert(data_bits==gi->part2_3_length-gi->part2_length);
		assert(scale_bits==gi->part2_length);
		tot_bits += scale_bits + data_bits;

	    } /* for ch */
	} /* for gr */
    } else {
	/* MPEG 2 */
	gr = 0;
	for (ch = 0; ch < gfc->stereo; ch++) {
	    gr_info *gi = &l3_side->gr[gr].ch[ch].tt;
	    int i, sfb_partition;
	    assert(gi->sfb_partition_table);
	    data_bits = 0;
	    scale_bits=0;

	    sfb = 0;
	    sfb_partition = 0;
	    if (gi->block_type == SHORT_TYPE) {
		for (; sfb_partition < 4; sfb_partition++) {
		    int sfbs = gi->sfb_partition_table[sfb_partition] / 3;
		    int slen = gi->slen[sfb_partition];
		    for (i = 0; i < sfbs; i++, sfb++) {
			putbits2(gfp,Max(scalefac[gr][ch].s[sfb][0], 0U), slen);
			putbits2(gfp,Max(scalefac[gr][ch].s[sfb][1], 0U), slen);
			putbits2(gfp,Max(scalefac[gr][ch].s[sfb][2], 0U), slen);
			scale_bits += 3*slen;
		    }
		}
		data_bits += ShortHuffmancodebits(gfp,l3_enc[gr][ch], gi);
	    } else {
		for (; sfb_partition < 4; sfb_partition++) {
		    int sfbs = gi->sfb_partition_table[sfb_partition];
		    int slen = gi->slen[sfb_partition];
		    for (i = 0; i < sfbs; i++, sfb++) {
			putbits2(gfp,Max(scalefac[gr][ch].l[sfb], 0U), slen);
			scale_bits += slen;
		    }
		}
		data_bits +=LongHuffmancodebits(gfp,l3_enc[gr][ch], gi);
	    }
	    data_bits +=huffman_coder_count1(gfp,l3_enc[gr][ch], gi);

	    /* does bitcount in quantize.c agree with actual bit count?*/
	    assert(data_bits==gi->part2_3_length-gi->part2_length);
	    assert(scale_bits==gi->part2_length);
	    tot_bits += scale_bits + data_bits;
	} /* for ch */
    } /* for gf */
    return tot_bits;
} /* main_data */




void
flush_bitstream(lame_global_flags *gfp)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  int flushbits,remaining_headers;
  int bitsPerFrame, mean_bits;
  int last_ptr,first_ptr;
  first_ptr=gfc->w_ptr;           /* first header to add to bitstream */
  last_ptr = gfc->h_ptr - 1;   /* last header to add to bitstream */
  if (last_ptr==-1) last_ptr=MAX_HEADER_BUF-1;   

  /* add this many bits to bitstream so we can flush all headers */
  flushbits = gfc->header[last_ptr].write_timing - gfc->bs.totbit;

  if (flushbits >= 0) {
    /* if flushbits >= 0, some headers have not yet been written */
    /* reduce flushbits by the size of the headers */
    remaining_headers= 1+last_ptr - first_ptr;
    if (last_ptr < first_ptr) 
      remaining_headers= 1+last_ptr - first_ptr + MAX_HEADER_BUF;
    flushbits -= remaining_headers*8*gfc->sideinfo_len;
  }


  /* finally, add some bits so that the last frame is complete
   * these bits are not necessary to decode the last frame, but
   * some decoders will ignore last frame if these bits are missing 
   */
  getframebits(gfp,&bitsPerFrame,&mean_bits);
  flushbits += bitsPerFrame;
  if (flushbits<0) {
#if 0
    /* if flushbits < 0, this would mean that the buffer looks like:
     * (data...)  last_header  (data...)  (extra data that should not be here...)
     */
    DEBUGF("last header write_timing = %i \n",gfc->header[last_ptr].write_timing);
    DEBUGF("first header write_timing = %i \n",gfc->header[first_ptr].write_timing);
    DEBUGF("bs.totbit:                 %i \n",gfc->bs.totbit);
    DEBUGF("first_ptr, last_ptr        %i %i \n",first_ptr,last_ptr);
    DEBUGF("remaining_headers =        %i \n",remaining_headers);
    DEBUGF("bitsperframe:              %i \n",bitsPerFrame);
    DEBUGF("sidelen:                   %i \n",gfc->sideinfo_len);
#endif
    ERRORF("strange error flushing buffer ... \n");
  } else {
    drain_into_ancillary(gfp,flushbits);
  }

  assert (gfc->header[last_ptr].write_timing + bitsPerFrame  == gfc->bs.totbit);
}




void  add_dummy_byte ( lame_global_flags* const gfp, unsigned char val )
{
  lame_internal_flags *gfc = gfp->internal_flags;
  int i;

  putbits_noheaders(gfp,val,8);   

  for (i=0 ; i< MAX_HEADER_BUF ; ++i) 
    gfc->header[i].write_timing += 8;
}


/*
  format_bitstream()

  This is called after a frame of audio has been quantized and coded.
  It will write the encoded audio to the bitstream. Note that
  from a layer3 encoder's perspective the bit stream is primarily
  a series of main_data() blocks, with header and side information
  inserted at the proper locations to maintain framing. (See Figure A.7
  in the IS).
  */
int
format_bitstream(lame_global_flags *gfp, int bitsPerFrame,
      int              l3_enc[2][2][576],
  	III_scalefac_t   scalefac[2][2] )
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int bits;
    III_side_info_t *l3_side;
    l3_side = &gfc->l3_side;

    drain_into_ancillary(gfp,l3_side->resvDrain_pre);

    encodeSideInfo2(gfp,bitsPerFrame);
    bits = 8*gfc->sideinfo_len;
    bits+=writeMainData(gfp,l3_enc,scalefac);
    drain_into_ancillary(gfp,l3_side->resvDrain_post);
    bits += l3_side->resvDrain_post;

    l3_side->main_data_begin += (bitsPerFrame-bits)/8;
    if ((l3_side->main_data_begin * 8) != gfc->ResvSize ) {
      ERRORF("bit reservoir error: \n"
             "l3_side->main_data_begin: %i \n"
             "Resvoir size:             %i \n"
             "resv drain (post)         %i \n"
             "resv drain (pre)          %i \n"
             "header and sideinfo:      %i \n"
             "data bits:                %i \n"
             "total bits:               %i (remainder: %i) \n"
             "bitsperframe:             %i \n",

             8*l3_side->main_data_begin,
             gfc->ResvSize,
             l3_side->resvDrain_post,
             l3_side->resvDrain_pre,
             8*gfc->sideinfo_len,
             bits-l3_side->resvDrain_post-8*gfc->sideinfo_len,
             bits, bits % 8,
             bitsPerFrame
      );

      gfc->ResvSize = l3_side->main_data_begin*8;
    };
    assert(gfc->bs.totbit % 8 == 0);

    if (gfc->bs.totbit > 1000000000  ) {
      /* to avoid totbit overflow, (at 8h encoding at 128kbs) lets reset bit counter*/
      int i;
      for (i=0 ; i< MAX_HEADER_BUF ; ++i) 
	gfc->header[i].write_timing -= gfc->bs.totbit;      
      gfc->bs.totbit=0;
    }
    return 0;
}


int copy_buffer(char *buffer,int size,Bit_stream_struc *bs)
{
  int minimum = bs->buf_byte_idx + 1;
  if (minimum <= 0) return 0;
  if (size!=0 && minimum>size) return -1; /* buffer is too small */
  memcpy(buffer,bs->buf,minimum);
  bs->buf_byte_idx = -1;
  bs->buf_bit_idx = 0;
  return minimum;
}


void init_bit_stream_w(lame_internal_flags *gfc)
{
   gfc->bs.buf = (unsigned char *)       malloc(BUFFER_SIZE);
   gfc->bs.buf_size = BUFFER_SIZE;

   gfc->h_ptr = gfc->w_ptr = 0;
   gfc->header[gfc->h_ptr].write_timing = 0;
   gfc->bs.buf_byte_idx = -1;
   gfc->bs.buf_bit_idx = 0;
   gfc->bs.totbit = 0;
}

/* end of bitstream.c */
