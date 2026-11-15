#pragma once
#include <windows.h>
// winmm: the multimedia timer
DWORD timeGetTime();
UINT timeBeginPeriod(UINT period);
UINT timeEndPeriod(UINT period);
#define TIMERR_NOERROR 0u
// The multimedia timer drives only the loading screens' animation: headless there is none (no timer made).
using LPTIMECALLBACK = void (*)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
#define TIME_ONESHOT 0u
#define TIME_PERIODIC 1u
inline UINT timeSetEvent(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT) { return 0; }
inline UINT timeKillEvent(UINT) { return 0; }
