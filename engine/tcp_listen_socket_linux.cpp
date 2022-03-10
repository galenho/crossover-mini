#include "network.h"

#ifdef CONFIG_USE_EPOLL
TCPListenSocket::TCPListenSocket(const char* listen_address, uint16 port, const HandleInfo onconnected_handler, const HandleInfo onclose_handler, const HandleInfo onrecv_handler, uint32 sendbuffersize, uint32 recvbuffersize, bool is_parse_package /*= true*/)
{
	port_ = port;
	onconnected_handler_ = onconnected_handler;
	onclose_handler_ = onclose_handler;
	onrecv_handler_ = onrecv_handler;

	sendbuffersize_ = sendbuffersize;
	recvbuffersize_ = recvbuffersize;

	is_parse_package_ = is_parse_package;

	socket_ = socket(AF_INET, SOCK_STREAM, 0);

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
		ASSERT(false);
		return;
	}

	//监听端口与完成端口绑定
	struct epoll_event ev;
	memset(&ev, 0, sizeof(epoll_event));
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = this;
	ev.data.fd = 0;

	int epoll_fd = SocketMgr::get_instance()->GetEpollFd();
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_, &ev))
	{
		PRINTF_ERROR("epoll", "Could not add event to epoll set on fd %u ", socket_);
		ASSERT(false);
	}
}

TCPListenSocket::~TCPListenSocket()
{
	Close();
}

void TCPListenSocket::Close()
{
	SocketOps::CloseSocket(socket_);
}

bool TCPListenSocket::Start()
{
	bool ret = listen(socket_, 5);
	if (ret != 0)
	{
		PRINTF_ERROR("Unable to listen on port %u.", port_);
		return false;
	}

	// 先投递一个请求
	ret = PostAccept();

	return ret;
}

bool TCPListenSocket::PostAccept()
{
	int epoll_fd = SocketMgr::get_instance()->GetEpollFd();

	struct epoll_event ev;
	memset(&ev, 0, sizeof(epoll_event));
	ev.events = EPOLLIN | EPOLLET;
	ev.data.ptr = this;
	ev.data.fd = 0;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, socket_, &ev))
	{
		PRINTF_ERROR("Could not post accept event on fd %u", socket_);
		return false;
	}
	else
	{
		return true;
	}
}

int TCPListenSocket::GetFd()
{
	return socket_;
}
#endif