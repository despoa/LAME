/*
 *      Version numbering for LAME.
 *
 *      Copyright (c) 1999 A.L. Faber
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "version.h"    // macros of version numbers

#if defined(MMX_choose_table)
# define V1 "MMX "
#else
# define V1 ""
#endif


void get_lame_version ( char *strbuf, size_t buflen, const char *prefix )  // primary for reports on screen
{
    char str [48];

    /* Here we can also add informations about compile time configurations */
    
    if (LAME_ALPHA_VERSION > 0)
        sprintf ( str, "%u.%02d " V1 "(alpha %u, %6.6s %5.5s)", LAME_MAJOR_VERSION, LAME_MINOR_VERSION, LAME_ALPHA_VERSION, __DATE__, __TIME__ );
    else if (LAME_BETA_VERSION > 0)
        sprintf ( str, "%u.%02d " V1 "(beta %u, %s)", LAME_MAJOR_VERSION, LAME_MINOR_VERSION, LAME_BETA_VERSION, __DATE__ );
    else
        sprintf ( str, "%u.%02d " V1, LAME_MAJOR_VERSION, LAME_MINOR_VERSION );
        
    if (buflen-- == 0) buflen = 0;

    if (prefix) {
        size_t i;

        strncpy ( strbuf, prefix, buflen );
        strbuf [buflen] = 0;
        i = strlen( strbuf );
        strbuf += i;
        buflen -= i;
    }

    strncpy ( strbuf, str, buflen );
    strbuf [buflen] = 0;
}

void get_lame_short_version ( char *strbuf, size_t buflen, const char *prefix )  // primary to write into MP3 files
{
    char str [32];
    
    /* adding date and time to version string makes it harder for
     * output validation
     * I also removed version string and replaced by a "1" to be mt friendly 
     */

#if  (LAME_MINOR_VERSION == 88)  /* to avoid changes in the coded MP3 files until minor is changed to != 88 */
    
    if (LAME_ALPHA_VERSION > 0)
        sprintf ( str, "%u.%02d (alpha 1)", LAME_MAJOR_VERSION, LAME_MINOR_VERSION );
    else if (LAME_BETA_VERSION > 0)
        sprintf ( str, "%u.%02d (beta 1)", LAME_MAJOR_VERSION, LAME_MINOR_VERSION );
    else
        sprintf ( str, "%u.%02d", LAME_MAJOR_VERSION, LAME_MINOR_VERSION );
    
#else

    if (LAME_ALPHA_VERSION > 0)
        sprintf ( str, "%u.%02d (alpha)", LAME_MAJOR_VERSION, LAME_MINOR_VERSION );
    else if (LAME_BETA_VERSION > 0)
        sprintf ( str, "%u.%02d (beta)", LAME_MAJOR_VERSION, LAME_MINOR_VERSION );
    else
        sprintf ( str, "%u.%02d", LAME_MAJOR_VERSION, LAME_MINOR_VERSION );

#endif    
        
    if (buflen-- == 0) buflen = 0;

    if (prefix) {
        size_t i;

        strncpy ( strbuf, prefix, buflen );
        strbuf [buflen] = 0;
        i = strlen( strbuf );
        strbuf += i;
        buflen -= i;
    }

    strncpy ( strbuf, str, buflen );
    strbuf [buflen] = 0;
}

void get_psy_version ( char *strbuf, size_t buflen, const char *prefix )
{
    char str [48];
    
    if (PSY_ALPHA_VERSION > 0)
        sprintf ( str, "%u.%02d (alpha %u, %6.6s %5.5s)", PSY_MAJOR_VERSION, PSY_MINOR_VERSION, PSY_ALPHA_VERSION, __DATE__, __TIME__ );
    else if (PSY_BETA_VERSION > 0)
        sprintf ( str, "%u.%02d (beta %u, %s)", PSY_MAJOR_VERSION, PSY_MINOR_VERSION, PSY_BETA_VERSION, __DATE__ );
    else
        sprintf ( str, "%u.%02d", PSY_MAJOR_VERSION, PSY_MINOR_VERSION );
        
    if (buflen-- == 0) buflen = 0;

    if (prefix) {
        size_t i;

        strncpy ( strbuf, prefix, buflen );
        strbuf [buflen] = 0;
        i = strlen( strbuf );
        strbuf += i;
        buflen -= i;
    }

    strncpy ( strbuf, str, buflen );
    strbuf [buflen] = 0;
}

void get_mp3x_version ( char *strbuf, size_t buflen, const char *prefix )
{
    char str [48];
    
    if (MP3X_ALPHA_VERSION > 0)
        sprintf ( str, "%u:%02u (alpha %u, %6.6s %5.5s)", MP3X_MAJOR_VERSION, MP3X_MINOR_VERSION, MP3X_ALPHA_VERSION, __DATE__, __TIME__ );
    else if (MP3X_BETA_VERSION > 0)
        sprintf ( str, "%u:%02u (beta %u, %s)", MP3X_MAJOR_VERSION, MP3X_MINOR_VERSION, MP3X_BETA_VERSION, __DATE__ );
    else
        sprintf ( str, "%u:%02u", MP3X_MAJOR_VERSION, MP3X_MINOR_VERSION );
                
    if (buflen-- == 0) buflen = 0;

    if (prefix) {
        size_t i;

        strncpy ( strbuf, prefix, buflen );
        strbuf [buflen] = 0;
        i = strlen( strbuf );
        strbuf += i;
        buflen -= i;
    }

    strncpy ( strbuf, str, buflen );
    strbuf [buflen] = 0;
}

void get_lame_url ( char *strbuf, size_t buflen, const char *prefix )
{
    if (buflen-- == 0) buflen = 0;

    if (prefix) {
        size_t i;

        strncpy ( strbuf, prefix, buflen );
        strbuf [buflen] = 0;
        i = strlen( strbuf );
        strbuf += i;
        buflen -= i;
    }

    strncpy ( strbuf, LAME_URL, buflen );
    strbuf [buflen] = 0;
}    

/* End of version.c */
