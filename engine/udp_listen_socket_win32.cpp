#include "network.h"

#ifdef CONFIG_USE_IOCP
UDPListenSocket::UDPListenSocket(const char* listen_address, uint16 port, const HandleInfo onconnected_handler, const HandleInfo onclose_handler, const HandleInfo onrecv_handler, uint32 sendbuffersize, uint32 recvbuffersize)
{
	buff_len_ = 0;

	onconnected_handler_ = onconnected_handler;
	onclose_handler_ = onclose_handler;
	onrecv_handler_ = onrecv_handler;

	sendbuffersize_ = sendbuffersize;
	recvbuffersize_ = recvbuffersize;

	socket_ = WSASocket(AF_INET, SOCK_DGRAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
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
		assert(false);
		return;
	}

	//监听端口与完成端口绑定
	CreateIoCompletionPort((HANDLE)socket_, SocketMgr::get_instance()->GetCompletionPort(), (ULONG_PTR)this, 0);
}

UDPListenSocket::~UDPListenSocket()
{
	Close();
}

bool UDPListenSocket::Start()
{
	return PostAccept();
}

void UDPListenSocket::Close()
{
	SocketOps::CloseSocket(socket_);
}

bool UDPListenSocket::PostAccept()
{
	DWORD r_length = 0;
	DWORD flags = 0;
	WSABUF buf;
	buf.len = sizeof(buff_);
	buf.buf = buff_;

	int addr_len = sizeof(client_addr_);
	read_event_.SetEvent(SOCKET_IO_EVENT_ACCEPT_UDP);
	if (WSARecvFrom(socket_, &buf, 1, &r_length, &flags, (sockaddr*)&(client_addr_), &addr_len, &read_event_.overlap_, 0) == SOCKET_ERROR)
	{
		int last_error = WSAGetLastError();
		if (last_error != WSA_IO_PENDING)
		{
			return false;
		}
	}

	return true;
}

#endif