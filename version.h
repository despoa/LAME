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

#ifndef VERSION_H_INCLUDED
# define VERSION_H_INCLUDED

# include <stdio.h>

/* Please regularly increment version number, for alpha/beta versions at least once a week */

# define LAME_MAJOR_VERSION      3      /* Major version number */
# define LAME_MINOR_VERSION     87      /* Minor version number */
# define LAME_ALPHA_VERSION     16      /* Patch level of alpha version, otherwise zero */
# define LAME_BETA_VERSION       0      /* Patch level of beta  version, otherwise zero */

# define PSY_MAJOR_VERSION       0      /* Major version number */
# define PSY_MINOR_VERSION      80      /* Minor version number */
# define PSY_ALPHA_VERSION       0      /* Set number if this is an alpha version, otherwise zero */
# define PSY_BETA_VERSION        0      /* Set number if this is a beta version, otherwise zero */

# define MP3X_MAJOR_VERSION      0      /* Major version number */
# define MP3X_MINOR_VERSION     82      /* Minor version number */
# define MP3X_ALPHA_VERSION      0      /* Set number if this is an alpha version, otherwise zero */
# define MP3X_BETA_VERSION       0      /* Set number if this is a beta version, otherwise zero */

void         lame_print_version ( FILE* fp );
const char*  get_lame_version   ( void );       /* returns lame version number string */
const char*  get_psy_version    ( void );       /* returns psy model version number string */
const char*  get_mp3x_version   ( void );       /* returns mp3x version number string */

#endif  /* VERSION_H_INCLUDED */

/* End of version.h */
