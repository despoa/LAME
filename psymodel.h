/*
 *	psymodel.h
 *
 *	Copyright (c) 1999 Mark Taylor
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

#ifndef LAME_PSYMODEL_H
#define LAME_PSYMODEL_H

#include "l3side.h"

int L3psycho_anal( lame_global_flags *gfp,
                    short int *buffer[2], int gr , 
		    FLOAT8 *ms_ratio, 
		    FLOAT8 *ms_ratio_next, 
		    FLOAT8 *ms_ener_ratio, 
		    III_psy_ratio ratio[2][2],
		    III_psy_ratio MS_ratio[2][2],
		    FLOAT8 pe[2], FLOAT8 pe_MS[2], 
                    int blocktype_d[2]); 

#endif /* LAME_PSYMODEL_H */
