/*
* 3D MMORPG Server
* Copyright (C) 2009-2014 RedLight Team
* author: galen
*/

#include "network.h"
#ifdef CONFIG_USE_EPOLL

bool Socket::PostEvent(uint32 events)
{
	int epoll_fd = SocketMgr::get_instance()->GetEpollFd();

	struct epoll_event ev;
	memset(&ev, 0, sizeof(epoll_event));
	ev.events = events | EPOLLET;			/* use edge-triggered instead of level-triggered because we're using nonblocking sockets */
	ev.data.ptr = this;

	// post actual event
	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd_, &ev))
	{
		PRINTF_ERROR("Could not post event on fd %u", fd_);
		return false;
	}
	else
	{
		return true;
	}
}

bool Socket::WriteCallback()
{
	//-------------------------------------------------------------------------------------------------------------
	// galen:����Ҫһ���Է���д��������ߵ�����, ���ʧ�ܾ͵�EPOLLIN���¼���, ���л�����
	//-------------------------------------------------------------------------------------------------------------
	while(writeBuffer.GetSize() > 0)
	{
		int send_len = writeBuffer.GetSize();
	
		if (send_len > 1000000) //1000K����
		{
			send_len = 1000000;
		}

		int bytes_written = send(fd_, writeBuffer.GetBufferStart(), send_len, 0);
		if (bytes_written < 0)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK) // ��socket�Ƿ�����ʱ,�緵�ش˴���,��ʾд�����������, �ȴ���һ��EPOLLOUT�¼��Ĵ���
			{
				//PRINTF_ERROR("conn_idx = %d, send fail, system socket buff is full, errno = %d" , GetConnectIdx(), errno)
				return false;
			}
			else
			{
				//PRINTF_ERROR("send fail errno = %d" , errno);
				return false;
			}
		}
		else
		{
			writeBuffer.Remove(bytes_written); //Remove������߻��Զ�FillVector()
			
			if (bytes_written < send_len)
			{
				if (errno == 0)
				{
					// ����
				}
				else if(errno == EAGAIN || errno == EWOULDBLOCK)
				{
					PRINTF_ERROR("conn_idx = %u, send not full bytes errno = %d" , conn_idx_, errno);
				}
				else 
				{
					//PRINTF_ERROR("send fail errno = %d" , errno);
				}

				return false;
			}
			else
			{
				// �����߼�, ��������
			}
		}
	}

	return true;
}

bool Socket::BurstPush()
{
	if (AcquireSendLock())
	{
		bool ret = PostEvent(EPOLLOUT);
		return ret;
	}
	else
	{
		return false;
	}
}

#endif
