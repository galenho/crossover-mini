#include "network.h"
#include "timer.h"
#include "guard.h"
#include "tcp_listen_socket_win32.h"
#include "udp_listen_socket_win32.h"

#ifdef CONFIG_USE_IOCP
void HandleAcceptTCPComplete(TCPListenSocket* s, SOCKET aSocket)
{
	int addr_len = sizeof(sockaddr_in);

	sockaddr_in* addr_local = NULL;
	int addr_len_local = addr_len;
	sockaddr_in* addr_client = NULL;
	int addr_len_client = addr_len;

	s->accept_addrs_(s->buff_, 0,
		addr_len + 16, addr_len + 16,
		(LPSOCKADDR*)&addr_local, &addr_len_local,
		(LPSOCKADDR*)&addr_client, &addr_len_client);

	SocketMgr::get_instance()->Accept(aSocket,
		*addr_client,
		s->onconnected_handler_,
		s->onclose_handler_,
		s->onrecv_handler_,
		s->sendbuffersize_,
		s->recvbuffersize_,
		s->is_parse_package_);


	// ��Ͷ��һ��AcceptEx
	s->PostAccept();
}

void HandleAcceptUDPComplete(UDPListenSocket* s)
{
	//PRINTF_INFO("HandleAcceptUDPComplete fd = %d", s->socket_);

	uint8* buffer_start = (uint8*)(s->buff_);
	uint32 len = *((uint32*)(buffer_start));
	if (len == 8)
	{
		char src_conn_str[8] = "connect";
		char dst_conn_str[8];
		memcpy(dst_conn_str, (uint8*)(buffer_start + 4), 8);
		if (strcmp(src_conn_str, dst_conn_str) == 0)
		{
			// ģ��tcp��accept, ����һ���µ�һ��udp socket
			uint16 out_port = 0;
			bool ret = SocketMgr::get_instance()->AcceptUDP(s->client_addr_,
				s->onconnected_handler_,
				s->onclose_handler_,
				s->onrecv_handler_,
				s->sendbuffersize_,
				s->recvbuffersize_,
				out_port);

			// �ط�һ�����Ӱ���UDP�ͻ���, ���߿ͻ����µĶ˿ں�
			if (ret)
			{
				RepServerPort rep_msg;
				rep_msg.port = out_port;
				sendto(s->socket_, (char*)&rep_msg, sizeof(rep_msg), 0, (sockaddr*)&s->client_addr_, sizeof(s->client_addr_));
			}
		}
	}

	// ��Ͷ��һ��AcceptEx
	s->PostAccept();
}

void HandleConnectComplete(Socket* s, uint32 len, bool is_success)
{
	//PRINTF_INFO("HandleConnectComplete fd = %d, conn_idx = %d, status = %d, len = %d", s->GetFd(), s->GetConnectIdx(), s->status_, len);

	if (is_success)
	{
		s->status_ = socket_status_connectted;

		SocketMgr::get_instance()->AddSocket(s); //����socket�б�
		bool ret = s->SetupReadEvent();
		if (ret) //Ͷ��һ�����ɹ�
		{
			s->OnConnect(true);
		}
		else
		{
			SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket
			s->status_ = socket_status_closed;
			s->OnConnect(false);
			SocketMgr::get_instance()->RemoveSocket(s->GetConnectIdx());
		}
	}
	else
	{
		SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket
		s->status_ = socket_status_closed;
		s->OnConnect(false);
	}
}

void HandleReadComplete(Socket* s, uint32 len)
{
	//PRINTF_INFO("HandleReadComplete fd = %d, conn_idx = %d, status = %d, len = %d", s->GetFd(), s->GetConnectIdx(), s->status_, len);

	// �ͷ�����-1
	REF_RELEASE(s);

	if (s->status_ != socket_status_connectted && s->status_ != socket_status_closing)
	{
		return;
	}

	if (s->status_ == socket_status_connectted)
	{
		if (len)
		{
			s->GetReadBuffer().IncrementWritten(len);
			s->OnRead();

			bool ret = s->SetupReadEvent();
			if (!ret)
			{
				//PRINTF_ERROR("SetupReadEvent is fail, fd = %d, conn_idx = %d", s->GetFd(), s->GetConnectIdx());
				SocketMgr::get_instance()->CloseSocket(s);
			}
			else
			{
				//PRINTF_ERROR("SetupReadEvent is success, fd = %d, conn_idx = %d", s->GetFd(), s->GetConnectIdx());
			}
		}
		else
		{
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
					closesocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket

					//PRINTF_ERROR("closesocket fd = %d", fd);

					s->status_ = socket_status_closed;
					s->OnConnect(false);
				}
			}
		}
	}
	else if (s->status_ == socket_status_closing)
	{
		// galen: ���ʱ���Ѿ�������closesocket������, �п���socket��fd�Ѿ�������µ�������
		//        ���Է������������ĺ������Ͳ�Ӧ����Ͷ�ݽ�����, ��������fd��ͻ, OnRead�������ǿհ�
		SocketMgr::get_instance()->CloseSocket(s);
	}
}

void HandleWriteComplete(Socket* s, uint32 len)
{
	//PRINTF_INFO("HandleWriteComplete fd = %d, conn_idx = %d, status = %d", s->GetFd(), s->GetConnectIdx(), s->status_);

	// �ͷ�����-1
	REF_RELEASE(s);

	if (s->status_ != socket_status_connectted)
	{
		return;
	}
	else
	{
		s->GetWriteBuffer().Remove(len);
		s->WriteCallback();
	}
}

void HandleClose(Socket* s)
{
	//PRINTF_INFO("HandleClose fd = %d, conn_idx = %d, status = %d", s->GetFd(), s->GetConnectIdx(), s->status_);

	// �ͷ�����-1
	REF_RELEASE(s);

	if (s->status_ == socket_status_connectted)
	{
		s->status_ = socket_status_closing;
		SocketOps::CloseSocket(s->GetFd());
	}
}

void HandleDelaySend(Socket* s, uint32 len)
{
	//PRINTF_INFO("HandleDelaySend fd = %d, conn_idx = %d, status = %d", s->GetFd(), s->GetConnectIdx(), s->status_);
	 
	// �ͷ�����-1
	REF_RELEASE(s);

	if (s->status_ == socket_status_connectted)
	{
		s->BurstPush();
	}
}

void HandleShutdown()
{

}

void HandleWakeUp()
{

}

//-------------------------------------------------------------------------------

initialiseSingleton(SocketMgr);
SocketMgr::SocketMgr()
{
	auto_conn_idx_ = 1;
	completion_port_ = NULL;
}

SocketMgr::~SocketMgr()
{
	
}

uint32 SocketMgr::MakeGeneralConnID()
{
	uint32 conn_idx = INVALID_INDEX;
	while(true)
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

bool SocketMgr::Init()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	
	completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)0, 0);

	return true;
}

bool SocketMgr::Close()
{
	OverlappedStruct* ov = new OverlappedStruct(SOCKET_IO_THREAD_SHUTDOWN);
	PostQueuedCompletionStatus(completion_port_, 0, (ULONG_PTR)0, &ov->overlap_);

	return true;
}

void SocketMgr::Update(uint32 cur_time)
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

void SocketMgr::EventLoop(int32 timeout)
{
	uint32 start_time = getMSTime();
	Update(start_time);

	HANDLE cp = completion_port_;
	DWORD bytes_transferred;
	void* ptr = NULL;
	OverlappedStruct* ov;
	LPOVERLAPPED overlapped;
	
	while (true)
	{
		uint32 cur_time = getMSTime();

		uint32 timeout_time = 0;
		if (timeout - (int32)(cur_time - start_time) > 0)
		{
			timeout_time = timeout - (int32)(cur_time - start_time);
		}

		int ret = GetQueuedCompletionStatus(cp, &bytes_transferred, (LPDWORD)&ptr, &overlapped, timeout_time);
		if (ret)
		{
			//(1) �����������ɶ˿�ȡ��һ���ɹ�I/O��������ɰ�������ֵΪ��0��
			//    ������ָ��lpNumberOfBytesTransferred, lpCompletionKey, and lpOverlapped�Ĳ����д洢�����Ϣ��

			ov = CONTAINING_RECORD(overlapped, OverlappedStruct, overlap_);

			if (ptr == NULL)
			{
				continue;
			}

			if (ov->event_ == SOCKET_IO_EVENT_ACCEPT_TCP) // �����˿�Accept
			{
				TCPListenSocket* listen_socket = (TCPListenSocket*)ptr;
				HandleAcceptTCPComplete(listen_socket, ov->fd_);
			}
			else if (ov->event_ == SOCKET_IO_EVENT_ACCEPT_UDP) 
			{
				UDPListenSocket* listen_socket = (UDPListenSocket*)ptr;
				HandleAcceptUDPComplete(listen_socket);
			}
			else
			{
				Socket* s = (Socket*)ptr;
				REF_ADD(s);

				switch (ov->event_)
				{
				case SOCKET_IO_EVENT_CONNECT_COMPLETE:
					HandleConnectComplete(s, bytes_transferred, true);
					break;
				case SOCKET_IO_EVENT_READ_COMPLETE:
					HandleReadComplete(s, bytes_transferred);
					break;
				case SOCKET_IO_EVENT_WRITE_COMPLETE:
					HandleWriteComplete(s, bytes_transferred);
					break;
				case SOCKET_IO_EVENT_DELAY_SEND:
					HandleDelaySend(s, bytes_transferred);
					break;
				case SOCKET_IO_EVENT_CLOSE:
					HandleClose(s);
					break;
				default:
					break;
				}

				REF_RELEASE(s);
			}
		}
		else
		{
			ov = CONTAINING_RECORD(overlapped, OverlappedStruct, overlap_);

			if (ov == NULL) // ��(2)�����
			{
				//(2) ��� *lpOverlappedΪ�ղ��Һ���û�д���ɶ˿�ȡ����ɰ�������ֵ��Ϊ0��
				//    �����򲻻���lpNumberOfBytes and lpCompletionKey��ָ��Ĳ����д洢��Ϣ��
				//	  ����GetLastError���Եõ�һ����չ������Ϣ������������ڵȴ���ʱ��δ�ܳ�����ɰ���GetLastError����WAIT_TIMEOUT. 
				DWORD last_error = ::GetLastError();
				if (last_error == WAIT_TIMEOUT)
				{
					break;
				}
				else
				{
					break;
				}
			}
			else
			{
				if (ptr == NULL)
				{
					continue;
				}

				//---------------------------------------------------------------------------------
				if (bytes_transferred == 0) // ��(4)�����
				{
					//(4) ���������һ����ɶ˿ڵ�һ��socket������ر��ˣ���GetQueuedCompletionStatus����ERROR_SUCCESS,����lpNumberOfBytes����0
					if (ov->event_ == SOCKET_IO_EVENT_ACCEPT_TCP) // �����˿�Accept
					{
						
					}
					else if (ov->event_ == SOCKET_IO_EVENT_ACCEPT_UDP)
					{

					}
					else
					{
						Socket* s = (Socket*)ptr;
						REF_ADD(s);

						switch (ov->event_)
						{
						case SOCKET_IO_EVENT_CONNECT_COMPLETE:
							HandleConnectComplete(s, bytes_transferred, false);
							break;
						case SOCKET_IO_EVENT_READ_COMPLETE:
							HandleReadComplete(s, bytes_transferred);
							break;
						default:
							break;
						}
						REF_RELEASE(s);
					}
				}
				else // ��(3)�����
				{
					//(3) ��� *lpOverlapped��Ϊ�ղ��Һ�������ɶ˿ڳ���һ��ʧ��I/O��������ɰ�������ֵΪ0��
					//    ������ָ��lpNumberOfBytesTransferred, lpCompletionKey, and lpOverlapped�Ĳ���ָ���д洢�����Ϣ��
					//    ����GetLastError���Եõ���չ������Ϣ 
					if (overlapped)
					{
						break;
					}
				}
			}
		}
	}
}

void SocketMgr::WakeUp()
{
	OverlappedStruct* ov = new OverlappedStruct(SOCKET_IO_THREAD_WAKEUP);
	PostQueuedCompletionStatus(completion_port_, 0, (ULONG_PTR)0, &ov->overlap_);
}

HANDLE SocketMgr::GetCompletionPort()
{
	return completion_port_;
}

uint32 SocketMgr::Connect( const string& ip, uint16 port,
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
		AddSocket(s); //����socket�б�
		s->status_ = socket_status_connectted;

		bool ret = s->SetupReadEvent();
		if (ret) //Ͷ��һ�����ɹ�
		{
			s->OnConnect(true);
			return conn_idx;
		}
		else
		{
			SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket
			s->status_ = socket_status_closed;
			s->OnConnect(false);
			RemoveSocket(s->GetConnectIdx());
			return INVALID_INDEX;
		}
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
	Socket *s = new Socket(SOCKET_TYPE_TCP,
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
	
	if (!ret)
	{	
		delete s;
		s = NULL;
		return conn_idx;
	}
	else
	{
		return conn_idx;
	}
}

void SocketMgr::Accept(
	SOCKET aSocket, 
	sockaddr_in& address, 
	const HandleInfo onconnected_handler,
	const HandleInfo onclose_handler,
	const HandleInfo onrecv_handler,
	uint32 sendbuffersize, 
	uint32 recvbuffersize,
	bool is_parse_package)
{
	Socket *s = new Socket(SOCKET_TYPE_TCP,
		aSocket,
		MakeGeneralConnID(),
		onconnected_handler,
		onclose_handler,
		onrecv_handler,
		sendbuffersize, 
		recvbuffersize,
		is_parse_package);

	s->Accept(&address);

	AddSocket(s); //����socket�б�

	s->status_ = socket_status_connectted;
	bool ret = s->SetupReadEvent();
	if (ret) //Ͷ��һ�����ɹ�
	{	
		s->OnConnect(true);
	}
	else
	{
		SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket

		RemoveSocket(s->GetConnectIdx());
	}
}

bool SocketMgr::AcceptUDP(
	sockaddr_in& remote_address, 
	const HandleInfo onconnected_handler, 
	const HandleInfo onclose_handler, 
	const HandleInfo onrecv_handler, 
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

uint32 SocketMgr::ConnectUDP(
	const string& ip, 
	uint16 port, 
	uint16& local_port, 
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
		AddSocket(s); //����socket�б�
		s->status_ = socket_status_connectted;

		bool ret = s->SetupReadEvent();
		if (ret) //Ͷ��һ�����ɹ�
		{
			if (is_server_build) //����Ƿ���������������
			{
				s->is_udp_connected_ = true;
				s->OnConnect(true);
			}

			return conn_idx;
		}
		else
		{
			SocketOps::CloseSocket(s->GetFd()); //ֱ�ӵ��ùر�closesocket
			s->status_ = socket_status_closed;
			s->OnConnect(false);
			RemoveSocket(s->GetConnectIdx());
			return INVALID_INDEX;
		}
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
			//PRINTF_DEBUG("socket count = %d", socket_map_.size());
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
		REF_RELEASE(s);

		socket_map_.erase(it);
		//PRINTF_DEBUG("socket count = %d", socket_map_.size());
	}
}

void SocketMgr::CloseSocket(Socket* s)
{
	if (s->status_ == socket_status_closing || s->status_ == socket_status_connectted)
	{
		SocketStatus old_status = s->status_;

		s->status_ = socket_status_closed;
		s->OnDisconnect();

		SOCKET fd = s->GetFd();
		RemoveSocket(s->GetConnectIdx());

		if (old_status == socket_status_connectted)
		{
			SocketOps::CloseSocket(fd);
		}
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
	//PRINTF_INFO("SendMsg conn_idx = %d", conn_idx);

	Socket* s = NULL;
	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		s = it->second;

		REF_ADD(s);
	}
	
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

void SocketMgr::Disconnect(uint32 conn_idx)
{
	Socket* s = NULL;
	hash_map<uint32, Socket*>::iterator it = socket_map_.find(conn_idx);
	if (it != socket_map_.end())
	{
		s = it->second;
		
		REF_ADD(s);
	}
	
	if (s)
	{
		s->Disconnect();
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