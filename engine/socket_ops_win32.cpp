/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#include "network.h"
#ifdef CONFIG_USE_IOCP

namespace SocketOps
{
	// Create file descriptor for socket i/o operations.
	SOCKET CreateTCPFileDescriptor()
	{
		// create a socket for use with overlapped i/o.
		return ::WSASocket(AF_INET, SOCK_STREAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
	}

	// Create file descriptor for socket i/o operations.
	SOCKET CreateUDPFileDescriptor()
	{
		// create a socket for use with overlapped i/o.
		return ::WSASocket(AF_INET, SOCK_DGRAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
	}

	// Disable blocking send/recv calls.
	bool Nonblocking(SOCKET fd)
	{
		u_long arg = 1;
		return (::ioctlsocket(fd, FIONBIO, &arg) == 0);
	}

	// Disable blocking send/recv calls.
	bool Blocking(SOCKET fd)
	{
		u_long arg = 0;
		return (ioctlsocket(fd, FIONBIO, &arg) == 0);
	}

	// Disable nagle buffering algorithm
	bool DisableBuffering(SOCKET fd)
	{
		uint32 arg = 1;
		return (setsockopt(fd, 0x6, TCP_NODELAY, (const char*)&arg, sizeof(arg)) == 0);
	}

	// Enable nagle buffering algorithm
	bool EnableBuffering(SOCKET fd)
	{
		uint32 arg = 0;
		return (setsockopt(fd, 0x6, TCP_NODELAY, (const char*)&arg, sizeof(arg)) == 0);
	}

	// Set internal buffer size to socket.
	bool SetSendBufferSize(SOCKET fd, uint32 size)
	{
		return (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&size, sizeof(size)) == 0);
	}

	// Set internal buffer size to socket.
	bool SetRecvBufferSize(SOCKET fd, uint32 size)
	{
		return (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&size, sizeof(size)) == 0);
	}

	// Closes a socket fully.
	void CloseSocket(SOCKET fd)
	{
		//shutdown(fd, SD_BOTH);

		/*
			SO_LINGER
			��ѡ��ָ������close���������ӵ�Э����β�������TCP����ȱʡclose�������������أ���������ݲ������׽ӿڻ���������ϵͳ�����Ž���Щ���ݷ��͸��Է���
			SO_LINGERѡ�������ı��ȱʡ���á�ʹ�����½ṹ��
			struct linger 
			{
				int l_onoff; // 0 = off, nozero = on 
				int l_linger; // linger time 
			};
			���������������
			l_onoffΪ0�����ѡ��رգ�l_linger��ֵ�����ԣ�����ȱʡ�����close�������أ� 
			l_onoffΪ��0��l_lingerΪ0�����׽ӿڹر�ʱTCPز�����ӣ�TCP�������������׽ӿڷ��ͻ������е��κ����ݲ�����һ��RST���Է���������ͨ�����ķ�����ֹ���У��������TIME_WAIT״̬�� 
			l_onoff Ϊ��0��l_lingerΪ��0�����׽ӿڹر�ʱ�ں˽�����һ��ʱ�䣨��l_linger������������׽ӿڻ��������Բ������ݣ����̽�����˯��״̬��ֱ ����a���������ݷ������ұ��Է�ȷ�ϣ�֮�������������ֹ���У������ַ��ʼ���Ϊ0����b���ӳ�ʱ�䵽����������£�Ӧ�ó�����close�ķ���ֵ�Ƿǳ���Ҫ�ģ���������ݷ����겢��ȷ��ǰʱ�䵽��close������EWOULDBLOCK�������׽ӿڷ��ͻ������е��κ����ݶ���ʧ��close�ĳɹ����ؽ��������Ƿ��͵����ݣ���FIN�����ɶԷ�TCPȷ�ϣ��������ܸ������ǶԷ�Ӧ�ý����Ƿ��Ѷ������ݡ�����׽ӿ���Ϊ�������ģ��������ȴ�close��ɡ� 
			l_linger�ĵ�λ������ʵ�֣�4.4BSD�����䵥λ��ʱ�ӵδ𣨰ٷ�֮һ�룩����Posix.1g�涨��λΪ�롣
		*/

		int retv;
		LINGER LingerStruct;
		LingerStruct.l_onoff = 1;
		LingerStruct.l_linger = 0;

		retv = setsockopt(fd, SOL_SOCKET, SO_LINGER, (char*)&LingerStruct, sizeof(LingerStruct));
		if (retv == SOCKET_ERROR)
		{
			//PRINTF_ERROR("setsockopt Error:%d", WSAGetLastError());
		}

		retv = closesocket(fd);
		//PRINTF_ERROR("closesocket fd = %d", fd);

		if (retv == SOCKET_ERROR)
		{
			//PRINTF_ERROR("closesocket Error:%d", WSAGetLastError());
		}
	}

	// Sets reuseaddr
	void ReuseAddr(SOCKET fd)
	{
		uint32 option = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, 4);
	}
}

#endif
