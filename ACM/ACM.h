/**
 *
 * Lame ACM wrapper, encode/decode MP3 based RIFF/AVI files in MS Windows
 *
 *  Copyright (c) 2002 Steve Lhomme <steve.lhomme at free.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
 
/*!
	\author Steve Lhomme
	\version \$Id$
*/

#if !defined(_ACM_H__INCLUDED_)
#define _ACM_H__INCLUDED_

#if _MSC_VER >= 1000
#pragma once
#endif // _MSC_VER >= 1000

#include <windows.h>
#include <mmsystem.h>

#include "ADbg/ADbg.h"

class ACM  
{
public:
	ACM( HMODULE hModule );
	virtual ~ACM();

	LONG DriverProcedure(const HDRVR hdrvr, const UINT msg, LONG lParam1, LONG lParam2);

protected:
	inline DWORD Configure( HWND hParentWindow, LPDRVCONFIGINFO pConfig );
	inline DWORD About( HWND hParentWindow );

	inline DWORD OnDriverDetails(const HDRVR hdrvr, LPACMDRIVERDETAILS a_DriverDetail);
	inline DWORD OnFormatTagDetails(LPACMFORMATTAGDETAILS a_FormatTagDetails, const LPARAM a_Query);
	inline DWORD OnFormatDetails(LPACMFORMATDETAILS a_FormatDetails, const LPARAM a_Query);
	inline DWORD OnFormatSuggest(LPACMDRVFORMATSUGGEST a_FormatSuggest);
	inline DWORD OnStreamOpen(LPACMDRVSTREAMINSTANCE a_StreamInstance);
	inline DWORD OnStreamClose(LPACMDRVSTREAMINSTANCE a_StreamInstance);
	inline DWORD OnStreamSize(LPACMDRVSTREAMINSTANCE a_StreamInstance, LPACMDRVSTREAMSIZE the_StreamSize);
	inline DWORD OnStreamPrepareHeader(LPACMDRVSTREAMINSTANCE a_StreamInstance, LPACMSTREAMHEADER a_StreamHeader);
	inline DWORD OnStreamUnPrepareHeader(LPACMDRVSTREAMINSTANCE a_StreamInstance, LPACMSTREAMHEADER a_StreamHeader);
	inline DWORD OnStreamConvert(LPACMDRVSTREAMINSTANCE a_StreamInstance, LPACMDRVSTREAMHEADER a_StreamHeader);

	void GetMP3FormatForIndex(const DWORD the_Index, WAVEFORMATEX & the_Format, unsigned short the_String[ACMFORMATDETAILS_FORMAT_CHARS]) const;
	void GetPCMFormatForIndex(const DWORD the_Index, WAVEFORMATEX & the_Format, unsigned short the_String[ACMFORMATDETAILS_FORMAT_CHARS]) const;

	HMODULE my_hModule;
	HICON   my_hIcon;
	ADbg    my_debug;

};

#endif // !defined(_ACM_H__INCLUDED_)

