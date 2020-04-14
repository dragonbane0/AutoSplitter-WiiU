/****************************************************************************
 * Copyright (C) 2018 Marwin Misselhorn
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

#ifndef _AUTO_SPLITTER_SYSTEM_H_
#define _AUTO_SPLITTER_SYSTEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <gctypes.h>

	typedef struct
	{
		u8 enabled;
		u8 usePtr;
		u8 addressType;
		u32 baseAddress;
		u8 offsetCount;
		u32* offsetsPtr;
		u8 comparisonType;
		void* valuePtr;
	} SplitterCondition;

int SetupAutoSplitterSystem(const char *pJsonString);
int RunAutoSplitterSystem(u32 inSplitIndex, u8* outNewRun, u8* outEndRun, u8* outDoSplit, u8* outLoadingStatus);
void DestroyAutoSplitterSystem();

#ifdef __cplusplus
}
#endif

#endif /* _AUTO_SPLITTER_SYSTEM_H_ */
