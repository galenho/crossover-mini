#include "network.h"

#ifdef CONFIG_USE_EPOLL
UDPListenSocket::UDPListenSocket(const char* listen_address, uint16 port, const HandleInfo onconnected_handler, const HandleInfo onclose_handler, const HandleInfo onrecv_handler, uint32 sendbuffersize, uint32 recvbuffersize)
{
	socket_type_ = SOCKET_TYPE_UDP;
	is_listen_ = true;

	onconnected_handler_ = onconnected_handler;
	onclose_handler_ = onclose_handler;
	onrecv_handler_ = onrecv_handler;

	sendbuffersize_ = sendbuffersize;
	recvbuffersize_ = recvbuffersize;

	socket_ = socket(AF_INET, SOCK_DGRAM, 0);

	SocketOps::ReuseAddr(socket_);
	SocketOps::Blocking(socket_);

	address_.sin_family = AF_INET;
	address_.sin_port = ntohs((u_short)port);
	address_.sin_addr.s_addr = htonl(INADDR_ANY);

	struct hostent* hostname = gethostbyname(listen_address);
	if (hostname != 0)
	{
		memcpy(&address_.sin_addr.s_addr, hostname->h_addr_list[0], hostname->h_length);
	}

	int ret = ::bind(socket_, (const sockaddr*)&address_, sizeof(address_));
	if (ret != 0)
	{
		PRINTF_ERROR("Bind unsuccessful on port %u.", (unsigned int)port);
		assert(false);
		return;
	}

	//监听端口与完成端口绑定
	struct epoll_event ev;
	memset(&ev, 0, sizeof(epoll_event));
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = this;

	int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_, &ev))
	{
		PRINTF_ERROR("epoll", "Could not add event to epoll set on fd %u ", socket_);
		ASSERT(false);
	}
}

UDPListenSocket::~UDPListenSocket()
{
	Close();
}

void UDPListenSocket::Close()
{
	SocketOps::CloseSocket(socket_);
}

bool UDPListenSocket::Start()
{
	return PostAccept();
}

bool UDPListenSocket::PostAccept()
{
	return true;
}

int UDPListenSocket::GetFd()
{
	return socket_;
}
#endif