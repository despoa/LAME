/*
 *	Bitrate histogram source file
 *
 *	Copyright (c) 2000 Mark Taylor
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

/* basic #define's */

#ifdef NOTERMCAP            /* work around to change the !NOTERMCAP to TERMCAP_AVAILABLE */
# undef  TERMCAP_AVAILABLE
#else
# define TERMCAP_AVAILABLE
#endif

#ifndef BRHIST_WIDTH
# define BRHIST_WIDTH    14
#endif
#ifndef BRHIST_RES
# define BRHIST_RES      11
#endif


/* #includes */

#include <stdlib.h>
#include <string.h>

#if defined(TERMCAP_AVAILABLE)
# include <termcap.h>
#endif

#include "brhist.h"


/* ///
 * Sorry for not using KLEMM_03, but the code was really unreadable with so much #ifdefs
 * and finally I was not sure not to unintentionally have modified the original code ...
 *
 * remove after reading (such remarks I mark with a "///")
 * changed:
 *   not all bitrates are display, only min <= x <= max and outside this range the used bitrates
 *   cursor stays at the end of the screen, so ^C works right and nice (fflush moved two lines up)
 *   WINDOWS not tested, problems may be at the line marked with "$$$" if counting the lines is wrong for Windows
 *   No cache array for bit rate string representations, printed immediately with %3u from the int array
 *   Renaming of br_min to br_kbps_min (I trapped into the pitfall that br_min in 3.87 was the index and now without renaming it was the data rate in kbps)
 *   A "bug" is that brhist_disp_line has too many arguments
 *   assert.h removed, not used
 *   some minor changes, "%#5.4g" for percentages of LR/MS, a spell error in "BRHIST_WIDTH" (last two letters were wrong), and things like that
 *   some unsigned long => int stuff
 *   brhist_disp have a second arg to select to jump back or not
 *
 * Why not adding to the cvs comments:
 *   commenting must be done while transmission (costs money, 4.8 Pf/min)
 *   I have 3 minutes time, otherwise the modem cancels the connection and the comments are lost (cvs have problems with this case)
 *   comments can be added at the right place
 */

/* Structure holding all data related to the Console I/O 
 * may be this should be a more global frontend structure. So it
 * makes sense to print all files instead with
 * printf ( "blah\n") with printf ( "blah%s\n", Console_IO.str_clreoln );
 */

Console_IO_t Console_IO;

static struct {
    int     vbr_bitrate_min_index;
    int     vbr_bitrate_max_index;
    int     kbps [BRHIST_WIDTH];
    char    bar_asterisk [512 + 1];	/* buffer filled up with a lot of '*' to print a bar     */
    char    bar_hash     [512 + 1];	/* buffer filled up with a lot of '%' to print a bar     */
} brhist;

static size_t  calculate_index ( const int* const array, const size_t len, const int value )
{
    size_t  i;
    
    for ( i = 0; i < len; i++ )
        if ( array [i] == value )
	    return i;
    return -1;
}

int  brhist_init ( const lame_global_flags* gf, const int bitrate_kbps_min, const int bitrate_kbps_max )
{
#ifdef TERMCAP_AVAILABLE
    char         term_buff [2048];  // see 1)
    const char*  term_name;
    char*        tp;
    char         tc [10];
    int          val;
#endif

    /* setup basics of brhist I/O channels */
    Console_IO.disp_width  = 80;
    Console_IO.disp_height = 25;
    Console_IO.Console_fp  = stderr;
    Console_IO.Error_fp    = stderr;
    Console_IO.Report_fp   = stderr;

    setvbuf ( Console_IO.Console_fp, Console_IO.Console_buff, _IOFBF, sizeof (Console_IO.Console_buff) );
//  setvbuf ( Console_IO.Error_fp  , NULL                   , _IONBF, 0                                );

#if defined(_WIN32)  &&  !defined(__CYGWIN__) 
    Console_IO.Console_Handle = GetStdHandle (STD_ERROR_HANDLE);
#endif

    strcpy ( Console_IO.str_up, "\033[A" );
    
    /* some internal checks */
    if ( bitrate_kbps_min > bitrate_kbps_max ) {
	fprintf ( Console_IO.Error_fp, "lame internal error: VBR min %d kbps > VBR max %d kbps.\n", bitrate_kbps_min, bitrate_kbps_max );
	return -1;
    }
    if ( bitrate_kbps_min < 8  ||  bitrate_kbps_max > 320 ) {
	fprintf ( Console_IO.Error_fp, "lame internal error: VBR min %d kbps or VBR max %d kbps out of range.\n", bitrate_kbps_min, bitrate_kbps_max );
	return -1;
    }

    /* initialize histogramming data structure */
    lame_bitrate_kbps ( gf, brhist.kbps );
    brhist.vbr_bitrate_min_index = calculate_index ( brhist.kbps, BRHIST_WIDTH, bitrate_kbps_min );
    brhist.vbr_bitrate_max_index = calculate_index ( brhist.kbps, BRHIST_WIDTH, bitrate_kbps_max );

    if ( (unsigned)brhist.vbr_bitrate_min_index >= BRHIST_WIDTH  ||  
         (unsigned)brhist.vbr_bitrate_max_index >= BRHIST_WIDTH ) {
	fprintf ( Console_IO.Error_fp, "lame internal error: VBR min %d kbps or VBR max %d kbps not allowed.\n", bitrate_kbps_min, bitrate_kbps_max );
	return -1;
    }

    memset (brhist.bar_asterisk, '*', sizeof (brhist.bar_asterisk)-1 );
    memset (brhist.bar_hash    , '%', sizeof (brhist.bar_hash)    -1 );

#ifdef TERMCAP_AVAILABLE
    /* try to catch additional information about special console sequences */
    
    if ((term_name = getenv("TERM")) == NULL) {
	fprintf ( Console_IO.Error_fp, "LAME: Can't get \"TERM\" environment string.\n" );
	return -1;
    }
    if ( tgetent (term_buff, term_name) != 1 ) {
	fprintf ( Console_IO.Error_fp, "LAME: Can't find termcap entry for terminal \"%s\"\n", term_name );
	return -1;
    }
    
    val = tgetnum ("co");
    if ( val >= 40  &&  val <= 512 )
        Console_IO.disp_width   = val;
    val = tgetnum ("li");
    if ( val >= 16  &&  val <= 256 )
        Console_IO.disp_height  = val;
        
    *(tp = tc) = '\0';
    tp = tgetstr ("up", &tp);
    if (tp != NULL)
        strcpy ( Console_IO.str_up, tp );

    *(tp = tc) = '\0';
    tp = tgetstr ("ce", &tp);
    if (tp != NULL)
        strcpy ( Console_IO.str_clreoln, tp );

    *(tp = tc) = '\0';
    tp = tgetstr ("md", &tp);
    if (tp != NULL)
        strcpy ( Console_IO.str_emph, tp );

    *(tp = tc) = '\0';
    tp = tgetstr ("me", &tp);
    if (tp != NULL)
        strcpy ( Console_IO.str_norm, tp );
        
#endif /* TERMCAP_AVAILABLE */

    return 0;
}


static void  brhist_disp_line ( const lame_global_flags*  gf, int i, int br_hist_TOT, int br_hist_LR, int full, int frames )
{
    char    brppt [14];  /* [%] and max. 10 characters for kbps */
    int     barlen_TOT;
    int     barlen_LR;
    int     ppt  = 0;

    if ( full != 0 ) {
        // some problems when br_hist_TOT \approx br_hist_LR: You can't see that there are still MS frames
        barlen_TOT = (br_hist_TOT * (Console_IO.disp_width-BRHIST_RES) + full-1 ) / full;  /* round up */
        barlen_LR  = (br_hist_LR  * (Console_IO.disp_width-BRHIST_RES) + full-1 ) / full;  /* round up */
    } else {
        barlen_TOT = barlen_LR = 0;
    }

    if (frames > 0)
        ppt = (1000lu * br_hist_TOT + frames/2) / frames;                                  /* round nearest */

    if ( br_hist_TOT == 0 )
        sprintf ( brppt,  " [   ]" );
    else if ( ppt < br_hist_TOT/10000 )
        sprintf ( brppt," [%%..]" );
    else if ( ppt <  10 )
        sprintf ( brppt," [%%.%1u]", ppt );
    else if ( ppt < 995 )
        sprintf ( brppt, " [%2u%%]", (ppt+5)/10 );
    else
        sprintf ( brppt, "[%3u%%]", (ppt+5)/10 );
          
    if ( Console_IO.str_clreoln [0] ) /* ClearEndOfLine available */
        fprintf ( Console_IO.Console_fp, "\n%3d%s %.*s%.*s%s", 
	          brhist.kbps [i], brppt, 
                  barlen_LR, brhist.bar_hash, 
                  barlen_TOT - barlen_LR, brhist.bar_asterisk, 
		  Console_IO.str_clreoln );
    else
        fprintf ( Console_IO.Console_fp, "\n%3d%s %.*s%.*s%*s ", 
	          brhist.kbps [i], brppt, 
                  barlen_LR, brhist.bar_hash, 
                  barlen_TOT, brhist.bar_asterisk, 
		  Console_IO.disp_width - BRHIST_RES - barlen_TOT - barlen_LR, "" );
}


/* Yes, not very good */
#define LR  0
#define MS  2

void  brhist_disp ( const lame_global_flags*  gf, const int jump_back )
{
    int   i;
    int   br_hist [BRHIST_WIDTH];       /* how often a frame size was used */
    int   br_sm_hist [BRHIST_WIDTH] [4];/* how often a special frame size/stereo mode commbination was used */
    int   frames;                       /* total number of encoded frames */
    int   most_often;                   /* usage count of the most often used frame size, but not smaller than Console_IO.disp_width-BRHIST_RES (makes this sense?) and 1 */
    int   printed_lines = 0;            /* printed number of lines for the brhist functionality, used to skip back the right number of lines */
    
    lame_bitrate_hist             ( gf, br_hist    );
    lame_bitrate_stereo_mode_hist ( gf, br_sm_hist );

    frames = most_often = 0;
    for (i = 0; i < BRHIST_WIDTH; i++) {
        frames += br_hist[i];
        if (most_often < br_hist[i]) most_often = br_hist[i];
    }

#ifdef KLEMM_05
    i = 0;
    for (; i < brhist.vbr_bitrate_min_index; i++) 
        if ( br_hist [i] ) {
            brhist_disp_line ( gf, i, br_hist [i], br_sm_hist [i][LR], most_often, frames );
    	    printed_lines++;
	}
    for (; i <= brhist.vbr_bitrate_max_index; i++) {
        brhist_disp_line ( gf, i, br_hist [i], br_sm_hist [i][LR], most_often, frames );
	printed_lines++;
    }
    for (; i < BRHIST_WIDTH; i++)
        if ( br_hist [i] ) {
            brhist_disp_line ( gf, i, br_hist [i], br_sm_hist [i][LR], most_often, frames );
	    printed_lines++;
	}
#else
    most_often = most_often < Console_IO.disp_width - BRHIST_RES  ?  Console_IO.disp_width - BRHIST_RES : most_often;  /* makes this sense? */
    for ( i=0 ; i < BRHIST_WIDTH; i++) {
        brhist_disp_line ( gf, i, br_hist [i], br_sm_hist [i][LR], most_often, frames );
	printed_lines++; 
    }
#endif	
    
#if defined(_WIN32)  &&  !defined(__CYGWIN__) 
    /* fflush is needed for Windows ! */
	fflush ( Console_IO.Console_fp );

    if ( GetFileType (Console_IO.Console_Handle) != FILE_TYPE_PIPE ) {
        COORD                       Pos;
        CONSOLE_SCREEN_BUFFER_INFO  CSBI;
	
	GetConsoleScreenBufferInfo ( Console_IO.Console_Handle, &CSBI );
	Pos.Y = CSBI.dwCursorPosition.Y - printed_lines ;  /* $$$ */
	Pos.X = 0;
	SetConsoleCursorPosition ( Console_IO.Console_Handle, Pos );
    }
#else
    fputs ( "\r", Console_IO.Console_fp );
    fflush ( Console_IO.Console_fp );
    if ( jump_back )
        while ( printed_lines-- > 0 )
            fputs ( Console_IO.str_up, Console_IO.Console_fp );
#endif
}


void  brhist_disp_total ( const lame_global_flags* gf )
{
    int i;
    int br_hist [BRHIST_WIDTH];
    int st_mode [4];
    int st_frames = 0;
    int br_frames = 0;
    double sum = 0.;
    
    lame_stereo_mode_hist ( gf, st_mode );
    lame_bitrate_hist     ( gf, br_hist );
    
    for (i = 0; i < BRHIST_WIDTH; i++) {
        br_frames += br_hist[i];
	sum       += br_hist[i] * brhist.kbps[i];
    }

    for (i = 0; i < 4; i++) {
        st_frames += st_mode[i];
    }

    fprintf ( Console_IO.Console_fp, "\naverage: %5.1f kbps", sum / br_frames);

    // I'm very unhappy because this is only printed out in VBR modes
    if (st_frames > 0) {
        if ( st_mode[LR] > 0 )
            fprintf ( Console_IO.Console_fp, "   LR: %d (%#5.4g%%)", st_mode[LR], 100. * st_mode[LR] / st_frames );
        else
            fprintf ( Console_IO.Console_fp, "                 " );
        if ( st_mode[MS] > 0 )
            fprintf ( Console_IO.Console_fp, "   MS: %d (%#5.4g%%)", st_mode[MS], 100. * st_mode[MS] / st_frames );
    }
    fprintf ( Console_IO.Console_fp, "\n" );
    fflush  ( Console_IO.Console_fp );
}

/* 
 * 1)
 *
 * Taken from Termcap_Manual.html:
 *
 * With the Unix version of termcap, you must allocate space for the description yourself and pass
 * the address of the space as the argument buffer. There is no way you can tell how much space is
 * needed, so the convention is to allocate a buffer 2048 characters long and assume that is
 * enough.  (Formerly the convention was to allocate 1024 characters and assume that was enough.
 * But one day, for one kind of terminal, that was not enough.)
 */

/* end of brhist.c */
