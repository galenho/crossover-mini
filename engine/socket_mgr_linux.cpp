/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#include "network.h"

#ifdef CONFIG_USE_EPOLL

#include "netinet/tcp.h"
#include "guard.h"
#include "tcp_listen_socket_win32.h"
#include "udp_listen_socket_win32.h"

void HandleAcceptTCPComplete(TCPListenSocket* s)
{
	int len = 0;
	struct sockaddr_in tempAddress;

	SOCKET aSocket = accept(s->socket_, (sockaddr*)&tempAddress, (socklen_t*)&len);
	if (aSocket == -1)
	{
		printf("accept error\n");
		return;
	}

	SocketMgr::get_instance()->Accept(aSocket,
		tempAddress,
		s->onconnected_handler_,
		s->onclose_handler_,
		s->onrecv_handler_,
		s->sendbuffersize_,
		s->recvbuffersize_,
		s->is_parse_package_);
}

void HandleAcceptUDPComplete(UDPListenSocket* s)
{
	printf("1\n");

	sockaddr_in	client_addr_;
	socklen_t addr_len = sizeof(client_addr_);
	int bytes = recvfrom(s->socket_, s->buff_, sizeof(s->buff_), 0, (sockaddr*)&(client_addr_), &addr_len);
	if (bytes > 0)
	{
		if (bytes != 8 + 4)
		{
			return;
		}

		printf("2\n");
		uint8* buffer_start = (uint8*)(s->buff_);
		uint32 len = *((uint32*)(buffer_start));
		if (len == 8)
		{
			printf("3\n");

			char src_conn_str[8] = "connect";
			char dst_conn_str[8];
			memcpy(dst_conn_str, (uint8*)(buffer_start + 4), 8);
			if (strcmp(src_conn_str, dst_conn_str) == 0)
			{
				// 模拟tcp的accept, 创建一个新的一个udp socket
				uint16 out_port = 0;
				bool ret = SocketMgr::get_instance()->AcceptUDP(client_addr_,
					s->onconnected_handler_,
					s->onclose_handler_,
					s->onrecv_handler_,
					s->sendbuffersize_,
					s->recvbuffersize_,
					out_port);

				// 回发一个连接包给UDP客户端, 告诉客户端新的端口号
				if (ret)
				{
					RepServerPort rep_msg;
					rep_msg.port = out_port;
					sendto(s->socket_, (char*)&rep_msg, sizeof(rep_msg), 0, (sockaddr*)&client_addr_, sizeof(client_addr_));
				}
			}
		}
	}
}

void HandleConnect(Socket* s, bool is_success)
{
	if (is_success)
	{
		s->status_ = socket_status_connectted;

		s->PostEvent(EPOLLIN);
		s->OnConnect(true);
	}
	else
	{
		s->OnConnect(false);

		s->status_ = socket_status_closed;
		SOCKET fd = s->GetFd();
		SocketMgr::get_instance()->RemoveSocket(s->GetConnectIdx());
		SocketOps::CloseSocket(fd); //直接调用关闭closesocket
	}
}

void HandleReadComplete(Socket* s)
{
	if (s->status_ != socket_status_connectted)
	{
		return;
	}

	//---------------------------------------------------------------------------------------------------------------
	// 下面是循环读取
	bool rs = true;
	while(rs)
	{
		int space = (int)(s->GetReadBuffer().GetSpace());
		if (space == 0)
		{
			//PRINTF_ERROR("s->GetFd() = %d, space == 0", s->GetFd());
			return;
		}

		int bytes = recv(s->GetFd(), s->GetReadBuffer().GetBuffer(), space, 0); //先读入系统缓冲区
		if (bytes < 0)
		{
			// 由于是非阻塞的模式,所以当errno为EAGAIN时,表示当前缓冲区已无数据可读
			// 在这里就当作是该次事件已处理处.
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				//galen: 不用管, 由于粘包的问题, recv第一次把第二次的包都读了
				break;
			}
			else
			{
				//PRINTF_ERROR("conn_idx = %d, bytes = %d, errno = %d", s->GetConnectIdx(), bytes, errno);
				return;
			}
		}
		else if(bytes == 0) //Socket关闭事件
		{
			//PRINTF_ERROR("conn_idx = %d, bytes = %d, errno = %d", s->GetConnectIdx(), bytes, errno);
			
			if (s->GetSocketType() == SOCKET_TYPE_TCP)
			{
				SocketMgr::get_instance()->CloseSocket(s);
			}
			else //UDP
			{
				if (s->is_udp_connected_)
				{
					SocketMgr::get_instance()->CloseSocket(s);
				}
				else
				{
					close(s->GetFd()); //直接调用关闭closesocket

					s->status_ = socket_status_closed;
					s->OnConnect(false);
				}
			}

			return;
		}
		else
		{
			// 正常接收
			//PRINTF_INFO("conn_idx = %d, recv bytes = %d", s->GetConnectIdx(), bytes);

			s->GetReadBuffer().IncrementWritten(bytes);
			s->OnRead();
		}
		//----------------------------------------------------
		if(bytes == space)
		{
			rs = true;   // 需要再次读取
		}
		else
		{
			rs = false; 
		}
	}

	//---------------------------------------------------------------------
	s->GetWriteBuffer().FillVector(); //galen: 这句是防止有缓冲区扩展, new出来的内存
	if (s->GetWriteBuffer().GetSize() > 0)
	{
		s->BurstPush(); //如果写缓冲区里边有数据的话，把控制权变给写
	}
}

void HandleCanWrite(Socket* s)
{
	if (!s->HasSendLock())
	{
		// galen:挡掉写缓冲区满, 后面由不可写变为可写时触发的EPOLLOUT事件
		return;
	}

	if (s->status_ == socket_status_connectted)
	{
		s->GetWriteBuffer().FillVector(); //galen: 这句是防止有缓冲区扩展, new出来的内存

		s->WriteCallback();   		// Perform actual send()
		if (s->GetWriteBuffer().GetSize() == 0)
		{
			// change back to a read event
			s->DecSendLock();

			s->PostEvent(EPOLLIN);
		}
		else
		{
			// galen: 这个也要切到EPOLLIN状态, 因为如果MS----CS两边都处于写缓冲溢出, 在等待对端recv掉缓冲字节才会返回EPOLLOUT事件, 那就死锁了
			s->PostEvent(EPOLLIN | EPOLLOUT);
		}
	}
	else if (s->status_ == socket_status_closing) 
	{
		// 由于closesocket要在ReactorThread线程没有EPOLLIN的消息监听才能够结束, 怕ptr引用的Socket对象指针被delete后造成野指针, 
		// 所以主动关闭socket的话, 如果是处于EPOLLIN状态, 要先把开关设置EPOLLOUT, 延迟在这里处理

		SocketMgr::get_instance()->CloseSocket(s);
	}
}

void HandleDelaySend(Socket* s)
{
	if (s->status_ == socket_status_connectted)
	{
		s->GetWriteBuffer().FillVector(); //galen: 这句是防止有缓冲区扩展, new出来的内存
		if (s->GetWriteBuffer().GetSize() > 0)
		{
			s->BurstPush(); //如果写缓冲区里边有数据的话，把控制权变给写
		}
	}
}

void HandleClose(Socket* s)
{
	if (s->status_ == socket_status_connectted)
	{
		s->status_ = socket_status_closing;
		s->BurstPush();
	}
}

//----------------------------------------------------------------------------------------------------
initialiseSingleton(SocketMgr);
SocketMgr::SocketMgr()
{
	auto_conn_idx_ = 1;
	epoll_fd_ = 0;
}

SocketMgr::~SocketMgr()
{	
	
}

bool SocketMgr::Init()
{
	epoll_fd_ = epoll_create(SOCKET_HOLDER_SIZE);

	if (epoll_fd_ == -1)
	{
		PRINTF_ERROR("Could not create epoll fd (/dev/epoll).");
		return false;
	}

	int wakeup_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (wakeup_fd < 0)
	{
		return false;
	}

	wakeup_s_ = new Socket(wakeup_fd);

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.ptr = wakeup_s_;
	int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_s_->GetFd(), &event);
	if (ret < 0)
	{
		return false;
	}

	return true;
}

bool SocketMgr::Close()
{
	if (wakeup_s_)
	{
		epoll_event ev = { 0, { 0 } };
		epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, wakeup_s_->GetFd(), &ev);

		close(wakeup_s_->GetFd());

		delete wakeup_s_;
		wakeup_s_ = NULL;
	}

	if (epoll_fd_)
	{
		close(epoll_fd_);
	}

	return true;
}

void SocketMgr::WakeUp()
{
	uint64 one = 1;
	::write(wakeup_s_->GetFd(), &one, sizeof(one));
}

uint32 SocketMgr::MakeGeneralConnID()
{
	uint32 conn_idx = INVALID_INDEX;

	while (true)
	{
		hash_map<uint32, Socket*>::iterator it = socket_map_.find(auto_conn_idx_);
		if (it != socket_map_.end())
		{
			++auto_conn_idx_;
			if (auto_conn_idx_ == INVALID_INDEX)
			{
				auto_conn_idx_ = 1; //从头开始
			}
		}
		else
		{
			break;
		}
	}

	conn_idx = auto_conn_idx_;
	++auto_conn_idx_;
	if (auto_conn_idx_ == INVALID_INDEX)
	{
		auto_conn_idx_ = 1;
	}

	return conn_idx;
}

int SocketMgr::GetEpollFd()
{
	return epoll_fd_;
}

void SocketMgr::HandleDelayEvent()
{
	SocketEvent event;
	while (event_queue_.pop(event))
	{
		Socket* s = event.s;

		if (event.customized_events == SOCKET_IO_EVENT_CLOSE)
		{
			HandleClose(s);
		}
		else if (event.customized_events == SOCKET_IO_EVENT_DELAY_SEND)
		{
			HandleDelaySend(s);
		}

		REF_RELEASE(s);
	}
}

int SocketMgr::EventLoop(int32 timeout)
{
	uint32 start_time = getMSTime();
	Update(start_time);

	while (true)
	{
		uint32 cur_time = getMSTime();

		uint32 timeout_time = 0;
		if (timeout - (int32)(cur_time - start_time) > 0)
		{
			timeout_time = timeout - (int32)(cur_time - start_time);
		}

		int32 fd_count = epoll_wait(epoll_fd_, events, THREAD_EVENT_SIZE, timeout_time);

		//PRINTF_INFO("epoll_fd = %d , epoll_wait fd_count = %d", epoll_fd, fd_count);
		//PRINTF_INFO("---------------------------------------------------");

		if (fd_count == 0) // 定时器
		{
			break;
		}
		else if (fd_count > 0)
		{
			for (int i = 0; i < fd_count; i++)
			{
				uint32 event = events[i].events;
				SocketBase* socket = (Socket*)(events[i].data.ptr);

				//PRINTF_INFO("epoll_wait events = %d", events[i].events);

				if (socket == wakeup_s_) //唤醒事件
				{
					if (event & EPOLLIN)
					{
						uint64 one = 1;
						uint32 size = read(wakeup_s_->GetFd(), &one, sizeof(one));
						if (size == sizeof(one))
						{
							HandleDelayEvent();
						}
					}

					continue;
				}
				else if (socket->is_listen_) //监听
				{
					if (socket->socket_type_ == SOCKET_TYPE_TCP)
					{
						TCPListenSocket* s = (TCPListenSocket*)socket;
						HandleAcceptTCPComplete(s);
					}
					else if(socket->socket_type_ == SOCKET_TYPE_UDP)
					{
						UDPListenSocket* s = (UDPListenSocket*)socket;
						HandleAcceptUDPComplete(s);
					}
					
					continue;
				}
				else
				{
					Socket* s = (Socket*)socket;
					REF_ADD(s);
					if (event & EPOLLHUP || event & EPOLLERR)
					{
						if (s->status_ == socket_status_connecting)
						{
							HandleConnect(s, false);
						}
						else
						{
							SocketMgr::get_instance()->CloseSocket(s);
						}
					}
					else
					{
						if (event & EPOLLIN)
						{
							HandleReadComplete(s);
						}

						if (event & EPOLLOUT)
						{
							if (s->status_ == socket_status_connecting)
							{
								HandleConnect(s, true);
							}
							else
							{
								HandleCanWrite(s);
							}
						}
					}
					REF_RELEASE(s);
				}
			}
		}
		else
		{
			// 有错误
			break;
		}
	}
	
	return 0;
}

void SocketMgr::Update( uint32 cur_time )
{
	hash_map<uint32, Socket*>::iterator it = socket_map_.begin();
	for (; it != socket_map_.end(); it++)
	{
		Socket* s = it->second;
	
		REF_ADD(s);
		s->Update(cur_time);
		REF_RELEASE(s);
	}
}

void SocketMgr::Accept( SOCKET aSocket, 
						sockaddr_in& address, 
						const HandleInfo onconnected_handler,
						const HandleInfo onclose_handler,
						const HandleInfo onrecv_handler,
						uint32 sendbuffersize, 
						uint32 recvbuffersize,
						bool is_parse_package )
{
	Socket *s = new Socket( SOCKET_TYPE_TCP,
							aSocket,
							MakeGeneralConnID(),
							onconnected_handler,
							onclose_handler,
							onrecv_handler,
							sendbuffersize, 
							recvbuffersize,
							is_parse_package);
	
	AddSocket(s); //加入socket列表

	s->status_ = socket_status_connectted;

	// Add epoll event based on socket activity.
	struct epoll_event ev;
	memset(&ev, 0, sizeof(epoll_event));
	ev.events = (s->GetWriteBuffer().GetSize()) ? EPOLLOUT : EPOLLIN;
	ev.events = ev.events | EPOLLERR | EPOLLHUP | EPOLLPRI| EPOLLET;
	ev.data.ptr = s;

	int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, aSocket, &ev))
	{
		PRINTF_ERROR("epoll", "Could not add event to epoll set on fd %u ", s->GetFd());
		
		SocketOps::CloseSocket(s->GetFd()); //直接调用关闭closesocket
		RemoveSocket(s->GetConnectIdx());
	}
	else
	{
		s->Accept(&address);
		s->OnConnect(true);
	}
}

uint32 SocketMgr::Connect(const string& ip, uint16 port,
						  const HandleInfo onconnected_handler,
						  const HandleInfo onclose_handler,
						  const HandleInfo onrecv_handler,
						  uint32 sendbuffersize, 
						  uint32 recvbuffersize)
{
	uint32 conn_idx = MakeGeneralConnID();
	Socket *s = new Socket( SOCKET_TYPE_TCP,
							0,
							conn_idx,
							onconnected_handler, 
							onclose_handler, 
							onrecv_handler,
							sendbuffersize, 
							recvbuffersize );

	s->status_ = socket_status_connecting;

	bool ret = s->Connect(ip.c_str(), port);
	if (!ret)
	{
		delete s;
		s = NULL;
		return INVALID_INDEX;
	}
	else
	{
		AddSocket(s);

		s->status_ = socket_status_connectted;

		//----------------------------------------------------------------------------------------
		struct epoll_event ev;
		memset(&ev, 0, sizeof(epoll_event));
		ev.events = (s->GetWriteBuffer().GetSize()) ? EPOLLOUT : EPOLLIN;
		ev.events = ev.events | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET; 
		ev.data.ptr = s;

		int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s->GetFd(), &ev))
		{
			PRINTF_ERROR("Could not add event to epoll set on fd %u", s->GetFd());
		}
		//----------------------------------------------------------------------------------------
		s->OnConnect(true);

		return s->GetConnectIdx();
	}
}

uint32 SocketMgr::ConnectEx( const string& ip, uint16 port,
						  const HandleInfo onconnected_handler,
						  const HandleInfo onclose_handler,
						  const HandleInfo onrecv_handler,
						  uint32 sendbuffersize, 
						  uint32 recvbuffersize,
						  bool is_parse_package)
{
	uint32 conn_idx = MakeGeneralConnID();
	Socket *s = new Socket( SOCKET_TYPE_TCP, 
		0,
		conn_idx,
		onconnected_handler, 
		onclose_handler, 
		onrecv_handler,
		sendbuffersize, 
		recvbuffersize,
		is_parse_package);

	s->status_ = socket_status_connecting;

	bool ret = s->ConnectEx(ip.c_str(), port);
	if (ret)
	{
		AddSocket(s);
		//----------------------------------------------------------------------------------------
		struct epoll_event ev;
		memset(&ev, 0, sizeof(epoll_event));
		ev.events = EPOLLOUT | EPOLLIN;
		ev.events = ev.events | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET; 
		ev.data.ptr = s;

		int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s->GetFd(), &ev))
		{
			PRINTF_ERROR("Could not add event to epoll set on fd %u", s->GetFd());
		}
	}
	else
	{
		delete s;
		s = NULL;
	}

	return conn_idx;
}
//------------------------------------------------------------------------------
uint32 SocketMgr::ConnectUDP(const string& ip, uint16 port, uint16& local_port,
	const HandleInfo onconnected_handler,
	const HandleInfo onclose_handler,
	const HandleInfo onrecv_handler,
	uint32 sendbuffersize,
	uint32 recvbuffersize,
	bool is_server_build /*= false*/)
{
	uint32 conn_idx = MakeGeneralConnID();

	Socket* s = new Socket(SOCKET_TYPE_UDP,
		0,
		conn_idx,
		onconnected_handler,
		onclose_handler,
		onrecv_handler,
		sendbuffersize,
		recvbuffersize);

	s->status_ = socket_status_connecting;

	bool ret = s->ConnectUDP(ip.c_str(), port, local_port);
	if (!ret)
	{
		delete s;
		s = NULL;
		return INVALID_INDEX;
	}
	else
	{
		AddSocket(s); //加入socket列表

		s->status_ = socket_status_connectted;
		//----------------------------------------------------------------------------------------
		// Add epoll event based on socket activity.
		struct epoll_event ev;
		memset(&ev, 0, sizeof(epoll_event));
		ev.events = EPOLLOUT | EPOLLIN;
		ev.events = ev.events | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET;
		ev.data.ptr = s;

		int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, s->GetFd(), &ev))
		{
			PRINTF_ERROR("Could not add event to epoll set on fd %u", s->GetFd());

			SocketOps::CloseSocket(s->GetFd()); //直接调用关闭closesocket
			s->status_ = socket_status_closed;
			s->OnConnect(false);
			RemoveSocket(s->GetConnectIdx());

			return INVALID_INDEX;
		}
		else
		{
			if (is_server_build) //如果是服务器主动创建的
			{
				s->is_udp_connected_ = true;
				s->OnConnect(true);
			}

			return conn_idx;
		}

	}
}

bool SocketMgr::AcceptUDP(sockaddr_in& remote_address,
	const HandleInfo onconnected_handler,
	const HandleInfo onclose_handler,
	const HandleInfo onrecv_handler,
	uint32 sendbuffersize,
	uint32 recvbuffersize,
	uint16& local_port)
{
	// 分配一个临时的socket
	string ip = inet_ntoa(remote_address.sin_addr);
	uint16 port = htons(remote_address.sin_port);

	uint32 conn_idx = ConnectUDP(ip, port, local_port,
		onconnected_handler,
		onclose_handler,
		onrecv_handler,
		sendbuffersize,
		recvbuffersize,
		true);

	if (conn_idx != INVALID_INDEX)
	{
		return true;
	}
	else
	{
		return false;
	}
}

//------------------------------------------------------------------------------
void SocketMgr::AddSocket(Socket* s)
{
	hash_map<uint32, Socket*>::iterator it = socket_map_.find(s->GetConnectIdx());
	if (it != socket_map_.end())
	{
		ASSERT(false);
	}
	else
	{
		if (socket_map_.insert(make_pair(s->GetConnectIdx(), s)).second)
		{
			REF_ADD(s);
			//PRINTF_DEBUG("add socket count = %d", socket_map_.size());
		}
		else
		{
			ASSERT(false);
		}
	}
}

void SocketMgr::RemoveSocket(uint32 conn_idx)
{
	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		Socket *s = it->second;
		//--------------------------------------------------------------------------------------------
		epoll_event ev = { 0, { 0 } };
		int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
		if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, s->GetFd(), &ev))
		{
			PRINTF_ERROR("RemoveSocket Could not remove fd %u from epoll set, errno %u", conn_idx, errno);
		}
		//--------------------------------------------------------------------------------------------
		REF_RELEASE(s);

		socket_map_.erase(it);

		//PRINTF_DEBUG("remove socket count = %d", socket_map_.size());
	}
}

void SocketMgr::Disconnect(uint32 conn_idx)
{
	Socket* s = NULL;

	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		s = it->second;
		REF_ADD(s);
	}
	//------------------------------------------------------------------------
	if (s)
	{
		s->Disconnect();
	}
}

void SocketMgr::CloseSocket(Socket* s)
{
	if (s->status_ == socket_status_closing || s->status_ == socket_status_connectted)
	{
		s->status_ = socket_status_closed;
		s->OnDisconnect();

		SOCKET fd = s->GetFd();
		RemoveSocket(s->GetConnectIdx());

		//galen: 因为服务器端主动Disconnect客户端的时候，因为HandleClose延迟close, 状态会变成socket_status_closing, 所以也要SocketOps::CloseSocket(fd);
		SocketOps::CloseSocket(fd); 
	}
}

bool SocketMgr::Send(uint32 conn_idx, const void* content, uint32 len)
{
	Socket* s = NULL;
	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		s = it->second;
		REF_ADD(s);
	}
	//--------------------------------------------------------------------------
	if (s)
	{
		bool ret = s->Send(content, len);
		if (!ret)
		{
			REF_RELEASE(s);
		}

		return ret;
	}
	else
	{
		return false;
	}
}

bool SocketMgr::SendMsg(uint32 conn_idx, const void* content, uint32 len)
{
	Socket* s = NULL;
	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		s = it->second;
		REF_ADD(s);
	}
	//--------------------------------------------------------------------------
	if (s)
	{
		bool ret = false;

		if (s->GetSocketType() == SOCKET_TYPE_TCP)
		{
			ret = s->SendMsg(content, len);
		}
		else
		{
			if (s->is_udp_connected_)
			{
				ret = s->SendUDP(content, len); //不带有包头的消息
			}
			else
			{
				ret = s->SendMsg(content, len);
			}

		}

		if (!ret)
		{
			REF_RELEASE(s);
		}

		return ret;
	}
	else
	{
		return false;
	}
}

char* SocketMgr::GetIpAddress( uint32 conn_idx )
{
	char* ip_addr = NULL;

	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		Socket* s = it->second;

		ip_addr = inet_ntoa(s->GetRemoteStruct().sin_addr);	
	}

	return ip_addr;
}

#endif
