#ifndef _MAIN_H_
#define _MAIN_H_

#include "common/types.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/padscore_functions.h"

/* Main */
#ifdef __cplusplus
extern "C" {
#endif

//! C wrapper for our C++ functions
int App_Main(void);

extern const char *HOST_IP;

//For the Input Viewer Thread
extern VPADData g_currentInputData;
extern KPADData g_currentInputDataKPAD;
extern s32 g_proControllerChannel;

//Function Hooks-Auto Splitter
extern u8 g_newRun;
extern u8 g_endRun;
extern u8 g_doSplit;
extern u8 g_isLoading;

#ifdef __cplusplus
}
#endif

#endif
