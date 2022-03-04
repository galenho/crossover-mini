/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#include "network.h"
#include "guard.h"
#include "scheduler.h"

#ifdef CONFIG_USE_EPOLL
Socket::Socket( SOCKET wakeup_fd, SocketIOThread* work_thread )
{
	is_tcp_client_ = false;
	fd_ = wakeup_fd;
	work_thread_ = work_thread;
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

	if (fd == 0) //˵����TcpClient����
	{
		is_tcp_client_ = true;
	}
	else //˵����TcpServer����
	{
		is_tcp_client_ = false;
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

			Guard guard(kcp_mutex_);
			p_kcp_ = ikcp_create(conn_idx, (void*)this);
			p_kcp_->output = &Socket::udp_output;
			p_kcp_->input = &Socket::udp_input;

			// ��������ģʽ
			// �ڶ������� nodelay-�����Ժ����ɳ�����ٽ�����
			// ���������� intervalΪ�ڲ�����ʱ�ӣ�Ĭ������Ϊ 10ms
			// ���ĸ����� resendΪ�����ش�ָ�꣬����Ϊ2
			// ��������� Ϊ�Ƿ���ó������أ������ֹ
			//ikcp_nodelay(p_kcp_, 1, 10, 2, 1);
			ikcp_nodelay(p_kcp_, 1, 10, 2, 1); // ���ó�1��ACK��Խֱ���ش�, ������Ӧ�ٶȻ����. �ڲ�ʱ��5����.
		}
	}

	status_ = socket_status_closed;

	is_udp_connected_ = false;

	//PRINTF_INFO("new fd = %d, conn_idx = %d", fd_, conn_idx_);
}

Socket::~Socket()
{
	//PRINTF_INFO("delete fd = %d, conn_idx = %d", fd_, conn_idx_);
}

void Socket::AddRef()
{
	ref_mutex_.Lock();
	Referable::AddRef();
	ref_mutex_.UnLock();
}

bool Socket::Release()
{
	ref_mutex_.Lock();
	bool ret = Referable::Release();
	ref_mutex_.UnLock();

	return ret;
}

void Socket::Update( uint32 cur_time )
{
	if (socket_type_ == SOCKET_TYPE_UDP)
	{
		Guard guard(kcp_mutex_);
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
typedef bool (PASCAL *connect_ex_fn)(SOCKET,
									 const sockaddr*, int, void*, DWORD, DWORD*, OVERLAPPED*);

connect_ex_fn get_connect_ex(SOCKET fd)
{
	GUID guid = { 0x25a207b9, 0xddf3, 0x4660,
	{ 0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e } };

	void *ptr = NULL;
	DWORD bytes = 0;
	if (::WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid), &ptr, sizeof(ptr), &bytes, 0, 0) != 0)
	{
		// Set connect_ex_ to a special value to indicate that ConnectEx is
		// unavailable. That way we won't bother trying to look it up again.
		ASSERT(false);
	}

	return reinterpret_cast<connect_ex_fn>(ptr);
}

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
	else //�󶨳ɹ�
	{
		connect_event_.SetEvent(SOCKET_IO_EVENT_CONNECT_COMPLETE);
		connect_ex_fn connect_ex = get_connect_ex(fd_);
		bool result = connect_ex(fd_, (const sockaddr*)&m_client, sizeof(m_client), 0, 0, 0, &connect_event_.overlap_);
		if (!result)
		{
			last_error = ::WSAGetLastError();
			if ( last_error != ERROR_SUCCESS && last_error != ERROR_IO_PENDING )  // ����ʧ��
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
	if (ret == -1) //ֱ�����ӳɹ��ǲ����ܵģ� ��Ϊ�����첽connect
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
	local_port = ntohs(a.sin_port); // ��ȡ�˿ں�

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
	TcpConnectTask::process(onconnected_handler_, onclose_handler_, onrecv_handler_, conn_idx_, is_success, is_tcp_client_);
}

void Socket::OnRead()
{
	// ��� = uint32 + content_len�����Ƴ�
	uint32 packet_len = GetReadBuffer().GetSize();
	uint32 cursor = 0;
	char* buffer_start = (char*)(GetReadBuffer().GetBufferStart());

	//---------------------------------------------------------------------------------------------
	// ����Ҫ���
	if (!is_parse_package_)
	{
		TcpReadTask::process(onrecv_handler_, conn_idx_, buffer_start, packet_len);
		
		GetReadBuffer().Remove(packet_len);
		return;
	}

	//---------------------------------------------------------------------------------------------
	uint32 len = 0;
	while (packet_len > cursor + 4) //���岻��Ϊ0������Ҫ >
	{
		// (1) ������ͷ
		len = *((uint32*)(buffer_start + cursor));

		// �ж�һ�°�ͷ�����ݳ����Ƿ�����޶���Χ, ��ʱ���ٸ�
		if (len > 0 && len <= recvbuffersize_ - 4)
		{

		}
		else
		{
			PRINTF_ERROR("read packet is error! fd = %d, conn_idx = %d", fd_, conn_idx_);
			SocketMgr::get_instance()->Disconnect(conn_idx_);

			break;
		}

		// (2) ��������
		if (packet_len >= cursor + 4 + len) //����ɹ�
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

								// ���ӳɹ�
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

					Guard guard(kcp_mutex_);

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

				TcpReadTask::process(onrecv_handler_, conn_idx_, buffer_start + cursor, len);
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
	TcpCloseTask::process(onconnected_handler_, onrecv_handler_, onclose_handler_, conn_idx_, is_tcp_client_);
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
	work_thread_->event_queue_.push_back(event);
	work_thread_->WakeUp();
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
		work_thread_->event_queue_.push_back(event);
		work_thread_->WakeUp();
	}
#endif

	return ret;
}

bool Socket::SendMsg(const void* buff, uint32 len)
{
	// ƴ��
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
		work_thread_->event_queue_.push_back(event);
		work_thread_->WakeUp();
	}
#endif

	return ret;
}

bool Socket::SendUDP(const void* buff, uint32 len)
{
	Guard guard(kcp_mutex_);
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
	SendMsg(buf, len);
}

int  Socket::udp_input(const char* buf, int len, ikcpcb* kcp, void* user)
{
	((Socket*)user)->on_udp_package_recv(buf, len);
	return 0;
}

void Socket::on_udp_package_recv(const char* buf, int len)
{
	TcpReadTask::process(onrecv_handler_, conn_idx_, (char*)buf, len);
}