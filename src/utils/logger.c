#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "common/common.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "logger.h"

static int log_socket = 0;
static volatile int log_lock = 0;


/* Communication bytes with the server */
// Com
#define BYTE_NORMAL                     0xff
#define BYTE_SPECIAL                    0xfe
#define BYTE_OK                         0xfd
#define BYTE_PING                       0xfc
#define BYTE_LOG_STR                    0xfb
#define BYTE_DISCONNECT                 0xfa

// SD
#define BYTE_MOUNT_SD                   0xe0
#define BYTE_MOUNT_SD_OK                0xe1
#define BYTE_MOUNT_SD_BAD               0xe2

// Replacement
#define BYTE_STAT                       0x00
#define BYTE_STAT_ASYNC                 0x01
#define BYTE_OPEN_FILE                  0x02
#define BYTE_OPEN_FILE_ASYNC            0x03
#define BYTE_OPEN_DIR                   0x04
#define BYTE_OPEN_DIR_ASYNC             0x05
#define BYTE_CHANGE_DIR                 0x06
#define BYTE_CHANGE_DIR_ASYNC           0x07
#define BYTE_MAKE_DIR                   0x08
#define BYTE_MAKE_DIR_ASYNC             0x09
#define BYTE_RENAME                     0x0A
#define BYTE_RENAME_ASYNC               0x0B
#define BYTE_REMOVE                     0x0C
#define BYTE_REMOVE_ASYNC               0x0D

// Log
#define BYTE_CLOSE_FILE                 0x40
#define BYTE_CLOSE_FILE_ASYNC           0x41
#define BYTE_CLOSE_DIR                  0x42
#define BYTE_CLOSE_DIR_ASYNC            0x43
#define BYTE_FLUSH_FILE                 0x44
#define BYTE_GET_ERROR_CODE_FOR_VIEWER  0x45
#define BYTE_GET_LAST_ERROR             0x46
#define BYTE_GET_MOUNT_SOURCE           0x47
#define BYTE_GET_MOUNT_SOURCE_NEXT      0x48
#define BYTE_GET_POS_FILE               0x49
#define BYTE_SET_POS_FILE               0x4A
#define BYTE_GET_STAT_FILE              0x4B
#define BYTE_EOF                        0x4C
#define BYTE_READ_FILE                  0x4D
#define BYTE_READ_FILE_ASYNC            0x4E
#define BYTE_READ_FILE_WITH_POS         0x4F
#define BYTE_READ_DIR                   0x50
#define BYTE_READ_DIR_ASYNC             0x51
#define BYTE_GET_CWD                    0x52
#define BYTE_SET_STATE_CHG_NOTIF        0x53
#define BYTE_TRUNCATE_FILE              0x54
#define BYTE_WRITE_FILE                 0x55
#define BYTE_WRITE_FILE_WITH_POS        0x56

#define BYTE_SAVE_INIT                  0x57
#define BYTE_SAVE_SHUTDOWN              0x58
#define BYTE_SAVE_INIT_SAVE_DIR         0x59
#define BYTE_SAVE_FLUSH_QUOTA           0x5A
#define BYTE_SAVE_OPEN_DIR              0x5B
#define BYTE_SAVE_REMOVE                0x5C

#define BYTE_CREATE_THREAD              0x60

#define LOADIINE_LOGGER_IP              "192.168.2.104"


void log_init(const char * ipString)
{
	log_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (log_socket < 0)
		return;

	struct sockaddr_in connect_addr;
	memset(&connect_addr, 0, sizeof(connect_addr));
	connect_addr.sin_family = AF_INET;
	connect_addr.sin_port = 4405;
	inet_aton(ipString, &connect_addr.sin_addr);

	if(connect(log_socket, (struct sockaddr*)&connect_addr, sizeof(connect_addr)) < 0)
	{
	    socketclose(log_socket);
	    log_socket = -1;
	}
}

void log_deinit(void)
{
    if(log_socket > 0)
    {
        socketclose(log_socket);
        log_socket = -1;
    }
}

void log_print(const char *str)
{
    // socket is always 0 initially as it is in the BSS
    if(log_socket <= 0) {
        return;
    }

    while(log_lock)
        usleep(1000);
    log_lock = 1;

    int len = strlen(str);
    int ret;
    while (len > 0) {
        int block = len < 1400 ? len : 1400; // take max 1400 bytes per UDP packet
        ret = send(log_socket, str, block, 0);
        if(ret < 0)
            break;

        len -= ret;
        str += ret;
    }

    log_lock = 0;
}

void log_printf(const char *format, ...)
{
    if(log_socket <= 0) {
        return;
    }

	char * tmp = NULL;

	va_list va;
	va_start(va, format);
	if((vasprintf(&tmp, format, va) >= 0) && tmp)
	{
        log_print(tmp);
	}
	va_end(va);

	if(tmp)
		free(tmp);
}

void threadLog_init(const char * ipString, int * log_socket2)
{
	*log_socket2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (*log_socket2 < 0)
		return;

	struct sockaddr_in connect_addr;
	memset(&connect_addr, 0, sizeof(connect_addr));
	connect_addr.sin_family = AF_INET;
	connect_addr.sin_port = 4405;
	inet_aton(ipString, &connect_addr.sin_addr);

	if (connect(*log_socket2, (struct sockaddr*)&connect_addr, sizeof(connect_addr)) < 0)
	{
		socketclose(*log_socket2);
		*log_socket2 = -1;
	}
}

void threadLog_deinit(int * log_socket2)
{
	if (*log_socket2 > 0)
	{
		socketclose(*log_socket2);
		*log_socket2 = -1;
	}
}

void threadLog_print(const char *str, const int * log_socket2)
{
	// socket is always 0 initially as it is in the BSS
	if (*log_socket2 <= 0) {
		return;
	}

	int len = strlen(str);
	int ret;
	while (len > 0) {
		int block = len < 1400 ? len : 1400; // take max 1400 bytes per UDP packet
		ret = send(*log_socket2, str, block, 0);
		if (ret < 0)
			break;

		len -= ret;
		str += ret;
	}
}

void threadLog_printf(const int * log_socket2, const char *format, ...)
{
	if (*log_socket2 <= 0) {
		return;
	}

	char * tmp = NULL;

	va_list va;
	va_start(va, format);
	if ((vasprintf(&tmp, format, va) >= 0) && tmp)
	{
		threadLog_print(tmp, log_socket2);
	}
	va_end(va);

	if (tmp)
		free(tmp);
}
