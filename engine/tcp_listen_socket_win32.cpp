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
	bool ret = listen(fd_, 5);
	if (ret != 0)
	{
		PRINTF_ERROR("Unable to listen on port %u.", port_);
		return false;
	}

	/*
	SocketMgr::get_instance()->Accept(aSocket,
		tempAddress,
		onconnected_handler_,
		onclose_handler_,
		onrecv_handler_,
		sendbuffersize_,
		recvbuffersize_,
		is_parse_package_);
	*/

	SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	int addr_len = sizeof(sockaddr_in) + 16;
	accept_event_.SetEvent(SOCKET_IO_EVENT_ACCEPT);
	connect_ex_fn accept_ex = get_accept_ex(fd_);
	bool ret = accept_ex(fd_, client_socket, buff_, 0, addr_len, addr_len, &buff_len_, &accept_event_.overlap_);
	if (!ret)
	{

	}

	/*
	bool ret = AcceptEx(fd_, client_socket, buff_, 0, addr_len, addr_len, &buff_len_, &accept_event_.overlap_);
	if (false == ret && ERROR_IO_PENDING != GetLastError())
	{
		PRINTF_ERROR("AcceptEx Failed : %d", GetLastError());
		return false;
	}
	*/

	return true;
}

void TCPListenSocket::Close()
{
	SocketOps::CloseSocket(socket_);
}
