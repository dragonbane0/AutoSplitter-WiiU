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

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdarg.h>
#include <gctypes.h>
#include "autoSplitterSystem.h"
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
#include "main.h"
#include "rapidjson/document.h"

static std::vector<SplitterCondition> startConditions;
static std::vector<SplitterCondition> endConditions;
static std::vector<SplitterCondition> loadingConditions;
static std::vector<std::vector<SplitterCondition>> splitList;


void DestroyAllConditions(std::vector<SplitterCondition> conditionList)
{
	for (std::vector<SplitterCondition>::iterator condition = conditionList.begin(); condition != conditionList.end(); ++condition)
	{
		if (condition->offsetsPtr)
			free(condition->offsetsPtr);

		condition->offsetsPtr = NULL;

		if (condition->valuePtr)
			free(condition->valuePtr);

		condition->valuePtr = NULL;
	}
}

void DestroyAutoSplitterSystem()
{
	DestroyAllConditions(startConditions);
	startConditions.clear();
	std::vector<SplitterCondition>().swap(startConditions);

	DestroyAllConditions(endConditions);
	endConditions.clear();
	std::vector<SplitterCondition>().swap(endConditions);

	DestroyAllConditions(loadingConditions);
	loadingConditions.clear();
	std::vector<SplitterCondition>().swap(loadingConditions);

	//Destroy all splits
	for (std::vector<std::vector<SplitterCondition>>::iterator conditionList = splitList.begin(); conditionList != splitList.end(); ++conditionList)
	{
		DestroyAllConditions((*conditionList));
		conditionList->clear();
		std::vector<SplitterCondition>().swap(*conditionList);
	}

	splitList.clear();
	std::vector<std::vector<SplitterCondition>>().swap(splitList);
}

int SetupAutoSplitterSystem(const char *pJsonString)
{
	//Parse json string
 	rapidjson::Document document;

	if (document.Parse(pJsonString).HasParseError())
	{
		log_print("Json is faulty!\n");
		return -1;
	}
	else
	{
		//log_printf("Json load success: Member count root: %i", document.MemberCount());

		if (!document.IsObject())
		{
			log_print("[Json Parse Error] No object as first item!\n");
			return -1;
		}

		rapidjson::Value::MemberIterator splits = document.FindMember("Splits");

		if (splits == document.MemberEnd())
		{
			log_print("[Json Parse Error] Splits member wasn't found!\n");
			return -1;
		}

		if (!splits->value.IsArray())
		{
			log_print("[Json Parse Error] Splits member wasn't an array!\n");
			return -1;
		}

		if (splits->value.Size() < 3)
		{
			log_print("[Json Parse Error] Splits member is smaller than 3! Need at least the 3 required splits!\n");
			return -1;
		}

		//log_printf("Splits item count: %i", splits->value.Size());	

		int index = 0;

		//Parse Splits array
		for (rapidjson::Value::ConstValueIterator itrSplits = splits->value.Begin(); itrSplits != splits->value.End(); ++itrSplits)
		{
			rapidjson::Value::ConstMemberIterator conditions = itrSplits->FindMember("Conditions");

			if (conditions == itrSplits->MemberEnd())
			{
				log_print("[Json Parse Error] Conditions member wasn't found!\n");
				return -1;
			}

			if (!conditions->value.IsArray())
			{
				log_print("[Json Parse Error] Conditions member wasn't an array!\n");
				return -1;
			}

			if (conditions->value.Size() == 0)
			{
				log_print("[Json Parse Error] Conditions member has a size of 0! Need at least 1 condition!\n");
				return -1;
			}

			std::vector<SplitterCondition> conditionList;

			//Parse Conditions array
			for (rapidjson::Value::ConstValueIterator itrCond = conditions->value.Begin(); itrCond != conditions->value.End(); ++itrCond)
			{
				SplitterCondition newCond;
				newCond.offsetCount = 0;
				newCond.offsetsPtr = 0;
				newCond.enabled = 1; //all conditions are activated by default

				if (!itrCond->IsObject())
				{
					log_print("[Json Parse Error] Conditions entry wasn't an object!\n");
					return -1;
				}

				if (!itrCond->HasMember("UsePointer") || !itrCond->HasMember("AddressType") || !itrCond->HasMember("BaseAddress") || !itrCond->HasMember("ComparisonType") || !itrCond->HasMember("Value"))
				{
					log_print("[Json Parse Error] Conditions entry lacks a required member!\n");
					return -1;
				}

				newCond.usePtr = itrCond->FindMember("UsePointer")->value.GetBool();
				//log_printf("UsePointer: %i", newCond.usePtr);

				if (newCond.usePtr == 1 && !itrCond->HasMember("Offsets"))
				{
					log_print("[Json Parse Error] Conditions entry lacks a required member!\n");
					return -1;
				}

				newCond.addressType = (u8)itrCond->FindMember("AddressType")->value.GetUint();
				//log_printf("AddressType: %i", newCond.addressType);

				newCond.baseAddress = (u32)itrCond->FindMember("BaseAddress")->value.GetUint();
				//log_printf("BaseAddress: %i", newCond.baseAddress);

				if (newCond.usePtr == 1)
				{
					//Parse offsets and create a dynamic array
					rapidjson::Value::ConstMemberIterator offPointers = itrCond->FindMember("Offsets");

					if (offPointers == itrCond->MemberEnd())
					{
						log_print("[Json Parse Error] Offset member wasn't found!\n");
						return -1;
					}

					if (!offPointers->value.IsArray())
					{
						log_print("[Json Parse Error] Offset member wasn't an array!\n");
						return -1;
					}

					if (offPointers->value.Size() == 0)
					{
						log_print("[Json Parse Error] Offset member has a size of 0! Need at least 1 offset!\n");
						return -1;
					}

					//Create fitting array
					newCond.offsetCount = offPointers->value.Size();
					newCond.offsetsPtr = new u32[newCond.offsetCount];

					//Parse offset array
					u32 offsetIndex = 0;
					for (rapidjson::Value::ConstValueIterator itrOffsets = offPointers->value.Begin(); itrOffsets != offPointers->value.End(); ++itrOffsets)
					{
						*(newCond.offsetsPtr + offsetIndex) = itrOffsets->GetUint();
						offsetIndex++;
					}	

					//Test dump
					/*
					for (int n = 0; n < newCond.offsetCount; n++)
					{
						log_printf("Offset: %i", *(newCond.offsetsPtr + n));
					}
					*/
				}

				newCond.comparisonType = (u8)itrCond->FindMember("ComparisonType")->value.GetUint();
				//log_printf("ComparisonType: %i", newCond.comparisonType);

				//Create pointer to value depending on its type
				switch (newCond.addressType)
				{
				case 0: //byte
				{
					newCond.valuePtr = new u8;

					*((u8*)(newCond.valuePtr)) = (u8)itrCond->FindMember("Value")->value.GetUint();
					//log_printf("1 byte: %i", *(u8*)newCond.valuePtr);
					break;
				}
				case 1: //2 byte
				{
					newCond.valuePtr = new u16;

					*((u16*)(newCond.valuePtr)) = (u16)itrCond->FindMember("Value")->value.GetUint();
					//log_printf("2 byte: %i", *(u16*)newCond.valuePtr);
					break;
				}
				case 2: //4 byte
				{
					newCond.valuePtr = new u32;

					*((u32*)(newCond.valuePtr)) = (u32)itrCond->FindMember("Value")->value.GetUint();
					//log_printf("4 byte: %X", *(u32*)newCond.valuePtr);
					break;
				}
				case 3: //float
				{
					newCond.valuePtr = new float;

					*((float*)(newCond.valuePtr)) = (float)itrCond->FindMember("Value")->value.GetFloat();
					//log_printf("Float: %d", *(float*)newCond.valuePtr);
					break;
				}
				case 4: //string
				{
					int sizeString = itrCond->FindMember("Value")->value.GetStringLength() + 1;
					//log_printf("length: %i", sizeString);

					newCond.valuePtr = new char[sizeString];
					memset(newCond.valuePtr, 0, sizeString);

					strcpy((char*)newCond.valuePtr, itrCond->FindMember("Value")->value.GetString());

					//log_printf("String: %s", newCond.valuePtr);
					break;
				}
				default:
				{
					log_print("[Json Parse Error] Invalid address type found\n");
					return -1;
				}
				}

				//Special override to ignore a condition at runtime
				if (newCond.usePtr == 0 && newCond.addressType == 0x00 && newCond.baseAddress == 0xFFFFFFFF && newCond.comparisonType == 0x00 && *(u8*)newCond.valuePtr == 0xFF)
				{
					log_printf("Special override triggered for split: %i", index);
					newCond.enabled = 0;
				}

				conditionList.push_back(newCond);
			}

			switch (index)
			{
			case 0: //start split

				startConditions = conditionList;
				break;

			case 1: //end split

				endConditions = conditionList;
				break;

			case 2: //loading split
				
				loadingConditions = conditionList;
				break;

			default: //a generic split

				splitList.push_back(conditionList);
				break;
			}

			index++;
		}
		
		return 0;
	}
}

bool IsBitSet(u8 b, int pos)
{
	return (b & (1 << pos)) != 0;
}

bool RetrievePointer(u32* &pointer)
{
	if (*pointer > 0x00000000 && *pointer < 0xF6000000 && OSIsAddressValid(*pointer) == 1)
	{
		pointer = (u32*)*pointer;
		return true;
	}

	//log_printf("Ptr can not be read: %X", *pointer);

	return false;
}

bool Test8(u8* address, u8* value, u8 type)
{
	//log_printf("Compare8 target: %i with value: %i, comparison: %i", *address, *value, type);

	switch (type)
	{
	case 0: //==
	{
		return (*address == *value);
		break;
	}
	case 1: //!=
	{
		return (*address != *value);
		break;
	}
	case 2: //>
	{
		return (*address > *value);
		break;
	}
	case 3: //<
	{
		return (*address < *value);
		break;
	}
	case 4: //>=
	{
		return (*address >= *value);
		break;
	}
	case 5: //<=
	{
		return (*address <= *value);
		break;
	}
	case 6: //bit true
	{
		return (IsBitSet(*address, *value));
		break;
	}
	case 7: //bit false
	{
		return (!IsBitSet(*address, *value));
		break;
	}
	}
	
	return false;
}

bool Test16(u16* address, u16* value, u8 type)
{
	//log_printf("Compare16 target: %i with value: %i, comparison: %i", *address, *value, type);

	switch (type)
	{
	case 0: //==
	{
		return (*address == *value);
		break;
	}
	case 1: //!=
	{
		return (*address != *value);
		break;
	}
	case 2: //>
	{
		return (*address > *value);
		break;
	}
	case 3: //<
	{
		return (*address < *value);
		break;
	}
	case 4: //>=
	{
		return (*address >= *value);
		break;
	}
	case 5: //<=
	{
		return (*address <= *value);
		break;
	}
	}

	return false;
}

bool Test32(u32* address, u32* value, u8 type)
{
	//log_printf("Compare32 target: %i with value: %i, comparison: %i", *address, *value, type);

	switch (type)
	{
	case 0: //==
	{
		return (*address == *value);
		break;
	}
	case 1: //!=
	{
		return (*address != *value);
		break;
	}
	case 2: //>
	{
		return (*address > *value);
		break;
	}
	case 3: //<
	{
		return (*address < *value);
		break;
	}
	case 4: //>=
	{
		return (*address >= *value);
		break;
	}
	case 5: //<=
	{
		return (*address <= *value);
		break;
	}
	}

	return false;
}

bool TestFloat(float* address, float* value, u8 type)
{
	//log_printf("CompareFloat target: %d with value: %d, comparison: %i", *address, *value, type);

	switch (type)
	{
	case 0: //==
	{
		return (*address == *value);
		break;
	}
	case 1: //!=
	{
		return (*address != *value);
		break;
	}
	case 2: //>
	{
		return (*address > *value);
		break;
	}
	case 3: //<
	{
		return (*address < *value);
		break;
	}
	case 4: //>=
	{
		return (*address >= *value);
		break;
	}
	case 5: //<=
	{
		return (*address <= *value);
		break;
	}
	}

	return false;
}

bool TestString(const char* address, const char* value, u8 type)
{
	//log_printf("CompareString target: %s with value: %s, comparison: %i", address, value, type);

	switch (type)
	{
	case 0: //==
	{
		return (!strcmp(address, value));
		break;
	}
	case 1: //!=
	{
		return (strcmp(address, value));
		break;
	}
	}

	return false;
}

bool IsConditionMatch(SplitterCondition* condition)
{
	u32* finalReadAdd = (u32*)condition->baseAddress;

	//Use offset chain to find final read address from pointers
	if (condition->usePtr == 1)
	{
		for (u32 n = 0; n < condition->offsetCount; n++)
		{
			u32 offset = *(condition->offsetsPtr + n);

			if (!RetrievePointer(finalReadAdd))
				return false;

			finalReadAdd = (u32*)((u32)finalReadAdd + offset);

			//log_printf("Handled ptr search, level %i, result post offset: %p, offset: %X", n, finalReadAdd, offset);
		}
	}

	bool evaluationResult = false;

	switch (condition->addressType)
	{
	case 0: //byte
	{
		evaluationResult = Test8((u8*)finalReadAdd, (u8*)condition->valuePtr, condition->comparisonType);
		break;
	}
	case 1: //2 byte
	{
		evaluationResult = Test16((u16*)finalReadAdd, (u16*)condition->valuePtr, condition->comparisonType);
		break;
	}
	case 2: //4 byte
	{
		evaluationResult = Test32((u32*)finalReadAdd, (u32*)condition->valuePtr, condition->comparisonType);
		break;
	}
	case 3: //float
	{
		evaluationResult = TestFloat((float*)finalReadAdd, (float*)condition->valuePtr, condition->comparisonType);
		break;
	}
	case 4: //string
	{
		evaluationResult = TestString((const char*)finalReadAdd, (const char*)condition->valuePtr, condition->comparisonType);
		break;
	}
	}
	
	return evaluationResult;
}

//Returns 1 if success, 0 if fail, 255 if disabled
u8 AllConditionsMatch(std::vector<SplitterCondition> conditionList)
{
	for (std::vector<SplitterCondition>::iterator condition = conditionList.begin(); condition != conditionList.end(); ++condition)
	{
		if (condition->enabled == 0)
			return 0xFF;

		if (!IsConditionMatch(&(*condition)))
			return 0;
	}

	return 1;
}

int RunAutoSplitterSystem(u32 inSplitIndex, u8* outNewRun, u8* outEndRun, u8* outDoSplit, u8* outLoadingStatus)
{
	//Check conditions
	*outNewRun = AllConditionsMatch(startConditions);
	*outEndRun = AllConditionsMatch(endConditions);
	*outLoadingStatus = AllConditionsMatch(loadingConditions);

	if (splitList.size() > 0)
	{
		if (inSplitIndex < splitList.size())
		{
			*outDoSplit = AllConditionsMatch(splitList[inSplitIndex]);
		}
	}

	return 0;
}
