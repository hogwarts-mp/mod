// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MinHoloLensApi.h: Minimal includes for HoloLens
=============================================================================*/

#pragma once

#include "CoreTypes.h"

#if defined(_WINDOWS_) && !defined(UE4_MINIMAL_WINDOWS_INCLUDE)
	#pragma message ( " " )
	#pragma message ( "You have included windows.h before MinWindows.h" )
	#pragma message ( "All useless stuff from the windows headers won't be excluded !!!" )
	#pragma message ( " " )
#endif // _WINDOWS_

#if !PLATFORM_HOLOLENS
#error PLATFORM_HOLOLENS not defined
#endif

#define UE4_MINIMAL_WINDOWS_INCLUDE

// WIN32_LEAN_AND_MEAN excludes rarely-used services from windows headers.
#define WIN32_LEAN_AND_MEAN

// The below excludes some other unused services from the windows headers -- see windows.h for details.
#define NOGDICAPMASKS			// CC_*, LC_*, PC_*, CP_*, TC_*, RC_
//#define NOVIRTUALKEYCODES		// VK_*
//#define NOWINMESSAGES			// WM_*, EM_*, LB_*, CB_*
//#define NOWINSTYLES			// WS_*, CS_*, ES_*, LBS_*, SBS_*, CBS_*
//#define NOSYSMETRICS			// SM_*
#define NOMENUS					// MF_*
//#define NOICONS				// IDI_*
//#define NOKEYSTATES			// MK_*
//#define NOSYSCOMMANDS			// SC_*
//#define NORASTEROPS			// Binary and Tertiary raster ops
//#define NOSHOWWINDOW			// SW_*
#define OEMRESOURCE				// OEM Resource values
#define NOATOM					// Atom Manager routines
//#define NOCLIPBOARD			// Clipboard routines
//#define NOCOLOR				// Screen colors
//#define NOCTLMGR				// Control and Dialog routines
#define NODRAWTEXT				// DrawText() and DT_*
//#define NOGDI					// All GDI #defines and routines
#define NOKERNEL				// All KERNEL #defines and routines
//#define NOUSER				// All USER #defines and routines
//#define NONLS					// All NLS #defines and routines
//#define NOMB					// MB_* and MessageBox()
#define NOMEMMGR				// GMEM_*, LMEM_*, GHND, LHND, associated routines
#define NOMETAFILE				// typedef METAFILEPICT
#define NOMINMAX				// Macros min(a,b) and max(a,b)
//#define NOMSG					// typedef MSG and associated routines
#define NOOPENFILE				// OpenFile(), OemToAnsi, AnsiToOem, and OF_*
#define NOSCROLL				// SB_* and scrolling routines
#define NOSERVICE				// All Service Controller routines, SERVICE_ equates, etc.
#define NOSOUND					// Sound driver routines
//#define NOTEXTMETRIC			// typedef TEXTMETRIC and associated routines
//#define NOWH					// SetWindowsHook and WH_*
//#define NOWINOFFSETS			// GWL_*, GCL_*, associated routines
#define NOCOMM					// COMM driver routines
#define NOKANJI					// Kanji support stuff.
#define NOHELP					// Help engine interface.
#define NOPROFILER				// Profiler interface.
#define NODEFERWINDOWPOS		// DeferWindowPos routines
#define NOMCX					// Modem Configuration Extensions
#define NOCRYPT
#define NOTAPE
#define NOIMAGE
#define NOPROXYSTUB
#define NORPC

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Finally now we can include windows.h
#ifndef InterlockedIncrement
#define InterlockedIncrement_MinHoloLensApi_h_hidden
#define InterlockedIncrement _InterlockedIncrement
#endif // !InterlockedIncrement

#ifndef InterlockedDecrement
#define InterlockedDecrement_MinHoloLensApi_h_hidden
#define InterlockedDecrement _InterlockedDecrement
#endif // !InterlockedDecrement

#define InterlockedDecrement _InterlockedDecrement

#include <wrl.h>

#ifdef InterlockedIncrement_MinHoloLensApi_h_hidden
#undef InterlockedIncrement
#undef InterlockedIncrement_MinHoloLensApi_h_hidden
#endif // InterlockedIncrement_MinHoloLensApi_h_hidden

#ifdef InterlockedDecrement_MinHoloLensApi_h_hidden
#undef InterlockedDecrement
#undef InterlockedDecrement_MinHoloLensApi_h_hidden
#endif // InterlockedIncrement_MinHoloLensApi_h_hidden
