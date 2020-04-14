/****************************************************************************
 * Copyright (C) 2016 Maschell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>
#include <gctypes.h>
#include "function_hooks.h"
#include "dynamic_libs/aoc_functions.h"
#include "dynamic_libs/ax_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/acp_functions.h"
#include "dynamic_libs/syshid_functions.h"
#include "kernel/kernel_functions.h"
#include "utils/logger.h"
#include "common/common.h"
#include "autoSplitter.h"
#include "autoSplitterSystem.h"
#include "main.h"

#define LIB_CODE_RW_BASE_OFFSET                         0xC1000000
#define CODE_RW_BASE_OFFSET                             0x00000000
#define DEBUG_LOG_DYN                                   0

#define USE_EXTRA_LOG_FUNCTIONS   0

#define DECL(res, name, ...) \
        res (* real_ ## name)(__VA_ARGS__) __attribute__((section(".data"))); \
        res my_ ## name(__VA_ARGS__)


#define PRINT_TEXT1(x, y, str) { OSScreenPutFontEx(1, x, y, str); OSScreenPutFontEx(0, x, y, str); }
#define PRINT_TEXT2(x, y, _fmt, ...) { __os_snprintf(msg, 80, _fmt, __VA_ARGS__); OSScreenPutFontEx(0, x, y, msg);OSScreenPutFontEx(1, x, y, msg); }

//DKC Vars
char dkc_currentLevelName[4];
u32 dkc_currentIslandID = 0;
u8 dkc_splitGate = 0;


//Gets called whenever the system polls a generic controller (high level)
DECL(s32, KPADRead, s32 chan, KPADData *data, u32 size)
{
	s32 result = real_KPADRead(chan, data, size); //Read the actual inputs from the real function

	if (result == 0)
	{
		if (data->device_type > 1)
		{
			g_proControllerChannel = chan;
			g_currentInputDataKPAD = *data;
		}
		else
		{
			if (chan == g_proControllerChannel)
				g_proControllerChannel = -1;
		}
	}
	else
	{
		if (chan == g_proControllerChannel)
			g_proControllerChannel = -1;
	}

	//log_printf("read kpad channel: %i", chan);

	return result;
}

//Gets called whenever the system polls a generic controller (low level)
DECL(void, WPADRead, s32 chan, KPADData *data)
{
	real_WPADRead(chan, data);

	if (chan == g_proControllerChannel)
		g_currentInputDataKPAD = *data;

	//log_printf("read wpad channel: %i", chan);
}

//Test if controller is connected
DECL(s32, WPADProbe, s32 chan, u32 *pad_type)
{
	s32 result = real_WPADProbe(chan, pad_type);

	if (result == 0)
	{
		if (*pad_type == 2) //Classic/Pro Controller
		{
			g_proControllerChannel = chan;
		}
		else
		{
			if (chan == g_proControllerChannel)
				g_proControllerChannel = -1;
		}
	}
	else
	{
		if (chan == g_proControllerChannel) 
			g_proControllerChannel = -1;
	}

	return result;
}


//Gets called whenever the system polls the WiiU Gamepad
DECL(int, VPADRead, int chan, VPADData *buffer, u32 buffer_size, s32 *error) 
{
    int result = real_VPADRead(chan, buffer, buffer_size, error); //Read the actual inputs from the real function

	if (chan == 0) //Only read inputs from Controller Port 0 for now
		g_currentInputData = *buffer;
    
    return result;
}

//Re-direct socket lib finish into nothing so games can't kill the socket library and our connection by accident
DECL(int, socket_lib_finish, void)
{
	return 0;
}

//Gets called on process exit
DECL(void, _Exit, void)
{
	//Cleanup
	DestroyAutoSplitterSystem();

	real__Exit();
}

/* *****************************************************************************
 * Creates function pointer array
 * ****************************************************************************/
#define MAKE_MAGIC(x, lib,functionType) { (unsigned int) my_ ## x, (unsigned int) &real_ ## x, lib, # x,0,0,functionType,0}

static struct hooks_magic_t 
{
    const unsigned int replaceAddr;
    const unsigned int replaceCall;
    const unsigned int library;
    const char functionName[50];
    unsigned int realAddr;
    unsigned int restoreInstruction;
    unsigned char functionType;
    unsigned char alreadyPatched;
} 

method_hooks[] = 
{
    MAKE_MAGIC(VPADRead,                             LIB_VPAD,STATIC_FUNCTION),
	MAKE_MAGIC(socket_lib_finish,                    LIB_NSYSNET,STATIC_FUNCTION),
	MAKE_MAGIC(_Exit,								 LIB_CORE_INIT,STATIC_FUNCTION),
	//MAKE_MAGIC(KPADRead,							 LIB_PADSCORE,STATIC_FUNCTION),
	//MAKE_MAGIC(WPADRead,							 LIB_PADSCORE,STATIC_FUNCTION),
	//MAKE_MAGIC(WPADProbe,							 LIB_PADSCORE,DYNAMIC_FUNCTION),
};

//! buffer to store our 7 instructions needed for our replacements
//! the code will be placed in the address of that buffer - CODE_RW_BASE_OFFSET
//! avoid this buffer to be placed in BSS and reset on start up
volatile unsigned int dynamic_method_calls[sizeof(method_hooks) / sizeof(struct hooks_magic_t) * 7] __attribute__((section(".data")));

/*
*Patches a function that is loaded at the start of each application. Its not required to restore, at least when they are really dynamic.
* "normal" functions should be patch with the normal patcher. Current Code by Maschell with the help of dimok.
*/
void PatchMethodHooks(void)
{
    /* Patch branches to it.  */
    volatile unsigned int *space = &dynamic_method_calls[0];

    int method_hooks_count = sizeof(method_hooks) / sizeof(struct hooks_magic_t);

    u32 skip_instr = 1;
    u32 my_instr_len = 6;
    u32 instr_len = my_instr_len + skip_instr;
    u32 flush_len = 4*instr_len;
    for(int i = 0; i < method_hooks_count; i++)
    {
        log_printf("Patching %s ...",method_hooks[i].functionName);
        if(method_hooks[i].functionType == STATIC_FUNCTION && method_hooks[i].alreadyPatched == 1){
            if(isDynamicFunction((u32)OSEffectiveToPhysical((void*)method_hooks[i].realAddr))){
                log_printf(" The function %s is a dynamic function. Please fix that <3 ... ", method_hooks[i].functionName);
                method_hooks[i].functionType = DYNAMIC_FUNCTION;
            }else{
                log_printf(" skipped. Its already patched\n", method_hooks[i].functionName);
                space += instr_len;
                continue;
            }
        }

        u32 physical = 0;
        unsigned int repl_addr = (unsigned int)method_hooks[i].replaceAddr;
        unsigned int call_addr = (unsigned int)method_hooks[i].replaceCall;

        unsigned int real_addr = GetAddressOfFunction(method_hooks[i].functionName,method_hooks[i].library);

        if(!real_addr){
            log_printf("Error. OSDynLoad_FindExport failed for %s\n", method_hooks[i].functionName);
            space += instr_len;
            continue;
        }

        if(DEBUG_LOG_DYN)log_printf("%s is located at %08X!\n", method_hooks[i].functionName,real_addr);

        physical = (u32)OSEffectiveToPhysical((void*)real_addr);
        if(!physical){
             log_printf("Error. Something is wrong with the physical address\n");
             space += instr_len;
             continue;
        }

        if(DEBUG_LOG_DYN)log_printf("%s physical is located at %08X!\n", method_hooks[i].functionName,physical);

        bat_table_t my_dbat_table;
        if(DEBUG_LOG_DYN)log_printf("Setting up DBAT\n");
        KernelSetDBATsForDynamicFuction(&my_dbat_table,physical);

        //log_printf("Setting call_addr to %08X\n",(unsigned int)(space) - CODE_RW_BASE_OFFSET);
        *(volatile unsigned int *)(call_addr) = (unsigned int)(space) - CODE_RW_BASE_OFFSET;

        // copy instructions from real function.
        u32 offset_ptr = 0;
        for(offset_ptr = 0;offset_ptr<skip_instr*4;offset_ptr +=4){
             if(DEBUG_LOG_DYN)log_printf("(real_)%08X = %08X\n",space,*(volatile unsigned int*)(physical+offset_ptr));
            *space = *(volatile unsigned int*)(physical+offset_ptr);
            space++;
        }

        //Only works if skip_instr == 1
        if(skip_instr == 1){
            // fill the restore instruction section
            method_hooks[i].realAddr = real_addr;
            method_hooks[i].restoreInstruction = *(volatile unsigned int*)(physical);
        }else{
            log_printf("Error. Can't save %s for restoring!\n", method_hooks[i].functionName);
        }

        //adding jump to real function
        /*
            90 61 ff e0     stw     r3,-32(r1)
            3c 60 12 34     lis     r3,4660
            60 63 56 78     ori     r3,r3,22136
            7c 69 03 a6     mtctr   r3
            80 61 ff e0     lwz     r3,-32(r1)
            4e 80 04 20     bctr*/
        *space = 0x9061FFE0;
        space++;
        *space = 0x3C600000 | (((real_addr + (skip_instr * 4)) >> 16) & 0x0000FFFF); // lis r3, real_addr@h
        space++;
        *space = 0x60630000 |  ((real_addr + (skip_instr * 4)) & 0x0000ffff); // ori r3, r3, real_addr@l
        space++;
        *space = 0x7C6903A6; // mtctr   r3
        space++;
        *space = 0x8061FFE0; // lwz     r3,-32(r1)
        space++;
        *space = 0x4E800420; // bctr
        space++;
        DCFlushRange((void*)(space - instr_len), flush_len);
        ICInvalidateRange((unsigned char*)(space - instr_len), flush_len);

        //setting jump back
        unsigned int replace_instr = 0x48000002 | (repl_addr & 0x03fffffc);
        *(volatile unsigned int *)(physical) = replace_instr;
        ICInvalidateRange((void*)(real_addr), 4);

        //restore my dbat stuff
        KernelRestoreDBATs(&my_dbat_table);

        method_hooks[i].alreadyPatched = 1;

        log_printf("done!\n");
    }
    log_print("Done with patching all functions!\n");
}

/* ****************************************************************** */
/*                  RESTORE ORIGINAL INSTRUCTIONS                     */
/* ****************************************************************** */
void RestoreInstructions(void)
{
    bat_table_t table;
    log_printf("Restore functions!\n");
    int method_hooks_count = sizeof(method_hooks) / sizeof(struct hooks_magic_t);
    for(int i = 0; i < method_hooks_count; i++)
    {
        log_printf("Restoring %s ...",method_hooks[i].functionName);
        if(method_hooks[i].restoreInstruction == 0 || method_hooks[i].realAddr == 0){
            log_printf("Error. I dont have the information for the restore =( skip\n");
            continue;
        }

        unsigned int real_addr = GetAddressOfFunction(method_hooks[i].functionName,method_hooks[i].library);

        if(!real_addr){
            //log_printf("Error. OSDynLoad_FindExport failed for %s\n", method_hooks[i].functionName);
            continue;
        }

        u32 physical = (u32)OSEffectiveToPhysical((void*)real_addr);
        if(!physical){
            log_printf("Error. Something is wrong with the physical address\n");
            continue;
        }

        if(isDynamicFunction(physical)){
             log_printf("Error. Its a dynamic function. We don't need to restore it! %s\n",method_hooks[i].functionName);
        }else{
            KernelSetDBATs(&table);

            *(volatile unsigned int *)(LIB_CODE_RW_BASE_OFFSET + method_hooks[i].realAddr) = method_hooks[i].restoreInstruction;
            DCFlushRange((void*)(LIB_CODE_RW_BASE_OFFSET + method_hooks[i].realAddr), 4);
            ICInvalidateRange((void*)method_hooks[i].realAddr, 4);
            log_printf(" done\n");
            KernelRestoreDBATs(&table);
        }
        method_hooks[i].alreadyPatched = 0; // In case a
    }
    KernelRestoreInstructions();
    log_print("Done with restoring all functions!\n");
}

int isDynamicFunction(unsigned int physicalAddress){
    if((physicalAddress & 0x80000000) == 0x80000000){
        return 1;
    }
    return 0;
}

unsigned int GetAddressOfFunction(const char * functionName,unsigned int library){
    unsigned int real_addr = 0;

	unsigned int rpl_handle = 0;
	if (library == LIB_CORE_INIT) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_CORE_INIT\n", functionName);
		if (coreinit_handle == 0) { log_print("LIB_CORE_INIT not aquired\n"); return 0; }
		rpl_handle = coreinit_handle;
	}
	else if (library == LIB_NSYSNET) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_NSYSNET\n", functionName);
		if (nsysnet_handle == 0) { log_print("LIB_NSYSNET not aquired\n"); return 0; }
		rpl_handle = nsysnet_handle;
	}
	else if (library == LIB_GX2) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_GX2\n", functionName);
		if (gx2_handle == 0) { log_print("LIB_GX2 not aquired\n"); return 0; }
		rpl_handle = gx2_handle;
	}
	else if (library == LIB_AOC) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_AOC\n", functionName);
		if (aoc_handle == 0) { log_print("LIB_AOC not aquired\n"); return 0; }
		rpl_handle = aoc_handle;
	}
	else if (library == LIB_AX) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_AX\n", functionName);
		if (sound_handle == 0) { log_print("LIB_AX not aquired\n"); return 0; }
		rpl_handle = sound_handle;
	}
	else if (library == LIB_FS) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_FS\n", functionName);
		if (coreinit_handle == 0) { log_print("LIB_FS not aquired\n"); return 0; }
		rpl_handle = coreinit_handle;
	}
	else if (library == LIB_OS) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_OS\n", functionName);
		if (coreinit_handle == 0) { log_print("LIB_OS not aquired\n"); return 0; }
		rpl_handle = coreinit_handle;
	}
	else if (library == LIB_PADSCORE) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_PADSCORE\n", functionName);
		if (padscore_handle == 0) { log_print("LIB_PADSCORE not aquired\n"); return 0; }
		rpl_handle = padscore_handle;
	}
	else if (library == LIB_SOCKET) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_SOCKET\n", functionName);
		if (nsysnet_handle == 0) { log_print("LIB_SOCKET not aquired\n"); return 0; }
		rpl_handle = nsysnet_handle;
	}
	else if (library == LIB_SYS) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_SYS\n", functionName);
		if (sysapp_handle == 0) { log_print("LIB_SYS not aquired\n"); return 0; }
		rpl_handle = sysapp_handle;
	}
	else if (library == LIB_VPAD) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_VPAD\n", functionName);
		if (vpad_handle == 0) { log_print("LIB_VPAD not aquired\n"); return 0; }
		rpl_handle = vpad_handle;
	}
	else if (library == LIB_NN_ACP) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_NN_ACP\n", functionName);
		if (acp_handle == 0) { log_print("LIB_NN_ACP not aquired\n"); return 0; }
		rpl_handle = acp_handle;
	}
	else if (library == LIB_SYSHID) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_SYSHID\n", functionName);
		if (syshid_handle == 0) { log_print("LIB_SYSHID not aquired\n"); return 0; }
		rpl_handle = syshid_handle;
	}
	else if (library == LIB_VPADBASE) {
		if (DEBUG_LOG_DYN)log_printf("FindExport of %s! From LIB_VPADBASE\n", functionName);
		if (vpadbase_handle == 0) { log_print("LIB_VPADBASE not aquired\n"); return 0; }
		rpl_handle = vpadbase_handle;
	}

    if(!rpl_handle){
        log_printf("Failed to find the RPL handle for %s\n", functionName);
        return 0;
    }

    OSDynLoad_FindExport(rpl_handle, 0, functionName, &real_addr);

    if(!real_addr){
        log_printf("OSDynLoad_FindExport failed for %s\n", functionName);
        return 0;
    }

    if((u32)(*(volatile unsigned int*)(real_addr) & 0xFF000000) == 0x48000000){
        real_addr += (u32)(*(volatile unsigned int*)(real_addr) & 0x0000FFFF);
        if((u32)(*(volatile unsigned int*)(real_addr) & 0xFF000000) == 0x48000000){
            return 0;
        }
    }

    return real_addr;
}


//DKC Tropical Freeze Functions
DECL(void, DKC_LoadingFinished, void *CStateManager1, void *CStateManager2) //set loading to 0
{
	log_printf("LoadingFinished call");
	g_isLoading = 0;

	real_DKC_LoadingFinished(CStateManager1, CStateManager2);
}

DECL(u32, DKC_LaunchLevel, void *CArchitectureQueue1, void *CArchitectureQueue2) //use this to determine newRun via island id (=1) and the last island short name (=l01) after it returns
{
	u32 gameStatePtr = real_DKC_LaunchLevel(CArchitectureQueue1, CArchitectureQueue2);

	log_printf("LaunchLevel call finished! Game State Ptr: 0x%X", gameStatePtr);

	if (dkc_currentIslandID == 1)
	{
		if (!strcmp(dkc_currentLevelName, "l01"))
		{
			g_newRun = 1;
		}
	}

	return gameStatePtr;
}

DECL(int, DKC_ShowLoadingScreen, u8 ELoadDirection, void *CGameStateManager, void *CArchitectureQueue, void *IObjectStore, void *CResourceFactory) //set current island id here and loading = 1, reset gate
{
	u32 Ptr = 0;

	memcpy(&Ptr, (u32*)CGameStateManager, 4);
	memcpy(&Ptr, (u32*)(Ptr + 4), 4);
	memcpy(&dkc_currentIslandID, (u32*)Ptr, 4);

	if (ELoadDirection == 1)
	{
		log_printf("Load Selection Map - Island ID: %i", dkc_currentIslandID);
	}
	else if (ELoadDirection == 0)
	{
		log_printf("Load Level - Island ID: %i", dkc_currentIslandID);
	}

	g_isLoading = 1;
	dkc_splitGate = 0;

	int ret = real_DKC_ShowLoadingScreen(ELoadDirection, CGameStateManager, CArchitectureQueue, IObjectStore, CResourceFactory);

	return ret;
}

DECL(const char*, DKC_GetLocalizedShortName, const void *callingClass, const char* name) //save current level short name
{
	log_printf("Localized area short name call - request: %s", name);

	if (strlen(name) > 3)
	{
		memcpy(&dkc_currentLevelName, name, 3);
		dkc_currentLevelName[3] = 0;

		if (!strcmp(dkc_currentLevelName, "l01"))
		{
			log_printf("This is Level 01!");
		}
	}

	const char* string = real_DKC_GetLocalizedShortName(callingClass, name);

	return string;
}

DECL(void, DKC_StartTransition, int ETransitionType) //check for boss level here (=b00) and if transition type = 10 set split flag1
{
	log_printf("Graphical Transition Started: %i", ETransitionType);

	if (ETransitionType == 10)
	{
		if (!strcmp(dkc_currentLevelName, "b00"))
		{
			dkc_splitGate = 1;
		}
	}

	real_DKC_StartTransition(ETransitionType);
}

DECL(void, DKC_BeatUpHandler_AcceptScriptMsg, void *CStateManager1, void *CStateManager2, const void *CScriptMsg) //if split flag1 and 0x49413036 then send Split. If final boss (islandID = 6) send runEnd
{
	u32 MsgID = 0;

	memcpy(&MsgID, (unsigned char*)CScriptMsg + 0x40, 4);

	log_printf("[BeatUpHandler] Accept Script Msg: 0x%X", MsgID);

	if (dkc_splitGate == 1 && MsgID == 0x49413036) //A boss was beatup (StopDetectionAndSetBeatup_0)
	{
		dkc_splitGate = 0;

		if (dkc_currentIslandID == 6) //Final boss was beatup
			g_endRun = 1;
		else //Some other boss was beatup
			g_doSplit = 1;
	}

	real_DKC_BeatUpHandler_AcceptScriptMsg(CStateManager1, CStateManager2, CScriptMsg);
}

DECL(void, DKC_BarrelBalloon_AcceptScriptMsg, void *CStateManager1, void *CStateManager2, const void *CScriptMsg) //if 0x49413037 and level is not boss level (!=b00) send Split
{
	u32 MsgID = 0;

	memcpy(&MsgID, (unsigned char*)CScriptMsg + 0x40, 4);

	log_printf("[BarrelBalloon] Accept Script Msg: 0x%X", MsgID);

	if (MsgID == 0x49413037) //Level End Barrel hit (AddToInventory)
	{
		if (strcmp(dkc_currentLevelName, "b00")) //confirm its not a boss level
		{
			g_doSplit = 1;
		}
	}

	real_DKC_BarrelBalloon_AcceptScriptMsg(CStateManager1, CStateManager2, CScriptMsg);
}

hooks_magic_t dkc_hooks[] = //DKC Tropical Freeze
{
	MAKE_MAGIC(DKC_LoadingFinished,					LIB_GAME,DYNAMIC_FUNCTION),
	MAKE_MAGIC(DKC_LaunchLevel,						LIB_GAME,DYNAMIC_FUNCTION),
	MAKE_MAGIC(DKC_ShowLoadingScreen,				LIB_GAME,DYNAMIC_FUNCTION),
	MAKE_MAGIC(DKC_GetLocalizedShortName,			LIB_GAME,DYNAMIC_FUNCTION),
	MAKE_MAGIC(DKC_StartTransition,					LIB_GAME,DYNAMIC_FUNCTION),
	MAKE_MAGIC(DKC_BeatUpHandler_AcceptScriptMsg,	LIB_GAME,DYNAMIC_FUNCTION),
	MAKE_MAGIC(DKC_BarrelBalloon_AcceptScriptMsg,	LIB_GAME,DYNAMIC_FUNCTION),
};

volatile unsigned int dynamic_dkc_calls[sizeof(dkc_hooks) / sizeof(struct hooks_magic_t) * 7] __attribute__((section(".data")));


static const int totalGameHookArrays = 1;

/*
*Patches a game function
*/
void PatchGameHooks(void)
{
	hooks_magic_t *currentHooks = 0;
	int sizeHookArray = 0;
	volatile unsigned int *space = 0; //Patch branches to it

	if (OSGetTitleID() != 0 && (OSGetTitleID() == 0x0005000010137F00 || OSGetTitleID() == 0x0005000010138300 || OSGetTitleID() == 0x0005000010144800)) //DKC Tropical Freeze
	{
		currentHooks = &dkc_hooks[0];
		sizeHookArray = sizeof(dkc_hooks);
		space = &dynamic_dkc_calls[0];

		log_printf("Patch game functions for DKC!\n");
	}
	else
	{
		log_printf("Game doesn't need function patching!\n");
		return;
	}

	int method_hooks_count = sizeHookArray / sizeof(struct hooks_magic_t);

	u32 skip_instr = 1;
	u32 my_instr_len = 6;
	u32 instr_len = my_instr_len + skip_instr;
	u32 flush_len = 4 * instr_len;
	for (int i = 0; i < method_hooks_count; i++)
	{
		log_printf("Patching %s ...", currentHooks[i].functionName);

		u32 physical = 0;
		unsigned int repl_addr = (unsigned int)currentHooks[i].replaceAddr;
		unsigned int call_addr = (unsigned int)currentHooks[i].replaceCall;

		unsigned int real_addr = GetGameAddressOfFunction(currentHooks[i].functionName);

		if (!real_addr) {
			log_printf("Error. Didnt find address for %s\n", currentHooks[i].functionName);
			space += instr_len;
			continue;
		}

		if (DEBUG_LOG_DYN)log_printf("%s is located at %08X!\n", currentHooks[i].functionName, real_addr);

		physical = (u32)OSEffectiveToPhysical((void*)real_addr);
		if (!physical) {
			log_printf("Error. Something is wrong with the physical address\n");
			space += instr_len;
			continue;
		}

		if (DEBUG_LOG_DYN)log_printf("%s physical is located at %08X!\n", currentHooks[i].functionName, physical);

		bat_table_t my_dbat_table;
		if (DEBUG_LOG_DYN)log_printf("Setting up DBAT\n");
		KernelSetDBATsForDynamicFuction(&my_dbat_table, physical);

		//log_printf("Setting call_addr to %08X\n",(unsigned int)(space) - CODE_RW_BASE_OFFSET);
		*(volatile unsigned int *)(call_addr) = (unsigned int)(space)-CODE_RW_BASE_OFFSET;

		// copy instructions from real function.
		u32 offset_ptr = 0;
		for (offset_ptr = 0; offset_ptr<skip_instr * 4; offset_ptr += 4) {
			if (DEBUG_LOG_DYN)log_printf("(real_)%08X = %08X\n", space, *(volatile unsigned int*)(physical + offset_ptr));
			*space = *(volatile unsigned int*)(physical + offset_ptr);
			space++;
		}

		//Only works if skip_instr == 1
		if (skip_instr == 1) {
			// fill the restore instruction section
			currentHooks[i].realAddr = real_addr;
			currentHooks[i].restoreInstruction = *(volatile unsigned int*)(physical);
		}
		else {
			log_printf("Error. Can't save %s for restoring!\n", currentHooks[i].functionName);
		}

		//adding jump to real function
		/*
		90 61 ff e0     stw     r3,-32(r1)
		3c 60 12 34     lis     r3,4660
		60 63 56 78     ori     r3,r3,22136
		7c 69 03 a6     mtctr   r3
		80 61 ff e0     lwz     r3,-32(r1)
		4e 80 04 20     bctr*/
		*space = 0x9061FFE0;
		space++;
		*space = 0x3C600000 | (((real_addr + (skip_instr * 4)) >> 16) & 0x0000FFFF); // lis r3, real_addr@h
		space++;
		*space = 0x60630000 | ((real_addr + (skip_instr * 4)) & 0x0000ffff); // ori r3, r3, real_addr@l
		space++;
		*space = 0x7C6903A6; // mtctr   r3
		space++;
		*space = 0x8061FFE0; // lwz     r3,-32(r1)
		space++;
		*space = 0x4E800420; // bctr
		space++;
		DCFlushRange((void*)(space - instr_len), flush_len);
		ICInvalidateRange((unsigned char*)(space - instr_len), flush_len);

		//setting jump back
		unsigned int replace_instr = 0x48000002 | (repl_addr & 0x03fffffc);
		*(volatile unsigned int *)(physical) = replace_instr;
		ICInvalidateRange((void*)(real_addr), 4);

		//restore my dbat stuff
		KernelRestoreDBATs(&my_dbat_table);

		currentHooks[i].alreadyPatched = 1;

		log_printf("done!\n");
	}

	log_print("Done with patching all game functions!\n");
}

void RestoreGameInstructions(void)
{
	log_printf("Restore all game functions!\n");

	for (int n = 0; n < totalGameHookArrays; n++)
	{
		hooks_magic_t *currentHooks = 0;
		int sizeHookArray = 0;

		if (n == 0) //DKC Tropical Freeze
		{
			currentHooks = &dkc_hooks[0];
			sizeHookArray = sizeof(dkc_hooks);
		}

		int method_hooks_count = sizeHookArray / sizeof(struct hooks_magic_t);
		for (int i = 0; i < method_hooks_count; i++)
		{
			log_printf("Restoring %s ...", currentHooks[i].functionName);
			if (currentHooks[i].restoreInstruction == 0 || currentHooks[i].realAddr == 0) {
				log_printf("Error. I dont have the information for the restore =( skip\n");
				continue;
			}

			currentHooks[i].alreadyPatched = 0;
		}
	}

	log_print("Done with restoring all game functions!\n");
}

unsigned int GetGameAddressOfFunction(const char *functionName)
{
	unsigned int real_addr = 0;

	//DKC Section
	if (!strcmp(functionName, "DKC_LoadingFinished"))
	{
		real_addr = 0x0ECB6A60;
		return real_addr;
	}
	if (!strcmp(functionName, "DKC_LaunchLevel"))
	{
		real_addr = 0x0EF7B044;
		return real_addr;
	}
	if (!strcmp(functionName, "DKC_ShowLoadingScreen"))
	{
		real_addr = 0x0EF7F29C;
		return real_addr;
	}
	if (!strcmp(functionName, "DKC_GetLocalizedShortName"))
	{
		real_addr = 0x0EC3C804;
		return real_addr;
	}
	if (!strcmp(functionName, "DKC_StartTransition"))
	{
		real_addr = 0x0EB44B04;
		return real_addr;
	}
	if (!strcmp(functionName, "DKC_BeatUpHandler_AcceptScriptMsg"))
	{
		real_addr = 0x0EFEBBF0;
		return real_addr;
	}
	if (!strcmp(functionName, "DKC_BarrelBalloon_AcceptScriptMsg"))
	{
		real_addr = 0x0EFE7270;
		return real_addr;
	}

	return 0;
}