/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*/

#ifndef _TCP_LISTEN_SOCKET_H_
#define _TCP_LISTEN_SOCKET_H_

#ifdef CONFIG_USE_EPOLL

#include <errno.h>
#include "socket_defines.h"

class TCPListenSocket : public SocketBase
{
public:
	TCPListenSocket(const char* listen_address, 
				 uint16 port, 
				 const HandleInfo onconnected_handler,
				 const HandleInfo onclose_handler,
				 const HandleInfo onrecv_handler,
				 uint32 sendbuffersize, 
				 uint32 recvbuffersize,
				 bool is_parse_package = true);

	virtual ~TCPListenSocket();

	bool Start();
	void Close();

	int GetFd();

public:
	SOCKET socket_;
	struct sockaddr_in address_;
	uint16 port_;

	uint32 sendbuffersize_;
	uint32 recvbuffersize_;

	HandleInfo onconnected_handler_;
	HandleInfo onclose_handler_;
	HandleInfo onrecv_handler_;

	bool is_parse_package_;
};

#endif
#endif //_TCP_LISTEN_SOCKET_H_
