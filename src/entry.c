#include <string.h>
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
#include "autoSplitter.h"
#include "main.h"

//Entry point for the App
int __entry_menu(int argc, char **argv)
{
	//Main Menu is loading, do nothing
	if (OSGetTitleID != 0 && (
		OSGetTitleID() == 0x0005001010040200 //Wii U Menu PAL
		|| OSGetTitleID() == 0x0005001010040100 //Wii U Menu NTSC
		|| OSGetTitleID() == 0x0005001010040000)) //Wii U Menu JPN
	{
		InitOSFunctionPointers();
		InitSocketFunctionPointers();
		InitVPadFunctionPointers(); //for restoring VPAD Read
		InitPadScoreFunctionPointers(); //for restoring WPAD/KPAD functions

		//Init twice is needed so logging works properly
		log_init(HOST_IP); //Opens a connection to the host for logging purposes
		log_deinit();
		log_init(HOST_IP);

		log_printf("Loading Wii U Main Menu\n");

		RestoreGameInstructions(); //Restore custom game hooks
		RestoreInstructions(); //Restore original VPAD Read function and socket lib finish (and others)

		log_deinit(); //Closes the log connection again

		return EXIT_RELAUNCH_ON_LOAD; 	//EXIT_RELAUNCH_ON_LOAD restarts this app ONCE every time another app loads (menu, game, HBL, etc.)
	}
	
	//Another app is starting (not HBL nor Main Menu), assuming it's a game (could be bad if its not?)
	if (OSGetTitleID != 0 &&
		OSGetTitleID() != 0x000500101004A200 && // mii maker eur (HBL in disguise)
		OSGetTitleID() != 0x000500101004A100 && // mii maker usa
		OSGetTitleID() != 0x000500101004A000)   // mii maker jpn
	{
		InitOSFunctionPointers();
		InitSocketFunctionPointers();
		InitVPadFunctionPointers(); //for patching VPAD Read
		InitGX2FunctionPointers(); //for patching GX2 functions
		InitPadScoreFunctionPointers(); //for patching WPAD/KPAD functions

		SetupKernelCallback(); //Needed for PatchMethodHooks()

		//Ensure fresh start
		memset(&g_currentInputData, 0, sizeof(VPADData));	
		memset(&g_currentInputDataKPAD, 0, sizeof(KPADData));
		g_proControllerChannel = -1;

		g_newRun = 0;
		g_endRun = 0;
		g_doSplit = 0;
		g_isLoading = 0;

		//Init twice is needed so logging works properly
		log_init(HOST_IP);
		log_deinit();
		log_init(HOST_IP);

		//Patch VPAD Read,socket lib finish and others to inject custom code into them
		PatchMethodHooks();

		log_printf("Launching a game - Title ID: 0x%16X\n", OSGetTitleID());

		start_tools(); //Jump to start_tools function

		log_printf("Auto Splitter thread started!\n");

		log_deinit();

		return EXIT_RELAUNCH_ON_LOAD;
	}


    //! ************************************************************************************
    //! *                 Jump to our application if Mii Maker is running                  *
    //! ************************************************************************************
	return App_Main();
}
