/*
** Copyright (C) 2000 Albert L. Faber
**  
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software 
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/


#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include "mpg123.h"
#include "mpglib.h"

extern int const tabsel_123[2][3][16];

int head_check(unsigned long head);
int decode_header(struct frame *fr,unsigned long newhead);
void print_header(struct frame *fr);
void print_header_compact(struct frame *fr);
unsigned int getbits(int number_of_bits);
unsigned int getbits_fast(int number_of_bits);

#endif
