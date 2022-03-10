/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*/

#ifndef _TCP_LISTEN_SOCKET_WIN32_H_
#define _TCP_LISTEN_SOCKET_WIN32_H_

#ifdef CONFIG_USE_IOCP

class TCPListenSocket
{
public:
	TCPListenSocket(const char* listen_address,
				 uint16 port, 
				 const HandleInfo onconnected_handler,
				 const HandleInfo onclose_handler,
				 const HandleInfo onrecv_handler,
				 uint32 sendbuffersize, 
				 uint32 recvbuffersize,
				 bool is_parse_package);

	~TCPListenSocket();

	bool Start();
	void Close();

	// 投递一个accept请求
	bool PostAccept();

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
	OverlappedStruct accept_event_;

	char buff_[128];
	DWORD buff_len_;

	LPFN_ACCEPTEX accept_ex_;
	LPFN_GETACCEPTEXSOCKADDRS accept_addrs_;
};

#endif

#endif //_TCP_LISTEN_SOCKET_WIN32_H_
