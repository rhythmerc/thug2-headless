#pragma once
#include <windows.h>
struct PROCESS_MEMORY_COUNTERS {
    DWORD cb;
    DWORD PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
};
inline BOOL K32GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD) { return FALSE; }
#define GetProcessMemoryInfo K32GetProcessMemoryInfo
