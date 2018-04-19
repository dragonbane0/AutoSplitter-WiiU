#ifndef __THREAD_DEFS_H_
#define __THREAD_DEFS_H_

#include "types.h"
#include "assert.h"

#ifdef __cplusplus
extern "C" 
{
#endif

	//Useful Thread Stuff
	typedef struct OSContext OSContext;
	typedef struct OSThread OSThread;

	typedef u8 OSThreadState;
	typedef u32 OSThreadRequest;
	typedef u8 OSThreadAttributes;

	typedef int(*OSThreadEntryPointFn)(int argc, void *argv);
	typedef void(*OSThreadCleanupCallbackFn)(OSThread *thread, void *stack);
	typedef void(*OSThreadDeallocatorFn)(OSThread *thread, void *stack);

	typedef struct OSThreadLink OSThreadLink;
	typedef struct OSThreadQueue OSThreadQueue;
	typedef struct OSThreadSimpleQueue OSThreadSimpleQueue;

	typedef struct OSMutex OSMutex;
	typedef struct OSMutexQueue OSMutexQueue;
	typedef struct OSMutexLink OSMutexLink;

	typedef struct OSFastMutex OSFastMutex;
	typedef struct OSFastMutexQueue OSFastMutexQueue;
	typedef struct OSFastMutexLink OSFastMutexLink;


	enum OS_THREAD_STATE
	{
		OS_THREAD_STATE_NONE = 0,
		OS_THREAD_STATE_READY = 1 << 0,
		OS_THREAD_STATE_RUNNING = 1 << 1,
		OS_THREAD_STATE_WAITING = 1 << 2,
		OS_THREAD_STATE_MORIBUND = 1 << 3,
	};

	enum OS_THREAD_REQUEST
	{
		OS_THREAD_REQUEST_NONE = 0,
		OS_THREAD_REQUEST_SUSPEND = 1,
		OS_THREAD_REQUEST_CANCEL = 2,
	};
	enum OS_THREAD_ATTRIB
	{
		OS_THREAD_ATTRIB_AFFINITY_CPU0 = 1 << 0,
		OS_THREAD_ATTRIB_AFFINITY_CPU1 = 1 << 1,
		OS_THREAD_ATTRIB_AFFINITY_CPU2 = 1 << 2,
		OS_THREAD_ATTRIB_AFFINITY_ANY = ((1 << 0) | (1 << 1) | (1 << 2)),
		OS_THREAD_ATTRIB_DETACHED = 1 << 3,
		OS_THREAD_ATTRIB_STACK_USAGE = 1 << 5
	};

	struct OSContext
	{
		/* OSContext identifier */
		u32 tag1;
		u32 tag2;

		/* GPRs */
		u32 gpr[32];

		/* Special registers */
		u32 cr;
		u32 lr;
		u32 ctr;
		u32 xer;

		/* Initial PC and MSR */
		u32 srr0;
		u32 srr1;

		/* Only valid during DSI exception */
		u32 exception_specific0;
		u32 exception_specific1;

		u8 unk1[12];
		u32 fpscr;
		double fpr[32];
		u16 spinLockCount;
		u16 state;
		u32 gqr[8];
		u8 unk2[4];
		double psf[32];
		u64 coretime[3];
		u64 starttime;
		u32 error;
		u8 unk3[4];
		u32 pmc1;
		u32 pmc2;
		u32 pmc3;
		u32 pmc4;
		u32 mmcr0;
		u32 mmcr1;
	};
	static_assert(sizeof(OSContext) == 0x320, "OSContext Size");

	struct OSThreadLink
	{
		OSThread *prev;
		OSThread *next;
	};
	static_assert(sizeof(OSThreadLink) == 8, "OSThreadLink Size");

	struct OSThreadQueue
	{
		OSThread *head;
		OSThread *tail;
		void *parent;
		u8 unk1[4];
	};
	static_assert(sizeof(OSThreadQueue) == 0x10, "OSThreadQueue Size");

	struct OSThreadSimpleQueue
	{
		OSThread *head;
		OSThread *tail;
	};
	static_assert(sizeof(OSThreadSimpleQueue) == 8, "OSThreadSimpleQueue Size");

	struct OSMutexQueue
	{
		OSMutex *head;
		OSMutex *tail;
		void *parent;
		u8 unk[4];
	};
	static_assert(sizeof(OSMutexQueue) == 0x10, "OSMutexQueue Size");

	struct OSMutexLink
	{
		OSMutex *next;
		OSMutex *prev;
	};
	static_assert(sizeof(OSMutexLink) == 8, "OSMutexLink Size");

	struct OSMutex
	{
		u32 tag;
		const char *name;
		u8 unk1[4];
		OSThreadQueue queue;
		OSThread *owner;
		s32 count;
		OSMutexLink link;
	};
	static_assert(sizeof(OSMutex) == 0x2C, "OSMutex Size");

	struct OSFastMutexQueue
	{
		OSFastMutex *head;
		OSFastMutex *tail;
	};
	static_assert(sizeof(OSFastMutexQueue) == 8, "OSFastMutexQueue Size");

	struct OSFastMutexLink
	{
		OSFastMutex *next;
		OSFastMutex *prev;
	};
	static_assert(sizeof(OSFastMutexLink) == 8, "OSFastMutexLink Size");

	struct OSFastMutex
	{
		u32 tag;
		const char *name;
		u8 unk[4];
		OSThreadSimpleQueue queue;
		OSFastMutexLink link;
		u8 unk2[16];
	};
	static_assert(sizeof(OSFastMutex) == 0x2C, "OSFastMutex Size");

	#pragma pack(push, 1)
	struct OSThread
	{
		OSContext context;
		u32 tag;
		OSThreadState state;
		OSThreadAttributes attr;
		u16 id;

		s32 suspendCounter;
		s32 priority;
		s32 basePriority;
		s32 exitValue;

		u8 unk1[0x35C - 0x338];

		OSThreadQueue *queue;
		OSThreadLink link;
		OSThreadQueue joinQueue;
		OSMutex *mutex;
		OSMutexQueue mutexQueue;
		OSThreadLink activeLink;

		void *stackStart;
		void *stackEnd;
		OSThreadEntryPointFn entryPoint;

		u8 unk2[0x57c - 0x3a0];
		u32 specific[0x10];
		u8 unk3[0x5c0 - 0x5bc];

		const char *name;
		u8 unk4[0x4];

		void *userStackPointer;
		OSThreadCleanupCallbackFn cleanupCallback;
		OSThreadDeallocatorFn deallocator;

		int cancelState;
		OSThreadRequest requestFlag;

		s32 needSuspend;
		s32 suspendResult;
		OSThreadQueue suspendQueue;

		u8 unk5[0x69c - 0x5f4];
	};
	#pragma pack(pop)
	static_assert(sizeof(OSThread) == 0x69c, "OSThread Size");

#ifdef __cplusplus
}
#endif

#endif // __THREAD_DEFS_H_