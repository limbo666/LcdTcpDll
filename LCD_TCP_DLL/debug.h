#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <windows.h>
#include <stdio.h>

// FORCE ENABLE LOGGING
#define DEBUG_LEVEL_1_EN 

#ifdef DEBUG_LEVEL_1_EN
#define d1printf DebugString
#else
#define d1printf(...)
#endif

void WINAPI DebugString(LPCSTR format, ...);

#endif