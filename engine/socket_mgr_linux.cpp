/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#include "network.h"

#ifdef CONFIG_USE_EPOLL

#include "netinet/tcp.h"
#include "guard.h"

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
		SocketOps::CloseSocket(fd); //ֱ�ӵ��ùر�closesocket
	}
}

void HandleReadComplete(Socket* s)
{
	if (s->status_ != socket_status_connectted)
	{
		return;
	}

	//---------------------------------------------------------------------------------------------------------------
	// ������ѭ����ȡ
	bool rs = true;
	while(rs)
	{
		int space = (int)(s->GetReadBuffer().GetSpace());
		if (space == 0)
		{
			//PRINTF_ERROR("s->GetFd() = %d, space == 0", s->GetFd());
			return;
		}

		int bytes = recv(s->GetFd(), s->GetReadBuffer().GetBuffer(), space, 0); //�ȶ���ϵͳ������
		if (bytes < 0)
		{
			// �����Ƿ�������ģʽ,���Ե�errnoΪEAGAINʱ,��ʾ��ǰ�������������ݿɶ�
			// ������͵����Ǹô��¼��Ѵ���.
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				//galen: ���ù�, ����ճ��������, recv��һ�ΰѵڶ��εİ�������
				break;
			}
			else
			{
				//PRINTF_ERROR("conn_idx = %d, bytes = %d, errno = %d", s->GetConnectIdx(), bytes, errno);
				return;
			}
		}
		else if(bytes == 0) //Socket�ر��¼�
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
					close(s->GetFd()); //ֱ�ӵ��ùر�closesocket

					s->status_ = socket_status_closed;
					s->OnConnect(false);
				}
			}

			return;
		}
		else
		{
			// ��������
			//PRINTF_INFO("conn_idx = %d, recv bytes = %d", s->GetConnectIdx(), bytes);

			s->GetReadBuffer().IncrementWritten(bytes);
			s->OnRead();
		}
		//----------------------------------------------------
		if(bytes == space)
		{
			rs = true;   // ��Ҫ�ٴζ�ȡ
		}
		else
		{
			rs = false; 
		}
	}

	//---------------------------------------------------------------------
	s->GetWriteBuffer().FillVector(); //galen: ����Ƿ�ֹ�л�������չ, new�������ڴ�
	if (s->GetWriteBuffer().GetSize() > 0)
	{
		s->BurstPush(); //���д��������������ݵĻ����ѿ���Ȩ���д
	}
}

void HandleCanWrite(Socket* s)
{
	if (!s->HasSendLock())
	{
		// galen:����д��������, �����ɲ���д��Ϊ��дʱ������EPOLLOUT�¼�
		return;
	}

	if (s->status_ == socket_status_connectted)
	{
		s->GetWriteBuffer().FillVector(); //galen: ����Ƿ�ֹ�л�������չ, new�������ڴ�

		s->WriteCallback();   		// Perform actual send()
		if (s->GetWriteBuffer().GetSize() == 0)
		{
			// change back to a read event
			s->DecSendLock();

			s->PostEvent(EPOLLIN);
		}
		else
		{
			// galen: ���ҲҪ�е�EPOLLIN״̬, ��Ϊ���MS----CS���߶�����д�������, �ڵȴ��Զ�recv�������ֽڲŻ᷵��EPOLLOUT�¼�, �Ǿ�������
			s->PostEvent(EPOLLIN | EPOLLOUT);
		}
	}
	else if (s->status_ == socket_status_closing) 
	{
		// ����closesocketҪ��ReactorThread�߳�û��EPOLLIN����Ϣ�������ܹ�����, ��ptr���õ�Socket����ָ�뱻delete�����Ұָ��, 
		// ���������ر�socket�Ļ�, ����Ǵ���EPOLLIN״̬, Ҫ�Ȱѿ�������EPOLLOUT, �ӳ������ﴦ��

		SocketMgr::get_instance()->CloseSocket(s);
	}
}

void HandleDelaySend(Socket* s)
{
	if (s->status_ == socket_status_connectted)
	{
		s->GetWriteBuffer().FillVector(); //galen: ����Ƿ�ֹ�л�������չ, new�������ڴ�
		if (s->GetWriteBuffer().GetSize() > 0)
		{
			s->BurstPush(); //���д��������������ݵĻ����ѿ���Ȩ���д
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
	epoll_fd = 0;
}

SocketMgr::~SocketMgr()
{	
	for (uint32 n=0; n < io_threads_.size(); n++)
	{
		delete io_threads_[n];
		io_threads_[n] = NULL;
	}
	
	thread_count_ = 0;
	io_threads_.clear();
}

bool SocketMgr::Init(uint32 thread_count)
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

	wakeup_s_ = new Socket(wakeup_fd, this);

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
				auto_conn_idx_ = 1; //��ͷ��ʼ
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
	return epoll_fd;
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
	}
}

int SocketMgr::EventLoop(uint32 cur_time)
{
	int32 fd_count = epoll_wait(epoll_fd, events, THREAD_EVENT_SIZE, -1);

	//PRINTF_INFO("epoll_fd = %d , epoll_wait fd_count = %d", epoll_fd, fd_count);
	//PRINTF_INFO("---------------------------------------------------");

	if (fd_count == 0) // ��ʱ��
	{
		return 0;
	}
	else if (fd_count > 0)
	{
		for (int i = 0; i < fd_count; i++)
		{
			uint32 event = events[i].events;
			Socket* s = (Socket*)(events[i].data.ptr);

			//PRINTF_INFO("epoll_wait fd = %d, events = %d", s->GetFd(), events[i].events);

			if (s == wakeup_s_) //�����¼�
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
			//------------------------------------------------------------
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
		}
	}
	else
	{
		// �д���
	}

	return 0;
}

void SocketMgr::Update( uint32 cur_time )
{
	hash_map<uint32, Socket*>::iterator it = socket_map_.begin();
	for (; it != socket_map_.end(); it++)
	{
		Socket* s = it->second;
	
		s->Update(cur_time);
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
	
	AddSocket(s); //����socket�б�

	s->status_ = socket_status_connectted;

	// Add epoll event based on socket activity.
	struct epoll_event ev;
	memset(&ev, 0, sizeof(epoll_event));
	ev.events = (s->GetWriteBuffer().GetSize()) ? EPOLLOUT : EPOLLIN;
	ev.events = ev.events | EPOLLERR | EPOLLHUP | EPOLLPRI| EPOLLET;
	ev.data.ptr = s;

	if (epoll_ctl(s->work_thread_->GetEpollFd(), EPOLL_CTL_ADD, aSocket, &ev))
	{
		PRINTF_ERROR("epoll", "Could not add event to epoll set on fd %u ", s->GetFd());
		
		SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket
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

		if (epoll_ctl(s->work_thread_->GetEpollFd(), EPOLL_CTL_ADD, s->GetFd(), &ev))
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

		if (epoll_ctl(s->work_thread_->GetEpollFd(), EPOLL_CTL_ADD, s->GetFd(), &ev))
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
	const connected_handler_type onconnected_handler,
	const close_handler_type onclose_handler,
	const recv_handler_type onrecv_handler,
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
		AddSocket(s); //����socket�б�

		s->status_ = socket_status_connectted;
		//----------------------------------------------------------------------------------------
		// Add epoll event based on socket activity.
		struct epoll_event ev;
		memset(&ev, 0, sizeof(epoll_event));
		ev.events = EPOLLOUT | EPOLLIN;
		ev.events = ev.events | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET;
		ev.data.ptr = s;

		if (epoll_ctl(s->work_thread_->GetEpollFd(), EPOLL_CTL_ADD, s->GetFd(), &ev))
		{
			PRINTF_ERROR("Could not add event to epoll set on fd %u", s->GetFd());

			SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket
			s->status_ = socket_status_closed;
			s->OnConnect(false);
			RemoveSocket(s->GetConnectIdx());

			return INVALID_INDEX;
		}
		else
		{
			if (is_server_build) //����Ƿ���������������
			{
				s->is_udp_connected_ = true;
				s->OnConnect(true);
			}

			return conn_idx;
		}

	}
}

bool SocketMgr::AcceptUDP(sockaddr_in& remote_address,
	const connected_handler_type onconnected_handler,
	const close_handler_type onclose_handler,
	const recv_handler_type onrecv_handler,
	uint32 sendbuffersize,
	uint32 recvbuffersize,
	uint16& local_port)
{
	// ����һ����ʱ��socket
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
		if (epoll_ctl(s->work_thread_->GetEpollFd(), EPOLL_CTL_DEL, s->GetFd(), &ev))
		{
			PRINTF_ERROR("RemoveSocket Could not remove fd %u from epoll set, errno %u", conn_idx, errno);
		}
		//--------------------------------------------------------------------------------------------
	
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

		//galen: ��Ϊ������������Disconnect�ͻ��˵�ʱ����ΪHandleClose�ӳ�close, ״̬����socket_status_closing, ����ҲҪSocketOps::CloseSocket(fd);
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
	}
	//--------------------------------------------------------------------------
	if (s)
	{
		bool ret = s->Send(content, len);
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
				ret = s->SendUDP(content, len); //�����а�ͷ����Ϣ
			}
			else
			{
				ret = s->SendMsg(content, len);
			}

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
