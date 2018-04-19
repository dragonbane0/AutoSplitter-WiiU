#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>

#include <unistd.h>
#include <fcntl.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/ax_functions.h"
#include "patcher/function_hooks.h"
#include "fs/fs_utils.h"
#include "fs/sd_fat_devoptab.h"
#include "kernel/kernel_functions.h"
#include "system/exception_handler.h"
#include "system/memory.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"

//Global Logic Gate Var
static int app_launched = 0;

//Global Extern Vars
const char *HOST_IP = "192.168.2.104";

VPADData g_currentInputData;
KPADData g_currentInputDataKPAD;
s32 g_proControllerChannel = -1;

u8 g_newRun = 0;
u8 g_endRun = 0;
u8 g_doSplit = 0;
u8 g_isLoading = 0;

//Entry point for the app itself
int App_Main(void)
{
        //!*******************************************************************
        //!                   Initialize function pointers                   *
        //!*******************************************************************
		InitOSFunctionPointers(); //various uses
		InitSocketFunctionPointers(); //for logging
		InitSysFunctionPointers(); //for SYSLaunchMenu
		InitVPadFunctionPointers(); //for restoring VPAD Read
		InitGX2FunctionPointers(); //Graphics e.g. GX2WaitForVsync
		InitPadScoreFunctionPointers(); //for restoring WPAD/KPAD functions

		SetupKernelCallback(); //for RestoreInstructions() and printing RPX Name

		//Init twice is needed so logging works properly
        log_init(HOST_IP);
		log_deinit();
		log_init(HOST_IP);

		log_printf("TCPGecko Auto Splitter App was launched\n");

        log_printf("Current RPX Name: %s\n", cosAppXmlInfoStruct.rpx_name); //Prints the current RPX file running (RPX = EXE)
	    log_printf("App Launched Value: %i\n", app_launched); //Dump the app_launched variable for debugging purposes


		//Return to HBL if app is launched a second time (by using the Mii Maker Channel from the System Menu)
		if (app_launched == 1)
		{
			RestoreGameInstructions(); //Restore custom game hooks
			RestoreInstructions(); //Restore original VPAD Read function and socket lib finish (and others)

			log_printf("Returning to HBL\n");
			log_deinit();
			return EXIT_SUCCESS; //Returns to HBL
		}

		//First time launch from inside the HBL
		log_printf("TCPGecko Auto Splitter prepared for launch\n");

		log_deinit(); //Clean up log stuff
	
		app_launched = 1;

		SYSLaunchMenu(); //Launches the Wii U Main Menu (System Menu)

        return EXIT_RELAUNCH_ON_LOAD;
}