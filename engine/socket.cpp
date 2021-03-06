/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#include "network.h"
#include "guard.h"
#include "scheduler.h"
#include "lua_fix.h"

#ifdef CONFIG_USE_IOCP
LPFN_CONNECTEX get_connect_ex(SOCKET fd)
{
	GUID guid = WSAID_CONNECTEX;

	LPFN_CONNECTEX ptr = NULL;
	DWORD bytes = 0;
	if (::WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid), &ptr, sizeof(ptr), &bytes, 0, 0) != 0)
	{
		ASSERT(false);
	}

	return ptr;
}

LPFN_ACCEPTEX get_accept_ex(SOCKET fd)
{
	GUID guid = WSAID_ACCEPTEX;

	LPFN_ACCEPTEX ptr = NULL;
	DWORD bytes = 0;
	if (::WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid), &ptr, sizeof(ptr), &bytes, 0, 0) != 0)
	{
		ASSERT(false);
	}

	return ptr;
}

LPFN_GETACCEPTEXSOCKADDRS get_accept_addr(SOCKET fd)
{
	GUID guid = WSAID_GETACCEPTEXSOCKADDRS;

	LPFN_GETACCEPTEXSOCKADDRS ptr = NULL;
	DWORD bytes = 0;
	if (0 != WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid), &ptr, sizeof(ptr), &bytes, NULL, NULL) != 0)
	{
		ASSERT(false);
	}

	return ptr;
}

#endif

#ifdef CONFIG_USE_EPOLL
Socket::Socket( SOCKET wakeup_fd )
{
	is_client_ = false;
	fd_ = wakeup_fd;
	memset(&m_client, 0, sizeof(m_client));
}
#endif

Socket::Socket(SocketType socket_type, 
			   SOCKET fd,
			   uint32 conn_idx,
			   const HandleInfo onconnected_handler,
			   const HandleInfo onclose_handler,
			   const HandleInfo onrecv_handler,
			   uint32 sendbuffersize, 
			   uint32 recvbuffersize,
			   bool is_parse_package) 
{
	socket_type_ = socket_type;
	is_listen_ = false;

	memset(&m_client, 0, sizeof(m_client));

	if (fd == 0) //说明是TcpClient连接
	{
		is_client_ = true;
	}
	else //说明是TcpServer连接
	{
		is_client_ = false;
	}

	is_parse_package_ = is_parse_package;

	fd_ = fd;
	conn_idx_ = conn_idx;

	onconnected_handler_ = onconnected_handler;
	onclose_handler_ = onclose_handler;
	onrecv_handler_ = onrecv_handler;

	// Allocate Buffers
	readBuffer.Allocate(recvbuffersize);
	recvbuffersize_ = recvbuffersize;

	writeBuffer.Allocate(sendbuffersize);

	write_lock_ = 0;

	p_kcp_ = NULL;

	// Check for needed fd allocation.
	if (fd_ == 0)
	{
		if (socket_type == SOCKET_TYPE_TCP)
		{
			fd_ = SocketOps::CreateTCPFileDescriptor();
		}
		else
		{
			fd_ = SocketOps::CreateUDPFileDescriptor();

			p_kcp_ = ikcp_create(conn_idx, (void*)this);
			p_kcp_->output = &Socket::udp_output;
			p_kcp_->input = &Socket::udp_input;

			// 启动快速模式
			// 第二个参数 nodelay-启用以后若干常规加速将启动
			// 第三个参数 interval为内部处理时钟，默认设置为 10ms
			// 第四个参数 resend为快速重传指标，设置为2
			// 第五个参数 为是否禁用常规流控，这里禁止
			//ikcp_nodelay(p_kcp_, 1, 10, 2, 1);
			ikcp_nodelay(p_kcp_, 1, 10, 2, 1); // 设置成1次ACK跨越直接重传, 这样反应速度会更快. 内部时钟5毫秒.
		}
	}

	status_ = socket_status_closed;

	is_udp_connected_ = false;

	//PRINTF_INFO("new fd = %d, conn_idx = %d", fd_, conn_idx_);
}

Socket::~Socket()
{
	//PRINTF_INFO("delete fd = %d, conn_idx = %d", fd_, conn_idx_);
	if (is_client_)
	{
		SocketClientDeleteTask::process(onconnected_handler_, onrecv_handler_, onclose_handler_);
	}
}

void Socket::Update( uint32 cur_time )
{
	if (socket_type_ == SOCKET_TYPE_UDP)
	{
		if (is_udp_connected_ && p_kcp_)
		{
			ikcp_update(p_kcp_, cur_time);
		}
	}
}

string Socket::GetRemoteIP()
{
	char* ip = (char*)inet_ntoa(m_client.sin_addr);
	if (ip != NULL)
		return string(ip);
	else
		return string("noip");
}

bool Socket::Connect(const char* address, uint16 port)
{
	struct hostent* ci = gethostbyname(address);
	if (ci == 0)
		return false;

	m_client.sin_family = ci->h_addrtype;
	m_client.sin_port = ntohs((u_short)port);
	memcpy(&m_client.sin_addr.s_addr, ci->h_addr_list[0], ci->h_length);

	SocketOps::Blocking(fd_);
	if (connect(fd_, (const sockaddr *)&m_client, sizeof(m_client)) == -1)
	{
		return false;
	}
	
	// set common parameters on the file descriptor
	SocketOps::Nonblocking(fd_);
	SocketOps::DisableBuffering(fd_);

	// IOCP stuff
#ifdef CONFIG_USE_IOCP
	if (CreateIoCompletionPort((HANDLE)fd_, SocketMgr::get_instance()->GetCompletionPort(), (ULONG_PTR)this, 0) == 0)
	{
		return false;
	}
#endif

	return true;
}

#ifdef CONFIG_USE_IOCP
bool Socket::ConnectEx(const char* address, uint16 port)
{
	struct hostent* ci = gethostbyname(address);
	if (ci == 0)
		return false;

	m_client.sin_family = ci->h_addrtype;
	m_client.sin_port = ntohs((u_short)port);
	memcpy(&m_client.sin_addr.s_addr, ci->h_addr_list[0], ci->h_length);
	
	SocketOps::Nonblocking(fd_);

	// set common parameters on the file descriptor
	SocketOps::DisableBuffering(fd_);

	// IOCP stuff
	if (CreateIoCompletionPort((HANDLE)fd_, SocketMgr::get_instance()->GetCompletionPort(), (ULONG_PTR)this, 0) == 0)
	{
		return false;
	}
	//-------------------------------------------------------------
	sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	int ret = ::bind(fd_, (const sockaddr*)&a, sizeof(a));
	DWORD last_error = ::WSAGetLastError();
	//-------------------------------------------------------------
	if (ret != 0) 
	{
		OnConnect(false);
		PRINTF_ERROR("ConnectEx bind fail");
		return false;
	}
	else //绑定成功
	{
		connect_event_.SetEvent(SOCKET_IO_EVENT_CONNECT_COMPLETE);

		LPFN_CONNECTEX connect_ex = get_connect_ex(fd_);
		PVOID lpSendBuffer = NULL;
		DWORD dwSendDataLength = 0;
		DWORD dwBytesSent = 0;
		bool result = connect_ex(fd_, (const sockaddr*)&m_client, sizeof(m_client), lpSendBuffer, dwSendDataLength, &dwBytesSent, &connect_event_.overlap_);

		if (!result)
		{
			last_error = ::WSAGetLastError();
			if ( last_error != ERROR_SUCCESS && last_error != ERROR_IO_PENDING )  // 调用失败
			{
				OnConnect(false);
				return false;
			}
		}
	}

	return true;
}
#else
bool Socket::ConnectEx(const char* address, uint16 port)
{
	struct hostent* ci = gethostbyname(address);
	if (ci == 0)
	{
		return false;
	}

	m_client.sin_family = ci->h_addrtype;
	m_client.sin_port = ntohs((u_short)port);
	memcpy(&m_client.sin_addr.s_addr, ci->h_addr_list[0], ci->h_length);
	
	// set common parameters on the file descriptor
	SocketOps::Nonblocking(fd_);
	SocketOps::DisableBuffering(fd_);

	int ret = connect(fd_, (const sockaddr *)&m_client, sizeof(m_client));
	if (ret == -1) //直接连接成功是不可能的， 因为它是异步connect
	{
		return true;
	}

	return false;
}

#endif

bool Socket::ConnectUDP(const char* address, uint16 port, uint16& local_port)
{
	struct hostent* ci = gethostbyname(address);
	if (ci == 0)
		return false;

	m_client.sin_family = ci->h_addrtype;
	m_client.sin_port = ntohs((u_short)port);
	memcpy(&m_client.sin_addr.s_addr, ci->h_addr_list[0], ci->h_length);

	sockaddr_in a;
	a.sin_family = AF_INET;
	a.sin_port = htons((u_short)local_port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);

	int ret = ::bind(fd_, (const sockaddr*)&a, sizeof(a));
	if (ret != 0)
	{
		PRINTF_ERROR("Bind unsuccessful on port:%d", local_port);
		return false;
	}

#ifdef WIN32
	int len = sizeof(a);
#else
	socklen_t len = sizeof(a);
#endif

	getsockname(fd_, (sockaddr*)&a, &len);
	local_port = ntohs(a.sin_port); // 获取端口号

	if (connect(fd_, (const sockaddr*)&m_client, sizeof(m_client)) == -1)
	{
		return false;
	}

	SocketOps::Nonblocking(fd_);
	// set common parameters on the file descriptor
	SocketOps::DisableBuffering(fd_);

	// IOCP stuff
#ifdef CONFIG_USE_IOCP
	if (CreateIoCompletionPort((HANDLE)fd_, SocketMgr::get_instance()->GetCompletionPort(), (ULONG_PTR)this, 0) == 0)
	{
		return false;
	}
#endif

	return true;
}

void Socket::Accept(sockaddr_in* address)
{
	memcpy(&m_client, address, sizeof(*address));

	// set common parameters on the file descriptor
	SocketOps::Nonblocking(fd_);
	SocketOps::DisableBuffering(fd_);

	// IOCP stuff
#ifdef CONFIG_USE_IOCP
	CreateIoCompletionPort((HANDLE)fd_, SocketMgr::get_instance()->GetCompletionPort(), (ULONG_PTR)this, 0);
#endif
}

void Socket::OnConnect(bool is_success)
{
	SocketConnectTask::process(onconnected_handler_, conn_idx_, is_success);
}

void Socket::OnRead()
{
	// 解包 = uint32 + content_len，并移除
	uint32 packet_len = GetReadBuffer().GetSize();
	uint32 cursor = 0;
	char* buffer_start = (char*)(GetReadBuffer().GetBufferStart());

	//---------------------------------------------------------------------------------------------
	// 不需要解包
	if (!is_parse_package_)
	{
		SocketReadTask::process(onrecv_handler_, conn_idx_, buffer_start, packet_len);
		
		GetReadBuffer().Remove(packet_len);
		return;
	}

	//---------------------------------------------------------------------------------------------
	uint32 len = 0;
	while (packet_len > cursor + 4) //包体不能为0，所以要 >
	{
		// (1) 解析包头
		len = *((uint32*)(buffer_start + cursor));

		// 判断一下包头的内容长度是否符合限定范围, 到时候再改
		if (len > 0 && len <= recvbuffersize_ - 4)
		{

		}
		else
		{
			PRINTF_ERROR("read packet is error! fd = %d, conn_idx = %d", fd_, conn_idx_);
			SocketMgr::get_instance()->Disconnect(conn_idx_);

			break;
		}

		// (2) 解析包体
		if (packet_len >= cursor + 4 + len) //解包成功
		{
			if (socket_type_ == SOCKET_TYPE_UDP)
			{
				if (!is_udp_connected_)
				{
					if (packet_len == sizeof(RepServerPort))
					{
						RepServerPort* msg = static_cast<RepServerPort*>((void*)(buffer_start + cursor));
						char src_port_str[8] = "port";
						if (strcmp(msg->flags, src_port_str) == 0)
						{
							m_client.sin_port = ntohs((u_short)msg->port);
							if (connect(fd_, (const sockaddr*)&m_client, sizeof(m_client)) == -1)
							{
								OnConnect(false);

								SocketMgr::get_instance()->Disconnect(conn_idx_);
							}
							else
							{
								is_udp_connected_ = true;

								// 连接成功
								OnConnect(true);
							}
						}
					}
					else
					{
						SocketMgr::get_instance()->Disconnect(conn_idx_);
					}

					cursor += 4;
				}
				else
				{
					cursor += 4;

					ikcp_input(p_kcp_, (const char*)(buffer_start + cursor), len);
					while (true)
					{
						int ret_value = ikcp_recv(p_kcp_);
						if (ret_value > 0)
						{
							continue;
						}
						else
						{
							break;
						}
					}

				}
			}
			else
			{
				cursor += 4;

				SocketReadTask::process(onrecv_handler_, conn_idx_, buffer_start + cursor, len);
			}

			cursor += len;
		}
		else
		{
			break;
		}
	}

	if (cursor)
	{
		GetReadBuffer().Remove(cursor);
	}
}

void Socket::OnDisconnect()
{
	SocketCloseTask::process(onclose_handler_, conn_idx_);
}

void Socket::Disconnect()
{
#ifdef CONFIG_USE_IOCP
	close_event_.SetEvent(SOCKET_IO_EVENT_CLOSE);
	PostQueuedCompletionStatus(SocketMgr::get_instance()->GetCompletionPort(), 0, (ULONG_PTR)this, &close_event_.overlap_);
#else
	SocketEvent event;
	event.s = this;
	event.customized_events = SOCKET_IO_EVENT_CLOSE;
	SocketMgr::get_instance()->event_queue_.push_back(event);
	SocketMgr::get_instance()->WakeUp();
#endif
}

bool Socket::Send(const void* buff, uint32 len)
{
	bool ret = writeBuffer.Write(buff, len);

#ifdef CONFIG_USE_IOCP
	if (ret)
	{
		delay_send_event_.SetEvent(SOCKET_IO_EVENT_DELAY_SEND);
		PostQueuedCompletionStatus(SocketMgr::get_instance()->GetCompletionPort(), 0, (ULONG_PTR)this, &delay_send_event_.overlap_);
	}
#else
	if (ret)
	{
		SocketEvent event;
		event.s = this;
		event.customized_events = SOCKET_IO_EVENT_DELAY_SEND;
		SocketMgr::get_instance()->event_queue_.push_back(event);
		SocketMgr::get_instance()->WakeUp();
	}
#endif

	return ret;
}

bool Socket::SendMsg(const void* buff, uint32 len)
{
	// 拼包
	bool ret = writeBuffer.WriteMsg(&len, 4, buff, len);

#ifdef CONFIG_USE_IOCP
	if (ret)
	{
		delay_send_event_.SetEvent(SOCKET_IO_EVENT_DELAY_SEND);
		PostQueuedCompletionStatus(SocketMgr::get_instance()->GetCompletionPort(), 0, (ULONG_PTR)this, &delay_send_event_.overlap_);
	}
#else
	if (ret)
	{
		SocketEvent event;
		event.s = this;
		event.customized_events = SOCKET_IO_EVENT_DELAY_SEND;
		SocketMgr::get_instance()->event_queue_.push_back(event);
		SocketMgr::get_instance()->WakeUp();
	}
#endif

	return ret;
}

bool Socket::SendUDP(const void* buff, uint32 len)
{
	int send_ret = ikcp_send(p_kcp_, (const char*)buff, len);
	if (send_ret < 0)
	{
		return false;
	}
	else
	{
		return true;
	}
}

int  Socket::udp_output(const char* buf, int len, ikcpcb* kcp, void* user)
{
	((Socket*)user)->send_udp_package(buf, len);
	return 0;
}

void Socket::send_udp_package(const char* buf, int len)
{
	AddRef(); //增加引用

	SendMsg(buf, len);
}

int  Socket::udp_input(const char* buf, int len, ikcpcb* kcp, void* user)
{
	((Socket*)user)->on_udp_package_recv(buf, len);
	return 0;
}

void Socket::on_udp_package_recv(const char* buf, int len)
{
	SocketReadTask::process(onrecv_handler_, conn_idx_, (char*)buf, len);
}