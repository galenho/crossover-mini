/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*
*
* 核心类: 
*/
#ifndef _SOCKETMGR_WIN32_H_
#define _SOCKETMGR_WIN32_H_

#ifdef CONFIG_USE_IOCP
#include "singleton.h"
#include "rwlock.h"

class Socket;
//----------------------------------------------------------------------------
typedef void(*OperationHandler)(Socket* s, uint32 len, bool is_success);

void HandleAcceptComplete(Socket* s, uint32 len, bool is_success);
void HandleConnectComplete(Socket* s, uint32 len, bool is_success);
void HandleReadComplete(Socket* s, uint32 len, bool is_success);
void HandleWriteComplete(Socket* s, uint32 len, bool is_success);
void HandleClose(Socket* s, uint32 len, bool is_success);
void HandleDelaySend(Socket* s, uint32 len, bool is_success);

void HandleShutdown(Socket* s, uint32 len, bool is_success);
void HandleWakeUp(Socket* s, uint32 len, bool is_success);

static OperationHandler ophandlers[MAX_SOCKET_IO_EVENTS] =
{
	&HandleAcceptComplete, &HandleConnectComplete, &HandleReadComplete, &HandleWriteComplete, &HandleClose, &HandleDelaySend, &HandleShutdown, &HandleWakeUp
};

//----------------------------------------------------------------------------
class SocketMgr : public Singleton<SocketMgr>
{
public:
	SocketMgr();
	~SocketMgr();

	bool Init();
	bool Close();

	void Update(uint32 cur_time);
	void EventLoop(int32 timeout);
	void WakeUp();

	HANDLE GetCompletionPort();

	uint32 Connect( const string& ip, uint16 port,
		const HandleInfo onconnected_handler,
		const HandleInfo onclose_handler,
		const HandleInfo onrecv_handler,
		uint32 sendbuffersize, 
		uint32 recvbuffersize);

	uint32 ConnectEx( const string& ip, uint16 port,
		const HandleInfo onconnected_handler,
		const HandleInfo onclose_handler,
		const HandleInfo onrecv_handler,
		uint32 sendbuffersize, 
		uint32 recvbuffersize,
		bool is_parse_package);

	bool Send(uint32 conn_idx, const void* content, uint32 len);
	bool SendMsg(uint32 conn_idx, const void* content, uint32 len);
	void Disconnect(uint32 conn_idx);

	void Accept( SOCKET aSocket, 
		sockaddr_in& address, 
		const HandleInfo onconnected_handler,
		const HandleInfo onclose_handler,
		const HandleInfo onrecv_handler,
		uint32 sendbuffersize, 
		uint32 recvbuffersize,
		bool is_parse_package);

	bool AcceptUDP(sockaddr_in& remote_address,
		const HandleInfo onconnected_handler,
		const HandleInfo onclose_handler,
		const HandleInfo onrecv_handler,
		uint32 sendbuffersize,
		uint32 recvbuffersize,
		uint16& local_port);

	uint32 ConnectUDP(const string& ip, uint16 port, uint16& local_port,
		const HandleInfo onconnected_handler,
		const HandleInfo onclose_handler,
		const HandleInfo onrecv_handler,
		uint32 sendbuffersize,
		uint32 recvbuffersize,
		bool is_server_build = false);

	void CloseSocket(Socket* s);
	void ShutdownThreads();

	void RemoveSocket(uint32 conn_idx);

	uint32 MakeGeneralConnID();

	void AddSocket(Socket* s);

	char* GetIpAddress(uint32 conn_idx);
	
public:
	hash_map<uint32, Socket*> socket_map_;
	HANDLE completion_port_;

private:
	uint32 auto_conn_idx_;  //用conn_idx来做为socket_map_的Key, 因为端口事件检测使用的是指针ptr形式的
};

#endif
#endif //_SOCKETMGR_WIN32_H_