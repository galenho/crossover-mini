/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*/

#ifndef _SOCKET_DEFINES_H_
#define _SOCKET_DEFINES_H_

#include "common.h"
using namespace std;

/* Implementation Selection */
#ifdef WIN32
#define CONFIG_USE_IOCP
#else

// unix defines
#define SOCKET int
#define SD_BOTH SHUT_RDWR

// epoll
#include <sys/epoll.h>
#define CONFIG_USE_EPOLL

#endif

enum SocketIOEvent
{
	SOCKET_IO_EVENT_ACCEPT_TCP			= 1,
	SOCKET_IO_EVENT_ACCEPT_UDP			= 2,

	SOCKET_IO_EVENT_CONNECT_COMPLETE	= 3,
	SOCKET_IO_EVENT_READ_COMPLETE		= 4,
	SOCKET_IO_EVENT_WRITE_COMPLETE		= 5,
	
	SOCKET_IO_EVENT_DELAY_SEND			= 6,
	SOCKET_IO_EVENT_CLOSE				= 7,
	
	SOCKET_IO_THREAD_SHUTDOWN			= 8,
	SOCKET_IO_THREAD_WAKEUP				= 9,
};

enum SocketType
{
	SOCKET_TYPE_TCP = 1,
	SOCKET_TYPE_UDP = 2
};

/* IOCP Defines */
#ifdef CONFIG_USE_IOCP

class OverlappedStruct
{
public:
	OVERLAPPED overlap_;
	SocketIOEvent event_;
	SOCKET fd_;

	OverlappedStruct(SocketIOEvent ev) : event_(ev)
	{
		fd_ = 0;
		memset(&overlap_, 0, sizeof(OVERLAPPED));
	};

	OverlappedStruct()
	{
		memset(&overlap_, 0, sizeof(OVERLAPPED));
	}

	__forceinline void SetEvent(SocketIOEvent ev)
	{
		memset(&overlap_, 0, sizeof(OVERLAPPED));
		event_ = ev;
	}
};

#endif

#endif
