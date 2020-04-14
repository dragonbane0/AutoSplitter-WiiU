#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "common/common.h"
#include "common/thread_defs.h"
#include "main.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "patcher/function_hooks.h"
#include "kernel/syscalls.h"
#include "system/exception_handler.h"
#include "utils/logger.h"
#include "autoSplitterSystem.h"

struct pygecko_bss_t //Helper struct for threads (supports storing an error code)
{
	int error, line;
	OSThread thread;
	unsigned char stack[0x5000]; //Used for variables created inside the thread
};

#define CHECK_ERROR(cond) if (cond) { bss->line = __LINE__; goto error; } //Helper function to check a condition and if it fails jump to the error marker in the code section
#define errno (*__gh_errno_ptr()) //Returns the current error number after a failed checkbyte operation (not used in this code)
#define MSG_DONTWAIT 32 //Used by checkbyte
#define EWOULDBLOCK 6 //this error code describes that the connection to the client is still active, but there was simply no data available (not used in this code)


static int recvwait(struct pygecko_bss_t *bss, int sock, void *buffer, int len) //Wait for data from the client
{
	int ret;
	while (len > 0) {
		ret = recv(sock, buffer, len, 0);
		CHECK_ERROR(ret < 0);
		len -= ret;
		buffer += ret;
	}
	return 0;
error:
	bss->error = ret;
	return ret;
}

static int recvbyte(struct pygecko_bss_t *bss, int sock) //Wait for 1 byte from the client
{
	unsigned char buffer[1];
	int ret;

	ret = recvwait(bss, sock, buffer, 1);
	if (ret < 0) return ret;
	return buffer[0];
}

static int checkbyte(struct pygecko_bss_t *bss, int sock) //Checks if there is data coming from the client. If not the function returns, no wait! Returns error 6 if connection is still alive
{
	unsigned char buffer[1];
	int ret;

	ret = recv(sock, buffer, 1, MSG_DONTWAIT);
	if (ret < 0) return ret;
	if (ret == 0) return ret;
	return buffer[0];
}

static int sendwait(struct pygecko_bss_t *bss, int sock, const void *buffer, int len) //Sends data to the client and waits until it is done sending
{
	int ret;
	while (len > 0) 
	{
		ret = send(sock, buffer, len, 0);
		CHECK_ERROR(ret < 0);

		len -= ret;
		buffer += ret;
	}
	return 0;
error:
	bss->error = ret;
	return ret;
}

static int sendbyte(struct pygecko_bss_t *bss, int sock, unsigned char byte) //Sends 1 byte to the connected client and wait until it is done sending
{
	unsigned char buffer[1];

	buffer[0] = byte;
	return sendwait(bss, sock, buffer, 1);
}

//Handles the connection to the WiiU Autosplitter PC App
static int run_autoSplitter(struct pygecko_bss_t *bss, int clientfd)
{
	int ret;

	unsigned char Cmd[1];
	Cmd[0] = 0x01; //Each data block send to the pc app begins with a byte set to 0 or 1 to allow the app to verify that the connection is still fine

	//Pointers to useful memory addresses
	//TWW HD Stuff (+6FF000)

	//TP HD Stuff : +631E00
	const unsigned char *CurrentStagePtr_TP = (const unsigned char *)0x1064CDE8;
	unsigned char CurrentStage_TP[7];

	const unsigned char *NextSpawnPtr_TP = (const unsigned char *)0x1064CDFF;
	unsigned char NextSpawn_TP[1];

	const unsigned char *IsLoadingPtr_TP = (const unsigned char *)0x10680E94;
	unsigned char IsLoading_TP[4];

	const unsigned char *IsHeroModeFlagPtr_TP = (const unsigned char *)0x10647D1F;
	unsigned char IsHeroModeFlag_TP[1];

	const unsigned char *TitleScreenPointerPtr_TP = (const unsigned char *)0x10680E7C;
	unsigned int TitleScreenPointer_TP;

	unsigned char TitleScreenFlag_TP[1];

	const unsigned char *PlaytimeTimerPtr_TP = (const unsigned char *)0x10647B70;
	unsigned char PlaytimeTimer_TP[4];

	const unsigned char *EventFlagPtr_TP = (const unsigned char *)0x1064CF95;
	unsigned char EventFlag_TP[1];

	u8 lastTitleScreenFlag_TP = 0;
	int newRunConditions_TP = 0;
	u8 loadingStatus_TP = 0;
	u8 firstStart_TP = 1;

	//DKC Stuff
	u8 loadingStatus_DKC = 0;

	//Global
	int gameID = -1;

	u8 newRunOut[1], endRunOut[1], doSplitOut[1];
	u8 isLoadingOut[1];
	u8 prevNewRun = 0, prevEndRun = 0, prevIsLoading = 0;

	u32 currentSplitIndex = 0;

	double OneMillisecond = 0.0f;
	int64_t timeBase = 0;
	u32 loadingFrames = 0;

	u64 currTitleID = OSGetTitleID();

	//Check which game is running
	if (currTitleID != 0 && (currTitleID == 0x0005000010143500 || currTitleID == 0x0005000010143600 || currTitleID == 0x0005000010143400)) //TWW HD is running
		gameID = 0;
	else if (currTitleID != 0 && (currTitleID == 0x000500001019E500 || currTitleID == 0x000500001019E600 || currTitleID == 0x000500001019C800)) //TP HD is running
		gameID = 1;
	else if (currTitleID != 0 && (currTitleID == 0x0005000010137F00 || currTitleID == 0x0005000010138300 || currTitleID == 0x0005000010144800)) //DKC Tropical Freeze is running
		gameID = 2;


	//Function Hook vars (mainly for DKC TF)
	g_newRun = 0;
	g_endRun = 0;
	g_doSplit = 0;
	g_isLoading = 0;
	
	//Get Clock Speed
	OSSystemInfo *systemInfo = OSGetSystemInfo();

	OneMillisecond = systemInfo->busSpeed / 4.0f;
	OneMillisecond = OneMillisecond / 1000.0f;

	//Cleanup system
	DestroyAutoSplitterSystem();

	//Send title id as handshake
	unsigned char bufferHandshake[8];
	memcpy(bufferHandshake, &currTitleID, 8);

	ret = sendwait(bss, clientfd, bufferHandshake, sizeof(bufferHandshake));
	CHECK_ERROR(ret < 0);

	//Receive jsonString length as response to handshake
	u32 strLength = 0;

	ret = recvwait(bss, clientfd, bufferHandshake, 4);
	CHECK_ERROR(ret < 0);

	memcpy(&strLength, bufferHandshake, 4);

	if (strLength == 0)
	{
		//Title id mismatched --> Abort
		bss->line = __LINE__;
		goto error;

		return 0;
	}

	//log_printf("Init1! length: %i", strLength);

	//Receive json string
	char* jsonStringBuffer = memalign(0x80, strLength);

	ret = recvwait(bss, clientfd, jsonStringBuffer, strLength);
	CHECK_ERROR(ret < 0);

	//Setup AutoSplitting system
	int success = SetupAutoSplitterSystem(jsonStringBuffer);

	free(jsonStringBuffer);

	if (success == -1)
	{
		log_printf("Init error");

		//Json data was faulty. Send error code and abort
		bufferHandshake[0] = 0xEE;
		ret = sendwait(bss, clientfd, bufferHandshake, 1);

		bss->line = __LINE__;
		goto error;

		return 0;
	}
	else
	{
		log_printf("Init done");

		//Send Status OK and enter loop
		bufferHandshake[0] = 0x00;
		ret = sendwait(bss, clientfd, bufferHandshake, 1);
		CHECK_ERROR(ret < 0);
	}


	//Main Loop
	while (1) 
	{
		//Reset at start of cycle
		newRunOut[0] = 0;
		endRunOut[0] = 0;
		doSplitOut[0] = 0;
		isLoadingOut[0] = 0xFF;
		timeBase = 0;

	    GX2WaitForVsync(); //Executes 60 times a second (60 fps)

		//Runs for every game
		u8 inNewRun = 0, inEndRun = 0, inDoSplit = 0, inLoadingStatus = 0;
		int splitStatus = RunAutoSplitterSystem(currentSplitIndex, &inNewRun, &inEndRun, &inDoSplit, &inLoadingStatus);

		if (splitStatus != 0) //critical error, terminate connection
		{
			bss->line = __LINE__;
			goto error;

			return 0;
		}

		//Generic run end
		if (inEndRun != 0xFF)
		{
			if (prevEndRun != inEndRun)
			{
				prevEndRun = inEndRun;

				if (inEndRun == 1)
				{
					endRunOut[0] = 1;

					prevNewRun = 0;
				}
			}
		}

		//Generic new run/reset
		if (inNewRun != 0xFF)
		{
			if (prevNewRun != inNewRun)
			{
				prevNewRun = inNewRun;

				if (inNewRun == 1)
				{
					newRunOut[0] = 1;

					endRunOut[0] = 0;
					doSplitOut[0] = 0;
					isLoadingOut[0] = 0;

					prevEndRun = 0;
					prevIsLoading = 0;
					currentSplitIndex = 0;
				}
			}
		}

		//Generic split
		if (inDoSplit == 1)
		{
			currentSplitIndex++;
			doSplitOut[0] = 1;
		}

		//Generic loading code
		if (inLoadingStatus != 0xFF)
		{
			if (inLoadingStatus == 1)
			{
				if (prevIsLoading == 0) //Loading Start
				{
					loadingFrames = 0;
					prevIsLoading = 1;

					isLoadingOut[0] = 1; //Something to send
				}
				else
				{
					loadingFrames++;
				}
			}
			else
			{
				if (prevIsLoading == 1) //Loading End
				{
					loadingFrames++;
					prevIsLoading = 0;

					isLoadingOut[0] = 0; //Something to send
				}
				else
				{
					loadingFrames = 0;
				}
			}
		}


		//Game specific overrides
		if (gameID == 1) //TP HD is running
		{
			//Current Map Info Stuff
			memcpy(CurrentStage_TP, CurrentStagePtr_TP, 7); //Copies 7 bytes from the pointer address to a variable ready for sending

			//Next Map Info Stuff
			memcpy(NextSpawn_TP, NextSpawnPtr_TP, 1);

			//Loading Stuff
			memcpy(IsLoading_TP, IsLoadingPtr_TP, 4);

			//Event Flag Stuff
			memcpy(EventFlag_TP, EventFlagPtr_TP, 1);

			//Get Title Screen Pointer
			memcpy(&TitleScreenPointer_TP, TitleScreenPointerPtr_TP, 4);

			if (TitleScreenPointer_TP < 0x10000000)
			{
				memset(TitleScreenFlag_TP, 0, 1);
			}
			else
			{
				unsigned char *TitleScreenFlagPtr_TP = (unsigned char *)TitleScreenPointer_TP + 0x45;
				memcpy(TitleScreenFlag_TP, TitleScreenFlagPtr_TP, 1); //11 = normal load; 12 = title screen; 13 = file menu
			}

			//Make sure on first connection that the user is not inside the file menu already
			if (firstStart_TP == 1)
			{
				firstStart_TP = 0;

				if (!strcmp((const char*)CurrentStage_TP, "F_SP102") && EventFlag_TP[0] == 0)
				{
					bufferHandshake[0] = 0x02; //Error code invalid start
					ret = sendwait(bss, clientfd, bufferHandshake, 1);

					bss->line = __LINE__;
					goto error;

					return 0;
				}
			}

			//New Run Condition Check		
			if (newRunConditions_TP == 0) //Wait for FileSelect Loading Flag (13) and IsLoading = 1
			{
				if (TitleScreenFlag_TP[0] == 13 && IsLoading_TP[0] == 0 && IsLoading_TP[1] == 0 && IsLoading_TP[2] == 0 && IsLoading_TP[3] == 1)
				{
					newRunConditions_TP = 1;
				}
			}
			else if (newRunConditions_TP == 1) //Wait for IsLoading = 0 then set IsHeroMode = 2
			{
				if (IsLoading_TP[0] == 0 && IsLoading_TP[1] == 0 && IsLoading_TP[2] == 0 && IsLoading_TP[3] == 0)
				{
					char *heroModeFlagNew;
					heroModeFlagNew = ((char *)IsHeroModeFlagPtr_TP);

					*heroModeFlagNew = 2;

					DCFlushRange(heroModeFlagNew, 1);

					newRunConditions_TP = 2;
				}
			}
			else if (newRunConditions_TP == 2) //Wait for IsHeroMode to be changed to 0 or 1 then check Playtime Timer = 0
			{
				memcpy(IsHeroModeFlag_TP, IsHeroModeFlagPtr_TP, 1);

				if (IsHeroModeFlag_TP[0] == 0 || IsHeroModeFlag_TP[0] == 1)
				{
					memcpy(PlaytimeTimer_TP, PlaytimeTimerPtr_TP, 4);

					if (PlaytimeTimer_TP[0] == 0 && PlaytimeTimer_TP[1] == 0 && PlaytimeTimer_TP[2] == 0 && PlaytimeTimer_TP[3] == 0) //New Game was started (or Amiibo was assigned)
					{
						//Final checks to make sure its a New File
						if ((TitleScreenFlag_TP[0] == 12) || (IsLoading_TP[0] == 0 && IsLoading_TP[1] == 0 && IsLoading_TP[2] == 0 && IsLoading_TP[3] == 1))
						{
							//User assigned an Amiibo to a file, reset conditions
							newRunConditions_TP = 0;
						}
						else //All is good, send isNewRun Command
						{
							newRunOut[0] = 1;
							newRunConditions_TP = 3;

							endRunOut[0] = 0;
							doSplitOut[0] = 0;
							isLoadingOut[0] = 0;

							prevEndRun = 0;
							currentSplitIndex = 0;
						}
					}
					else //The user just loaded an existing file
					{
						newRunConditions_TP = 3;
					}
				}
				else if (strcmp((const char*)CurrentStage_TP, "F_SP102")) //Something went wrong, abort
				{
					newRunConditions_TP = 0;
				}
			}
			else if (newRunConditions_TP == 3) //Wait until current Stage is not F_SP102 (Title Screen) then reset conditions
			{
				if (strcmp((const char*)CurrentStage_TP, "F_SP102"))
				{
					newRunConditions_TP = 0;
				}
			}

			//Don't set loading flag if void or title screen reset/file load (so mistakes are still punished the same way as with RTA timing)
			if (loadingStatus_TP == 0)
			{
				if (IsLoading_TP[0] == 0 && IsLoading_TP[1] == 0 && IsLoading_TP[2] == 0 && IsLoading_TP[3] == 1)
				{
					if (lastTitleScreenFlag_TP == 1) //Handles file load cases (normal and with Amiibo)
					{
						loadingStatus_TP = 2;
					}

					if (TitleScreenFlag_TP[0] == 12 || TitleScreenFlag_TP[0] == 13) //if title screen is loaded or is on title screen and load file menu
					{
						loadingStatus_TP = 2;
						lastTitleScreenFlag_TP = 1;
					}
					else
					{
						lastTitleScreenFlag_TP = 0;
					}

					if (NextSpawn_TP[0] == 0xFF) //if void/game over
					{
						loadingStatus_TP = 2;
					}
				}
			}

			//Adjust loading flag
			if (loadingStatus_TP == 2)
			{
				if (IsLoading_TP[0] == 0 && IsLoading_TP[1] == 0 && IsLoading_TP[2] == 0 && IsLoading_TP[3] == 0)
				{
					loadingStatus_TP = 0;
				}
				else
				{
					IsLoading_TP[3] = 0;
				}
			}

			//Count loading frames
			if (IsLoading_TP[0] == 0 && IsLoading_TP[1] == 0 && IsLoading_TP[2] == 0 && IsLoading_TP[3] == 1)
			{
				if (loadingStatus_TP == 0) //Loading Start
				{
					loadingFrames = 0;
					loadingStatus_TP = 1;

					isLoadingOut[0] = 1; //Something to send
				}
				else
				{
					loadingFrames++;
				}
			}
			else
			{
				if (loadingStatus_TP == 1) //Loading End
				{
					loadingFrames++;
					loadingStatus_TP = 0;

					isLoadingOut[0] = 0; //Something to send
				}
				else
				{
					loadingFrames = 0;
				}
			}
		}	
		else if (gameID == 2) //DKC Tropical Freeze is running
		{
			if (g_isLoading == 1)
			{
				if (loadingStatus_DKC == 0) //Loading Start
				{
					loadingFrames = 0;
					loadingStatus_DKC = 1;

					isLoadingOut[0] = 1; //Something to send
				}
				else
				{
					loadingFrames++;
				}
			}
			else
			{
				if (loadingStatus_DKC == 1) //Loading End
				{
					loadingFrames++;
					loadingStatus_DKC = 0;

					isLoadingOut[0] = 0; //Something to send
				}
				else
				{
					loadingFrames = 0;
				}
			}

			//New Run/Reset
			if (g_newRun == 1)
			{
				newRunOut[0] = 1;

				g_newRun = 0;
				g_endRun = 0;
				g_doSplit = 0;

				endRunOut[0] = 0;
				doSplitOut[0] = 0;

				currentSplitIndex = 0;
			}

			//Run End
			if (g_endRun == 1)
			{
				endRunOut[0] = 1;

				g_endRun = 0;
			}

			//Split
			if (g_doSplit == 1)
			{
				doSplitOut[0] = 1;

				g_doSplit = 0;
			}
		}

		//Check if there is anything to send; don't send needless data if not
		if (newRunOut[0] == 0 && endRunOut[0] == 0 && doSplitOut[0] == 0 && isLoadingOut[0] == 0xFF)
		{
			Cmd[0] = 0; //no data to send
			ret = sendwait(bss, clientfd, Cmd, 1);
			CHECK_ERROR(ret < 0);

			continue;
		}

		//Gets the time since epoch in Milliseconds when a command is triggered
		timeBase = OSGetTime();
		timeBase = timeBase / OneMillisecond;

		//Sending all data vars
		Cmd[0] = 1;

		ret = sendwait(bss, clientfd, Cmd, 1);
		CHECK_ERROR(ret < 0); //Check after every send if the data went through, if not disconnect immediately

		//New Run/End Run
		ret = sendwait(bss, clientfd, newRunOut, 1);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, endRunOut, 1);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, doSplitOut, 1);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, isLoadingOut, 1);
		CHECK_ERROR(ret < 0);

		//Current Timebase and Loading Frames
		ret = sendwait(bss, clientfd, &timeBase, 8);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, &loadingFrames, 4);
		CHECK_ERROR(ret < 0);
    }

	DestroyAutoSplitterSystem();
	return 0;
error:
	bss->error = ret;

	DestroyAutoSplitterSystem();
	return 0;
}

//Auto Splitter Thread
static int start_autoSplitter(int argc, void *argv)
{
	int sockfd = -1, clientfd = -1, ret = 0, len;
	struct sockaddr_in addr;
	struct pygecko_bss_t *bss = argv;

	while (1)
	{
		addr.sin_family = AF_INET;
		addr.sin_port = 7334; //Auto Splitter uses Port 7334
		addr.sin_addr.s_addr = 0;

		sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //Open a socket with the TCP protocol and get the file handle
		CHECK_ERROR(sockfd == -1);

		ret = bind(sockfd, (void *)&addr, 16); //Bind the socket to port 7334 so it only reacts to requests on that specific port
		CHECK_ERROR(ret < 0);

		ret = listen(sockfd, 20); //Check if the socket is clear
		CHECK_ERROR(ret < 0);

		while (1)
		{
			len = 16;
			clientfd = accept(sockfd, (void *)&addr, &len); //Thread waits here until a client connects (PC App)
			CHECK_ERROR(clientfd == -1);

			ret = run_autoSplitter(bss, clientfd); //This function returns once the client disconnects or an error occurs
			CHECK_ERROR(ret < 0);

			socketclose(clientfd); //Close the connection on the WiiU side and repeat
			clientfd = -1;
		}

		socketclose(sockfd); //Close socket itself
		sockfd = -1;


	error:
		if (clientfd != -1)
			socketclose(clientfd);

		if (sockfd != -1)
			socketclose(sockfd);

		bss->error = ret;
	}

	return 0;
}

//Handles the connection to the Input Viewer App (NintendoSpy)
static int run_inputViewer(struct pygecko_bss_t *bss, int clientfd)
{
	int ret = 0;

	unsigned char Cmd[1];
	Cmd[0] = 0x01;

	//Button Variables
	unsigned char Buttons1[1];
	unsigned char Buttons2[1];
	unsigned char Buttons3[1];

	while (1)
	{
		GX2WaitForVsync();

		Buttons1[0] = 0;
		Buttons2[0] = 0;
		Buttons3[0] = 0;

		//Send Cmd Bit
		ret = sendwait(bss, clientfd, Cmd, 1);
		CHECK_ERROR(ret < 0);

		if (g_proControllerChannel != -1) //TODO: Add proper KPAD Support
		{
			if (g_currentInputDataKPAD.device_type > 1)
			{
				//Send WPAD/KPAD Data (Pro Controller)

				//Sticks
				ret = sendwait(bss, clientfd, &g_currentInputDataKPAD.classic.lstick_x, 4);
				CHECK_ERROR(ret < 0);

				ret = sendwait(bss, clientfd, &g_currentInputDataKPAD.classic.lstick_y, 4);
				CHECK_ERROR(ret < 0);

				ret = sendwait(bss, clientfd, &g_currentInputDataKPAD.classic.rstick_x, 4);
				CHECK_ERROR(ret < 0);

				ret = sendwait(bss, clientfd, &g_currentInputDataKPAD.classic.rstick_y, 4);
				CHECK_ERROR(ret < 0);

				//Button 1
				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_A) == WPAD_CLASSIC_BUTTON_A) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_A) == WPAD_CLASSIC_BUTTON_A)
					Buttons1[0] += 128;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_B) == WPAD_CLASSIC_BUTTON_B) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_B) == WPAD_CLASSIC_BUTTON_B)
					Buttons1[0] += 64;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_X) == WPAD_CLASSIC_BUTTON_X) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_X) == WPAD_CLASSIC_BUTTON_X)
					Buttons1[0] += 32;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_Y) == WPAD_CLASSIC_BUTTON_Y) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_Y) == WPAD_CLASSIC_BUTTON_Y)
					Buttons1[0] += 16;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_LEFT) == WPAD_CLASSIC_BUTTON_LEFT) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_LEFT) == WPAD_CLASSIC_BUTTON_LEFT)
					Buttons1[0] += 8;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_RIGHT) == WPAD_CLASSIC_BUTTON_RIGHT) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_RIGHT) == WPAD_CLASSIC_BUTTON_RIGHT)
					Buttons1[0] += 4;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_UP) == WPAD_CLASSIC_BUTTON_UP) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_UP) == WPAD_CLASSIC_BUTTON_UP)
					Buttons1[0] += 2;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_DOWN) == WPAD_CLASSIC_BUTTON_DOWN) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_DOWN) == WPAD_CLASSIC_BUTTON_DOWN)
					Buttons1[0] += 1;


				//Button 2
				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_ZL) == WPAD_CLASSIC_BUTTON_ZL) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_ZL) == WPAD_CLASSIC_BUTTON_ZL)
					Buttons2[0] += 128;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_ZR) == WPAD_CLASSIC_BUTTON_ZR) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_ZR) == WPAD_CLASSIC_BUTTON_ZR)
					Buttons2[0] += 64;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_L) == WPAD_CLASSIC_BUTTON_L) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_L) == WPAD_CLASSIC_BUTTON_L)
					Buttons2[0] += 32;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_R) == WPAD_CLASSIC_BUTTON_R) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_R) == WPAD_CLASSIC_BUTTON_R)
					Buttons2[0] += 16;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_PLUS) == WPAD_CLASSIC_BUTTON_PLUS) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_PLUS) == WPAD_CLASSIC_BUTTON_PLUS)
					Buttons2[0] += 8;

				if (((g_currentInputDataKPAD.classic.btns_d & WPAD_CLASSIC_BUTTON_MINUS) == WPAD_CLASSIC_BUTTON_MINUS) || (g_currentInputDataKPAD.classic.btns_h & WPAD_CLASSIC_BUTTON_MINUS) == WPAD_CLASSIC_BUTTON_MINUS)
					Buttons2[0] += 4;

				//if (((g_currentInputDataKPAD.classic.btns_d & VPAD_BUTTON_STICK_L) == VPAD_BUTTON_STICK_L) || (g_currentInputDataKPAD.classic.btns_h & VPAD_BUTTON_STICK_L) == VPAD_BUTTON_STICK_L)
					//Buttons2[0] += 2;

				//if (((g_currentInputDataKPAD.classic.btns_d & VPAD_BUTTON_STICK_R) == VPAD_BUTTON_STICK_R) || (g_currentInputDataKPAD.classic.btns_h & VPAD_BUTTON_STICK_R) == VPAD_BUTTON_STICK_R)
					//Buttons2[0] += 1;


				//Button 3
				if (((g_currentInputData.btns_d & WPAD_CLASSIC_BUTTON_HOME) == WPAD_CLASSIC_BUTTON_HOME) || (g_currentInputData.btns_h & WPAD_CLASSIC_BUTTON_HOME) == WPAD_CLASSIC_BUTTON_HOME)
					Buttons3[0] += 2;

				ret = sendwait(bss, clientfd, Buttons1, 1);
				CHECK_ERROR(ret < 0);

				ret = sendwait(bss, clientfd, Buttons2, 1);
				CHECK_ERROR(ret < 0);

				ret = sendwait(bss, clientfd, Buttons3, 1);
				CHECK_ERROR(ret < 0);

				continue;
			}
		}

		//Send VPAD Data (Gamepad)

		//Sticks
		ret = sendwait(bss, clientfd, &g_currentInputData.lstick.x, 4);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, &g_currentInputData.lstick.y, 4);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, &g_currentInputData.rstick.x, 4);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, &g_currentInputData.rstick.y, 4);
		CHECK_ERROR(ret < 0);	

		//Button 1
		if (((g_currentInputData.btns_d & VPAD_BUTTON_A) == VPAD_BUTTON_A) || (g_currentInputData.btns_h & VPAD_BUTTON_A) == VPAD_BUTTON_A)
			Buttons1[0] += 128;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_B) == VPAD_BUTTON_B) || (g_currentInputData.btns_h & VPAD_BUTTON_B) == VPAD_BUTTON_B)
			Buttons1[0] += 64;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_X) == VPAD_BUTTON_X) || (g_currentInputData.btns_h & VPAD_BUTTON_X) == VPAD_BUTTON_X)
			Buttons1[0] += 32;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_Y) == VPAD_BUTTON_Y) || (g_currentInputData.btns_h & VPAD_BUTTON_Y) == VPAD_BUTTON_Y)
			Buttons1[0] += 16;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_LEFT) == VPAD_BUTTON_LEFT) || (g_currentInputData.btns_h & VPAD_BUTTON_LEFT) == VPAD_BUTTON_LEFT)
			Buttons1[0] += 8;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_RIGHT) == VPAD_BUTTON_RIGHT) || (g_currentInputData.btns_h & VPAD_BUTTON_RIGHT) == VPAD_BUTTON_RIGHT)
			Buttons1[0] += 4;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_UP) == VPAD_BUTTON_UP) || (g_currentInputData.btns_h & VPAD_BUTTON_UP) == VPAD_BUTTON_UP)
			Buttons1[0] += 2;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_DOWN) == VPAD_BUTTON_DOWN) || (g_currentInputData.btns_h & VPAD_BUTTON_DOWN) == VPAD_BUTTON_DOWN)
			Buttons1[0] += 1;


		//Button 2
		if (((g_currentInputData.btns_d & VPAD_BUTTON_ZL) == VPAD_BUTTON_ZL) || (g_currentInputData.btns_h & VPAD_BUTTON_ZL) == VPAD_BUTTON_ZL)
			Buttons2[0] += 128;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_ZR) == VPAD_BUTTON_ZR) || (g_currentInputData.btns_h & VPAD_BUTTON_ZR) == VPAD_BUTTON_ZR)
			Buttons2[0] += 64;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_L) == VPAD_BUTTON_L) || (g_currentInputData.btns_h & VPAD_BUTTON_L) == VPAD_BUTTON_L)
			Buttons2[0] += 32;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_R) == VPAD_BUTTON_R) || (g_currentInputData.btns_h & VPAD_BUTTON_R) == VPAD_BUTTON_R)
			Buttons2[0] += 16;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_PLUS) == VPAD_BUTTON_PLUS) || (g_currentInputData.btns_h & VPAD_BUTTON_PLUS) == VPAD_BUTTON_PLUS)
			Buttons2[0] += 8;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_MINUS) == VPAD_BUTTON_MINUS) || (g_currentInputData.btns_h & VPAD_BUTTON_MINUS) == VPAD_BUTTON_MINUS)
			Buttons2[0] += 4;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_STICK_L) == VPAD_BUTTON_STICK_L) || (g_currentInputData.btns_h & VPAD_BUTTON_STICK_L) == VPAD_BUTTON_STICK_L)
			Buttons2[0] += 2;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_STICK_R) == VPAD_BUTTON_STICK_R) || (g_currentInputData.btns_h & VPAD_BUTTON_STICK_R) == VPAD_BUTTON_STICK_R)
			Buttons2[0] += 1;


		//Button 3
		if (((g_currentInputData.btns_d & VPAD_BUTTON_TV) == VPAD_BUTTON_TV) || (g_currentInputData.btns_h & VPAD_BUTTON_TV) == VPAD_BUTTON_TV)
			Buttons3[0] += 1;

		if (((g_currentInputData.btns_d & VPAD_BUTTON_HOME) == VPAD_BUTTON_HOME) || (g_currentInputData.btns_h & VPAD_BUTTON_HOME) == VPAD_BUTTON_HOME)
			Buttons3[0] += 2;

		if (g_currentInputData.tpdata.touched == 1 || g_currentInputData.tpdata1.touched == 1 || g_currentInputData.tpdata2.touched == 1)
			Buttons3[0] += 4;

		ret = sendwait(bss, clientfd, Buttons1, 1);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, Buttons2, 1);
		CHECK_ERROR(ret < 0);

		ret = sendwait(bss, clientfd, Buttons3, 1);
		CHECK_ERROR(ret < 0);

		continue;

	error:
		bss->error = ret;
		return ret;
	}

	return ret;
}


//Input Viewer Thread
static int start_inputViewer(int argc, void *argv) 
{
	int sockfd = -1, clientfd = -1, ret = 0, len;
	struct sockaddr_in addr;
	struct pygecko_bss_t *bss = argv;

	while (1) 
	{
		addr.sin_family = AF_INET;
		addr.sin_port = 7335; //Input Viewer uses Port 7335
		addr.sin_addr.s_addr = 0;

		sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  //Open socket
		CHECK_ERROR(sockfd == -1);

		ret = bind(sockfd, (void *)&addr, 16);
		CHECK_ERROR(ret < 0);

		ret = listen(sockfd, 20);
		CHECK_ERROR(ret < 0);

		while (1) 
		{
			len = 16;
			clientfd = accept(sockfd, (void *)&addr, &len);
			CHECK_ERROR(clientfd == -1);

			ret = run_inputViewer(bss, clientfd); //This function returns once the client disconnects or an error occurs
			CHECK_ERROR(ret < 0);

			socketclose(clientfd);
			clientfd = -1;
		}

		socketclose(sockfd);
		sockfd = -1;
	error:
		if (clientfd != -1)
			socketclose(clientfd);
		if (sockfd != -1)
			socketclose(sockfd);
		bss->error = ret;

	}
	return 0;
}

//Helper thread that waits until the game boots, then kickstarts the actual threads used for networking
static int CCThread(int argc, void *argv) 
{
	//Need to wait a bit so the game can fully boot. During sleep other threads can be scheduled which is critical during the boot process. 
	//Otherwise using sockets can be unstable and crash the console when trying to open a connection while the console is still under heavy load

	usleep(7000000); //Thread enters a resting state for 7 seconds (7000k microseconds) to allow other threads to be scheduled


	//Get handle to needed functions
	InitOSFunctionPointers(); //Cafe OS functions e.g. OSGetTitleID
	InitSocketFunctionPointers(); //Sockets
	InitGX2FunctionPointers(); //Graphics e.g. GX2WaitForVsync
	InitSysFunctionPointers(); //for SYSLaunchMenu
	InitFSFunctionPointers(); //for Saviine
	SetupOSExceptions(); //re-direct Exceptions to our function

	//Waits for Vsync = frame advance (system always updates at 60 fps) -> Ensures the game has booted
	GX2WaitForVsync();

	//Init twice is needed so logging works properly (idk why)
	log_init(HOST_IP);
	log_deinit();
	log_init(HOST_IP);

	PatchGameHooks(); //Patch game functions for hacks

	log_printf("Game has launched, starting Threads...\n");

	//Crash test, dont uncomment :P
	//OSBlockMove(0, "test", 4, 1);

	//Auto Splitter Thread
	struct pygecko_bss_t *bss; //See definition at the very top, this struct supports error handling for the threads
	
	bss = memalign(0x40, sizeof(struct pygecko_bss_t));
	if (bss == 0)
		return 0;

	memset(bss, 0, sizeof(struct pygecko_bss_t));
	
	if(OSCreateThread(&bss->thread, start_autoSplitter, 1, bss, (u32)bss->stack + sizeof(bss->stack), sizeof(bss->stack), 2, OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_DETACHED) == 1)
	{
		OSSetThreadName(&bss->thread, "DB_AutoSplitter");
		OSResumeThread(&bss->thread);
	}
	else
	{
		free(bss);
	}

	//Input Viewer Thread
	struct pygecko_bss_t *bss2;

	bss2 = memalign(0x40, sizeof(struct pygecko_bss_t));
	if (bss2 == 0)
		return 0;

	memset(bss2, 0, sizeof(struct pygecko_bss_t));

	if (OSCreateThread(&bss2->thread, start_inputViewer, 1, bss2, (u32)bss2->stack + sizeof(bss2->stack), sizeof(bss2->stack), 2, OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_DETACHED) == 1)
	{
		OSSetThreadName(&bss2->thread, "DB_InputViewer");
		//OSSetThreadRunQuantum(&bss2->thread, 2000); //Limits thread runtime to avoid possible deadlocks

		OSResumeThread(&bss2->thread);
	}
	else
	{
		free(bss2);
	}
	return 0;
}

//Gets called when a "game" boots. Creates a helper thread so execution can return to the booting game as quickly as possible
void start_tools(void)
{
	unsigned int stack = (unsigned int)memalign(0x40, 0x1000); //Allocates 4096 bytes as own RAM for the new thread and aligns them in 64 byte blocks, returns pointer to the memory

	OSThread *thread = (OSThread *)stack;
	
	//OSCreateThread Reference: http://bombch.us/CSXL ; Attribute Reference: http://bombch.us/CSXP	
	//Note: Core/CPU0 is reserved for the system and not used by games, CPU1 and 2 are for game tasks

	if(OSCreateThread(thread, CCThread, 1, NULL, stack + sizeof(stack), sizeof(stack), 2, OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_DETACHED) == 1) //Run on System Core
	{
		OSResumeThread(thread); //Thread sleeps by default, so make sure we resume it
	}
	else
	{
		free(thread); //Clear thread memory if something goes wrong
	}
}