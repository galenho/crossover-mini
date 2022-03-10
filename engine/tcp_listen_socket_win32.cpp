#include "network.h"

TCPListenSocket::TCPListenSocket(
	const char* listen_address, 
	uint16 port, 
	const HandleInfo onconnected_handler, 
	const HandleInfo onclose_handler, 
	const HandleInfo onrecv_handler, 
	uint32 sendbuffersize, 
	uint32 recvbuffersize, 
	bool is_parse_package)
{
	buff_len_ = 0;
	accept_ex_ = NULL;
	accept_addrs_ = NULL;

	port_ = port;
	onconnected_handler_ = onconnected_handler;
	onclose_handler_ = onclose_handler;
	onrecv_handler_ = onrecv_handler;

	sendbuffersize_ = sendbuffersize;
	recvbuffersize_ = recvbuffersize;

	is_parse_package_ = is_parse_package;

	socket_ = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SocketOps::ReuseAddr(socket_);
	SocketOps::Blocking(socket_);

	address_.sin_family = AF_INET;
	address_.sin_port = ntohs((u_short)port);
	address_.sin_addr.s_addr = htonl(INADDR_ANY);

	struct hostent* hostname = gethostbyname(listen_address);
	if (hostname != 0)
		memcpy(&address_.sin_addr.s_addr, hostname->h_addr_list[0], hostname->h_length);

	int ret = ::bind(socket_, (const sockaddr*)&address_, sizeof(address_));
	if (ret != 0)
	{
		PRINTF_ERROR("Bind unsuccessful on port %u.", port);
		ASSERT(false);
		return;
	}

	//监听端口与完成端口绑定
	CreateIoCompletionPort((HANDLE)socket_, SocketMgr::get_instance()->GetCompletionPort(), (ULONG_PTR)this, 0);
}

TCPListenSocket::~TCPListenSocket()
{
	Close();
}

bool TCPListenSocket::Start()
{
	bool ret = listen(socket_, 5);
	if (ret != 0)
	{
		PRINTF_ERROR("Unable to listen on port %u.", port_);
		return false;
	}

	accept_ex_ = get_accept_ex(socket_);
	accept_addrs_ = get_accept_addr(socket_);

	// 先投递一个请求
	ret = PostAccept();

	return ret;
}

void TCPListenSocket::Close()
{
	SocketOps::CloseSocket(socket_);
}

bool TCPListenSocket::PostAccept()
{
	// 投递一个accept请求
	SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	int addr_len = sizeof(sockaddr_in) + 16;
	accept_event_.SetEvent(SOCKET_IO_EVENT_ACCEPT);
	accept_event_.fd = client_socket;

	bool ret = accept_ex_(socket_, client_socket, buff_, 0, addr_len, addr_len, &buff_len_, &accept_event_.overlap_);
	if (!ret)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			PRINTF_ERROR("PostAccept error = %d", WSAGetLastError());
			return false;
		}
	}

	return true;
}