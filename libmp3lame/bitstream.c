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
 *
 * $Id$
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "bitstream.h"
#include "version.h"
#include "VbrTag.h"
#include "tables.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/* unsigned int is at least this large:  */
/* we work with ints, so when doing bit manipulation, we limit
 * ourselves to MAX_LENGTH-2 just to be on the safe side */
#define MAX_LENGTH      32  



#ifdef DEBUG
static int hoge, hogege;
#endif





/***********************************************************************
 * compute bitsperframe and mean_bits for a layer III frame 
 **********************************************************************/
int getframebits(const lame_global_flags * gfp)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int  bit_rate;

    /* get bitrate in kbps */
    if (gfc->bitrate_index) 
	bit_rate = bitrate_table[gfp->version][gfc->bitrate_index];
    else
	bit_rate = gfp->mean_bitrate_kbps;
    assert ( bit_rate <= 550 );

    /* main encoding routine toggles padding on and off */
    /* one Layer3 Slot consists of 8 bits */
    return 8 * (gfc->mode_gr*(576/8)*1000*bit_rate / gfp->out_samplerate
		+ gfc->padding);
}




static void
putheader_bits(lame_internal_flags *gfc,int w_ptr)
{
    Bit_stream_struc *bs = &gfc->bs;
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
inline static void
putbits2(lame_internal_flags *gfc, int val, int j)
{
    Bit_stream_struc *bs = &gfc->bs;

    assert(j < MAX_LENGTH-2);

    while (j > 0) {
	int k;
	if (bs->buf_bit_idx == 0) {
	    bs->buf_bit_idx = 8;
	    bs->buf_byte_idx++;
	    assert(bs->buf_byte_idx < BUFFER_SIZE);
	    assert(gfc->header[gfc->w_ptr].write_timing >= bs->totbit);
	    if (gfc->header[gfc->w_ptr].write_timing == bs->totbit)
		putheader_bits(gfc,gfc->w_ptr);
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
inline static void
putbits_noheaders(lame_internal_flags *gfc, int val, int j)
{
    Bit_stream_struc *bs = &gfc->bs;

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


/*
  Some combinations of bitrate, Fs, and stereo make it impossible to stuff
  out a frame using just main_data, due to the limited number of bits to
  indicate main_data_length. In these situations, we put stuffing bits into
  the ancillary data...
*/

inline static void
drain_into_ancillary(lame_global_flags *gfp, int remainingBits)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int i, pad;
    assert(remainingBits >= 0);

    if (remainingBits >= 8) {
	putbits2(gfc,0x4c,8);
	remainingBits -= 8;
    }
    if (remainingBits >= 8) {
	putbits2(gfc,0x41,8);
	remainingBits -= 8;
    }
    if (remainingBits >= 8) {
	putbits2(gfc,0x4d,8);
	remainingBits -= 8;
    }
    if (remainingBits >= 8) {
	putbits2(gfc,0x45,8);
	remainingBits -= 8;
    }

    pad = 0xaa;
    if (gfp->disable_reservoir)
	pad = 0;
    if (remainingBits >= 8) {
	const char *version = get_lame_short_version ();
	for (i=0; remainingBits >=8 ; ++i) {
	    if (i < strlen(version))
		putbits2(gfc,version[i],8);
	    else
		putbits2(gfc,pad,8);
	    remainingBits -= 8;
	}
    }

    putbits2(gfc, pad >> (8 - remainingBits), remainingBits);
}

/*write N bits into the header */
inline static void
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


static int
CRC_update(int value, int crc)
{
    int i;
    value <<= 8;
    for (i = 0; i < 8; i++) {
	value <<= 1;
	crc <<= 1;

	if (((crc ^ value) & 0x10000))
	    crc ^= CRC16_POLYNOMIAL;
    }
    return crc;
}


void
CRC_writeheader(lame_internal_flags *gfc, char *header)
{
    int crc = 0xffff; /* (jo) init crc16 for error_protection */
    int i;

    crc = CRC_update(((unsigned char*)header)[2], crc);
    crc = CRC_update(((unsigned char*)header)[3], crc);
    for (i = 6; i < gfc->sideinfo_len; i++) {
	crc = CRC_update(((unsigned char*)header)[i], crc);
    }

    header[4] = crc >> 8;
    header[5] = crc & 255;
}

inline static void
encodeSideInfo2(lame_global_flags *gfp,int bitsPerFrame)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    III_side_info_t *l3_side;
    int gr, ch;
    
    l3_side = &gfc->l3_side;
    gfc->header[gfc->h_ptr].ptr = 0;
    memset(gfc->header[gfc->h_ptr].buf, 0, gfc->sideinfo_len);
    if (gfp->out_samplerate < 16000) 
      writeheader(gfc,0xffe,                12);
    else
      writeheader(gfc,0xfff,                12);
    writeheader(gfc,(gfp->version),            1);
    writeheader(gfc,4 - 3,                 2);
    writeheader(gfc,(!gfp->error_protection),  1);
    writeheader(gfc,(gfc->bitrate_index),      4);
    writeheader(gfc,(gfc->samplerate_index),   2);
    writeheader(gfc,(gfc->padding),            1);
    writeheader(gfc,(gfp->extension),          1);
    writeheader(gfc,(gfp->mode),               2);
    writeheader(gfc,(gfc->mode_ext),           2);
    writeheader(gfc,(gfp->copyright),          1);
    writeheader(gfc,(gfp->original),           1);
    writeheader(gfc,(gfp->emphasis),           2);
    if (gfp->error_protection) {
	writeheader(gfc,0, 16); /* dummy */
    }

    assert(l3_side->main_data_begin >= 0);
    if (gfp->version == 1) {
	/* MPEG1 */
	writeheader(gfc, l3_side->main_data_begin, 9);
	writeheader(gfc,l3_side->private_bits, 7 - gfc->channels_out*2);

	for (ch = 0; ch < gfc->channels_out; ch++) {
	    int band;
	    for (band = 0; band < 4; band++) {
		writeheader(gfc,l3_side->scfsi[ch][band], 1);
	    }
	}
	for (gr = 0; gr < 2; gr++) {
	    for (ch = 0; ch < gfc->channels_out; ch++) {
		gr_info *gi = &l3_side->tt[gr][ch];
		writeheader(gfc,gi->part2_3_length+gi->part2_length, 12);
		writeheader(gfc,gi->big_values / 2,        9);
		writeheader(gfc,gi->global_gain,           8);
		writeheader(gfc,gi->scalefac_compress,     4);

		if (gi->block_type != NORM_TYPE) {
		    writeheader(gfc, 1, 1); /* window_switching_flag */
		    writeheader(gfc,gi->block_type,       2);
		    writeheader(gfc,gi->mixed_block_flag, 1);

		    if (gi->table_select[0] == 14)
			gi->table_select[0] = 16;
		    writeheader(gfc,gi->table_select[0],  5);
		    if (gi->table_select[1] == 14)
			gi->table_select[1] = 16;
		    writeheader(gfc,gi->table_select[1],  5);

		    writeheader(gfc,gi->subblock_gain[0], 3);
		    writeheader(gfc,gi->subblock_gain[1], 3);
		    writeheader(gfc,gi->subblock_gain[2], 3);
		} else {
		    writeheader(gfc, 0, 1); /* window_switching_flag */
		    if (gi->table_select[0] == 14)
			gi->table_select[0] = 16;
		    writeheader(gfc,gi->table_select[0], 5);
		    if (gi->table_select[1] == 14)
			gi->table_select[1] = 16;
		    writeheader(gfc,gi->table_select[1], 5);
		    if (gi->table_select[2] == 14)
			gi->table_select[2] = 16;
		    writeheader(gfc,gi->table_select[2], 5);

		    assert(gi->region0_count < 16U);
		    assert(gi->region1_count < 8U);
		    writeheader(gfc,gi->region0_count, 4);
		    writeheader(gfc,gi->region1_count, 3);
		}
		writeheader(gfc,gi->preflag,            1);
		writeheader(gfc,gi->scalefac_scale,     1);
		writeheader(gfc,gi->count1table_select, 1);
	    }
	}
    } else {
	/* MPEG2 */
	writeheader(gfc, l3_side->main_data_begin, 8);
	writeheader(gfc, l3_side->private_bits, gfc->channels_out);

	for (ch = 0; ch < gfc->channels_out; ch++) {
	    gr_info *gi = &l3_side->tt[0][ch];
	    int part;
	    writeheader(gfc,gi->part2_3_length+gi->part2_length, 12);
	    writeheader(gfc,gi->big_values / 2,        9);
	    writeheader(gfc,gi->global_gain,           8);

	    /* set scalefac_compress */
	    for (part = 0; part < 4; part++)
		gi->slen[part] = log2tab[gi->slen[part]];
	    switch (gi->scalefac_compress) {
	    case 0:
		gi->scalefac_compress
		    = gi->slen[0]*80 + gi->slen[1]*16 + gi->slen[2]*4 + gi->slen[3];
		break;
	    case 1:
		gi->scalefac_compress
		    = 400 + gi->slen[0]*20 + gi->slen[1]*4 + gi->slen[2];
		break;
	    case 2:
		gi->scalefac_compress = 500 + gi->slen[0]*3 + gi->slen[1];
		break;
	    default:
		ERRORF(gfc, "intensity stereo not implemented yet\n" );
		break;
	    }
	    writeheader(gfc,gi->scalefac_compress,     9);

	    if (gi->block_type != NORM_TYPE) {
		writeheader(gfc, 1, 1); /* window_switching_flag */
		writeheader(gfc,gi->block_type,       2);
		writeheader(gfc,gi->mixed_block_flag, 1);

		if (gi->table_select[0] == 14)
		    gi->table_select[0] = 16;
		writeheader(gfc,gi->table_select[0],  5);
		if (gi->table_select[1] == 14)
		    gi->table_select[1] = 16;
		writeheader(gfc,gi->table_select[1],  5);

		writeheader(gfc,gi->subblock_gain[0], 3);
		writeheader(gfc,gi->subblock_gain[1], 3);
		writeheader(gfc,gi->subblock_gain[2], 3);
	    } else {
		writeheader(gfc, 0, 1); /* window_switching_flag */
		if (gi->table_select[0] == 14)
		    gi->table_select[0] = 16;
		writeheader(gfc,gi->table_select[0], 5);
		if (gi->table_select[1] == 14)
		    gi->table_select[1] = 16;
		writeheader(gfc,gi->table_select[1], 5);
		if (gi->table_select[2] == 14)
		    gi->table_select[2] = 16;
		writeheader(gfc,gi->table_select[2], 5);

		assert(gi->region0_count < 16U);
		assert(gi->region1_count < 8U);
		writeheader(gfc,gi->region0_count, 4);
		writeheader(gfc,gi->region1_count, 3);
	    }

	    writeheader(gfc,gi->scalefac_scale,     1);
	    writeheader(gfc,gi->count1table_select, 1);
	}
    }

    if (gfp->error_protection) {
	/* (jo) error_protection: add crc16 information to header */
	CRC_writeheader(gfc, gfc->header[gfc->h_ptr].buf);
    }

    {
	int old = gfc->h_ptr;
	assert(gfc->header[old].ptr == gfc->sideinfo_len * 8);

	gfc->h_ptr = (old + 1) & (MAX_HEADER_BUF - 1);
	gfc->header[gfc->h_ptr].write_timing =
	    gfc->header[old].write_timing + bitsPerFrame;

	if (gfc->h_ptr == gfc->w_ptr) {
	  /* yikes! we are out of header buffer space */
	  ERRORF(gfc,"Error: MAX_HEADER_BUF too small in bitstream.c \n");
	}
    }
}


inline static int
huffman_coder_count1(lame_internal_flags *gfc, gr_info *gi)
{
    /* Write count1 area */
    const struct huffcodetab *h = &ht[gi->count1table_select + 32];
    int i,bits=0;
#ifdef DEBUG
    int gegebo = gfc->bs.totbit;
#endif

    int *ix = &gi->l3_enc[gi->big_values];
    FLOAT *xr = &gi->xr[gi->big_values];
    assert(gi->count1table_select < 2);

    for (i = (gi->count1 - gi->big_values) / 4; i > 0; --i) {
	int huffbits = 0;
	int p = 0, v;

	v = ix[0];
	if (v) {
	    p += 8;
	    if (xr[0] < 0)
		huffbits++;
	    assert(v <= 1u);
	}

	v = ix[1];
	if (v) {
	    p += 4;
	    huffbits *= 2;
	    if (xr[1] < 0)
		huffbits++;
	    assert(v <= 1u);
	}

	v = ix[2];
	if (v) {
	    p += 2;
	    huffbits *= 2;
	    if (xr[2] < 0)
		huffbits++;
	    assert(v <= 1u);
	}

	v = ix[3];
	if (v) {
	    p++;
	    huffbits *= 2;
	    if (xr[3] < 0)
		huffbits++;
	    assert(v <= 1u);
	}

	ix += 4;
	xr += 4;
	putbits2(gfc, huffbits + h->table[p], h->hlen[p]);
	bits += h->hlen[p];
    }
#ifdef DEBUG
    DEBUGF(gfc,"%ld %d %d %d\n",gfc->bs.totbit -gegebo, gi->count1bits, gi->big_values, gi->count1);
#endif
    return bits;
}



/*
  Implements the pseudocode of page 98 of the IS
  */
inline static int
Huffmancode( lame_internal_flags* const gfc, const int tableindex,
	     int start, int end, gr_info *gi)
{
    const struct huffcodetab* h = &ht[tableindex];
    int index, bits = 0;

    assert(tableindex < 32u);
    if (!tableindex)
	return bits;

    for (index = start; index < end; index += 2) {
	int cbits   = 0;
	int xbits   = 0;
	int linbits = h->xlen;
	int xlen    = h->xlen;
	int ext = 0;
	int x1 = gi->l3_enc[index];
	int x2 = gi->l3_enc[index+1];

	if (x1 != 0) {
	    if (gi->xr[index] < 0)
		ext++;
	    cbits--;
	}

	if (tableindex > 15) {
	    /* use ESC-words */
	    if (x1 > 14) {
		int linbits_x1 = x1 - 15;
		assert ( linbits_x1 <= h->linmax );
		ext   |= linbits_x1 << 1;
		xbits  = linbits;
		x1     = 15;
	    }

	    if (x2 > 14) {
		int linbits_x2 = x2 - 15;
		assert ( linbits_x2 <= h->linmax );
		ext  <<= linbits;
		ext   |= linbits_x2;
		xbits += linbits;
		x2     = 15;
	    }
	    xlen = 16;
	}

	if (x2 != 0) {
	    ext <<= 1;
	    if (gi->xr[index+1] < 0)
		ext++;
	    cbits--;
	}

	assert ( (x1|x2) < 16u );

	x1 = x1 * xlen + x2;
	xbits -= cbits;
	cbits += h->hlen  [x1];

	putbits2(gfc, h->table [x1], cbits );
	putbits2(gfc, ext,  xbits );
	bits += cbits + xbits;
    }
    return bits;
}

/*
  Note the discussion of huffmancodebits() on pages 28
  and 29 of the IS, as well as the definitions of the side
  information on pages 26 and 27.
  */
static int
Huffmancodebits(lame_internal_flags *gfc, gr_info *gi)
{
    int i, j, bits, regionStart[4];

    regionStart[0] = 0;

    j = gfc->scalefac_band.l[gi->region0_count + 1];
    if (j > gi->big_values)
	j = gi->big_values;
    regionStart[1] = j;

    j = gfc->scalefac_band.l[gi->region0_count + gi->region1_count + 2];
    if (j > gi->big_values)
	j = gi->big_values;
    regionStart[2] = j;

    regionStart[3] = gi->big_values;

    bits = 0;
    for (i = 0; i < 3; i++)
	bits += Huffmancode(gfc, gi->table_select[i],
			    regionStart[i], regionStart[i+1], gi);

    bits += huffman_coder_count1(gfc, gi);
    return bits;
}

inline static int
writeMainData (lame_internal_flags *gfc)
{
    int gr, ch, sfb,data_bits,tot_bits=0;
    if (gfc->mode_gr == 2) {
	/* MPEG 1 */
	for (gr = 0; gr < 2; gr++) {
	    for (ch = 0; ch < gfc->channels_out; ch++) {
		gr_info *gi = &gfc->l3_side.tt[gr][ch];
		int slen1 = slen1_tab[gi->scalefac_compress];
		int slen2 = slen2_tab[gi->scalefac_compress];
		data_bits=0;
#ifdef DEBUG
		hogege = gfc->bs.totbit;
#endif
		for (sfb = 0; sfb < gi->sfbdivide; sfb++) {
		    if (gi->scalefac[sfb] == -1)
			continue; /* scfsi is used */
		    if (gi->scalefac[sfb] == -2)
			gi->scalefac[sfb] = 0;
		    putbits2(gfc, gi->scalefac[sfb], slen1);
		    data_bits += slen1;
		}
		for (; sfb < gi->sfbmax; sfb++) {
		    if (gi->scalefac[sfb] == -1)
			continue; /* scfsi is used */
		    if (gi->scalefac[sfb] == -2)
			gi->scalefac[sfb] = 0;
		    putbits2(gfc, gi->scalefac[sfb], slen2);
		    data_bits += slen2;
		}
		assert(data_bits == gi->part2_length);
		data_bits += Huffmancodebits(gfc, gi);
#ifdef DEBUG
		DEBUGF(gfc,"<%ld> ", gfc->bs.totbit-hogege);
#endif
		/* does bitcount in quantize.c agree with actual bit count?*/
		assert(data_bits == gi->part2_3_length + gi->part2_length);
		tot_bits += data_bits;
	    } /* for ch */
	} /* for gr */
    } else {
	/* MPEG 2 */
	for (ch = 0; ch < gfc->channels_out; ch++) {
	    gr_info *gi = &gfc->l3_side.tt[0][ch];
	    int i, sfb_partition, data_bits = 0;
	    assert(gi->sfb_partition_table);

	    sfb = 0;
	    for (sfb_partition = 0; sfb_partition < 4; sfb_partition++) {
		int sfbs = gi->sfb_partition_table[sfb_partition];
		int slen = gi->slen[sfb_partition];
		for (i = 0; i < sfbs; i++, sfb++) {
		    putbits2(gfc, Max(gi->scalefac[sfb], 0U), slen);
		    data_bits += slen;
		}
	    }
	    assert(data_bits == gi->part2_length);
	    data_bits += Huffmancodebits(gfc, gi);

	    /* does bitcount in quantize.c agree with actual bit count?*/
	    assert(data_bits == gi->part2_3_length + gi->part2_length);
	    tot_bits += data_bits;
	} /* for ch */
    } /* for MPEG version */
    return tot_bits;
} /* main_data */



/* compute the number of bits required to flush all mp3 frames
   currently in the buffer.  This should be the same as the
   reservoir size.  Only call this routine between frames - i.e.
   only after all headers and data have been added to the buffer
   by format_bitstream().

   Also compute total_bits_output = 
       size of mp3 buffer (including frame headers which may not
       have yet been send to the mp3 buffer) + 
       number of bits needed to flush all mp3 frames.

   total_bytes_output is the size of the mp3 output buffer if 
   lame_encode_flush_nogap() was called right now. 

 */
int
compute_flushbits( const lame_global_flags * gfp, int *total_bytes_output )
{
  lame_internal_flags *gfc=gfp->internal_flags;
  int flushbits,remaining_headers;
  int bitsPerFrame;
  int last_ptr,first_ptr;
  first_ptr=gfc->w_ptr;           /* first header to add to bitstream */
  last_ptr = gfc->h_ptr - 1;   /* last header to add to bitstream */
  if (last_ptr==-1) last_ptr=MAX_HEADER_BUF-1;   

  /* add this many bits to bitstream so we can flush all headers */
  flushbits = gfc->header[last_ptr].write_timing - gfc->bs.totbit;
  *total_bytes_output=flushbits;

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
  bitsPerFrame = getframebits(gfp);
  flushbits += bitsPerFrame;
  *total_bytes_output += bitsPerFrame;
  /* round up: */
  if (*total_bytes_output % 8) 
      *total_bytes_output = 1 + (*total_bytes_output/8);
  else
      *total_bytes_output = (*total_bytes_output/8);
  *total_bytes_output +=  gfc->bs.buf_byte_idx + 1;


  if (flushbits<0) {
#if 0
    /* if flushbits < 0, this would mean that the buffer looks like:
     * (data...)  last_header  (data...)  (extra data that should not be here...)
     */
    DEBUGF(gfc,"last header write_timing = %i \n",gfc->header[last_ptr].write_timing);
    DEBUGF(gfc,"first header write_timing = %i \n",gfc->header[first_ptr].write_timing);
    DEBUGF(gfc,"bs.totbit:                 %i \n",gfc->bs.totbit);
    DEBUGF(gfc,"first_ptr, last_ptr        %i %i \n",first_ptr,last_ptr);
    DEBUGF(gfc,"remaining_headers =        %i \n",remaining_headers);
    DEBUGF(gfc,"bitsperframe:              %i \n",bitsPerFrame);
    DEBUGF(gfc,"sidelen:                   %i \n",gfc->sideinfo_len);
#endif
    ERRORF(gfc,"strange error flushing buffer ... \n");
  }
  return flushbits;
}



void
flush_bitstream(lame_global_flags *gfp)
{
  lame_internal_flags *gfc=gfp->internal_flags;
  III_side_info_t *l3_side;
  int nbytes;
  int flushbits;
  int last_ptr,first_ptr;
  first_ptr=gfc->w_ptr;           /* first header to add to bitstream */
  last_ptr = gfc->h_ptr - 1;   /* last header to add to bitstream */
  if (last_ptr==-1) last_ptr=MAX_HEADER_BUF-1;   
  l3_side = &gfc->l3_side;


  if ((flushbits = compute_flushbits(gfp,&nbytes)) < 0) return;  
  drain_into_ancillary(gfp, flushbits);

  /* check that the 100% of the last frame has been written to bitstream */
  assert (gfc->header[last_ptr].write_timing + getframebits(gfp)
	  == gfc->bs.totbit);

  /* we have padded out all frames with ancillary data, which is the
     same as filling the bitreservoir with ancillary data, so : */
  gfc->ResvSize=0;
  l3_side->main_data_begin = 0;

}




void  add_dummy_byte ( lame_global_flags* const gfp, unsigned char val )
{
  lame_internal_flags *gfc = gfp->internal_flags;
  int i;

  putbits_noheaders(gfc, val, 8);   

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
format_bitstream(lame_global_flags *gfp)
{
    lame_internal_flags *gfc=gfp->internal_flags;
    int bits,nbytes;
    III_side_info_t *l3_side;
    int bitsPerFrame;
    l3_side = &gfc->l3_side;

    bitsPerFrame = getframebits(gfp);
    drain_into_ancillary(gfp, l3_side->resvDrain_pre);

    encodeSideInfo2(gfp,bitsPerFrame);
    bits = 8*gfc->sideinfo_len;
    bits+=writeMainData(gfc);
    drain_into_ancillary(gfp, l3_side->resvDrain_post);
    bits += l3_side->resvDrain_post;

    l3_side->main_data_begin += (bitsPerFrame-bits)/8;

    /* compare number of bits needed to clear all buffered mp3 frames
     * with what we think the resvsize is: */
    if (compute_flushbits(gfp,&nbytes) != gfc->ResvSize) {
        ERRORF(gfc,"Internal buffer inconsistency. flushbits <> ResvSize");
    }


    /* compare main_data_begin for the next frame with what we
     * think the resvsize is: */
    if ((l3_side->main_data_begin * 8) != gfc->ResvSize ) {
      ERRORF(gfc,"bit reservoir error: \n"
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

      ERRORF(gfc,"This is a fatal error.  It has several possible causes:");
      ERRORF(gfc,"90%  LAME compiled with buggy version of gcc using advanced optimizations");
      ERRORF(gfc," 9%  Your system is overclocked");
      ERRORF(gfc," 1%  bug in LAME encoding library");

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





/* copy data out of the internal MP3 bit buffer into a user supplied
   unsigned char buffer.

   mp3data=0      indicates data in buffer is an id3tags and VBR tags
   mp3data=1      data is real mp3 frame data. 


*/
int copy_buffer(lame_internal_flags *gfc,unsigned char *buffer,int size,int mp3data) 
{
    Bit_stream_struc *bs=&gfc->bs;
    int minimum = bs->buf_byte_idx + 1;
    if (minimum <= 0) return 0;
    if (size!=0 && minimum>size) return -1; /* buffer is too small */
    memcpy(buffer,bs->buf,minimum);
    bs->buf_byte_idx = -1;
    bs->buf_bit_idx = 0;

    if (mp3data) {
        UpdateMusicCRC(&gfc->nMusicCRC,buffer,minimum);
#ifdef DECODE_ON_THE_FLY
	/* this is untested code;
	   if, for somereason, we would like to decode the frame: */
	short int pcm_out[2][1152];
	int mp3out;
	do {
	    /* re-synthesis to pcm.  Repeat until we get a mp3out=0 */
	    mp3out=lame_decode1(buffer,minimum,pcm_out[0],pcm_out[1]); 
	    /* mp3out = 0:  need more data to decode */
	    /* mp3out = -1:  error.  Lets assume 0 pcm output */
	    /* mp3out = number of samples output */
	    if (mp3out==-1) {
		/* error decoding.  Not fatal, but might screw up are
		 * ReplayVolume Info tag.  
		 * what should we do?  ignore for now */
		mp3out=0;
	    }
	    if (mp3out>0 && mp3out>1152) {
		/* this should not be possible, and indicates we have
		 * overflowed the pcm_out buffer.  Fatal error. */
		return -6;
	    }
	} while (mp3out>0);
#endif
    }
    return minimum;
}


/* end of bitstream.c */
