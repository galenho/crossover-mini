/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#ifndef _UDP_LISTEN_SOCKET_WIN32_H_
#define _UDP_LISTEN_SOCKET_WIN32_H_

#ifdef CONFIG_USE_IOCP

class UDPListenSocket : public SocketBase
{
public:
	UDPListenSocket(const char* listen_address, 
				 uint16 port, 
				 const HandleInfo onconnected_handler,
				 const HandleInfo onclose_handler,
				 const HandleInfo onrecv_handler,
				 uint32 sendbuffersize, 
				 uint32 recvbuffersize);

	virtual ~UDPListenSocket();

	bool Start();
	void Close();

	// 投递一个连接操作(仿TCP)
	bool PostAccept();

public:
	SOCKET socket_;
	struct sockaddr_in address_;
	
	uint32 sendbuffersize_;
	uint32 recvbuffersize_;

	HandleInfo onconnected_handler_;
	HandleInfo onclose_handler_;
	HandleInfo onrecv_handler_;

	OverlappedStruct read_event_;

	sockaddr_in	client_addr_;
	char buff_[128];
	int32 buff_len_;
};

#endif
#endif //_UDP_LISTEN_SOCKET_WIN32_H_
