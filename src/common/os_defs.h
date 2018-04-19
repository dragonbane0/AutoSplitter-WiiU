#ifndef __OS_DEFS_H_
#define __OS_DEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _OsSpecifics
{
    unsigned int addr_OSDynLoad_Acquire;
    unsigned int addr_OSDynLoad_FindExport;
    unsigned int addr_OSTitle_main_entry;

    unsigned int addr_KernSyscallTbl1;
    unsigned int addr_KernSyscallTbl2;
    unsigned int addr_KernSyscallTbl3;
    unsigned int addr_KernSyscallTbl4;
    unsigned int addr_KernSyscallTbl5;
} OsSpecifics;

typedef struct _OSCalendarTime
{
	signed int tm_sec;
	signed int tm_min;
	signed int tm_hour;
	signed int tm_mday;
	signed int tm_mon;
	signed int tm_year;
} OSCalendarTime;

typedef struct _OSSystemInfo
{
	unsigned int busSpeed;
	unsigned int coreSpeed;
	signed long long baseTime;
	unsigned int _unkX[3];
	unsigned int _unkY;
} OSSystemInfo;

#ifdef __cplusplus
}
#endif

#endif // __OS_DEFS_H_
