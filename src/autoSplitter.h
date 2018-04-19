#ifndef _AUTOSPLITTER_H_
#define _AUTOSPLITTER_H_

#include "common/types.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/padscore_functions.h"

/* Main */
#ifdef __cplusplus
extern "C" {
#endif

//! C wrapper for our C++ functions
void start_tools(void);

#ifdef __cplusplus
}
#endif

#endif
