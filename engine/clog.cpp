#include "clog.h"
#include "guard.h"

CLog::CLog( int32 log_level /*= LOG_LEVEL_INFO*/, int32 buff_size /*= 1024 * 1024*/ , int32 write_buff_size /*= 1024*/ )
{
	log_level_ = log_level;

#ifdef WIN32
	stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
	stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

	ASSERT(buff_size > 20);
	ASSERT(write_buff_size < buff_size);

	buff_size_ = buff_size;
	write_buff_size_ = write_buff_size;

	write_buff_size_ = 100;

	buffer_ = new char[buff_size_];
	memset(buffer_, 0, buff_size_);

	is_running_ = false;
	is_thread_exit_ = false;

	set_name("log thread");
}

CLog::~CLog()
{
	if (buffer_)
	{
		delete[] buffer_;
		buffer_ = NULL;
	}
}

void CLog::set_log_name( string file_name )
{
	file_name_ = file_name;
}

void CLog::set_log_level( int32 log_level )
{
	Guard guard(mutex);

	log_level_ = log_level;
}

void CLog::Color( unsigned int color )
{
#ifndef WIN32
	static const char* colorstrings[TBLUE + 1] =
	{
		"", "\033[22;31m", "\033[22;32m", "\033[01;33m",
		"\033[0m", "\033[01;37m", "\033[1;34m",
	};
	fputs(colorstrings[color], stdout);
#else
	SetConsoleTextAttribute(stdout_handle, (WORD)color);
#endif
}

void CLog::Start()
{
	fout.open(file_name_.c_str(), ios::app);//���ļ�

	Activate();
}

bool CLog::Run()
{
	LogData log_data;
	int32 buff_len = 0;
	bool is_save_soon = false;

	while(is_running_)
	{
		event_.Wait();

		while (log_list_.pop(log_data))
		{
			if (log_data.op_type == 1)
			{
				if (buff_len + log_data.data_len > write_buff_size_)
				{
					fout<<flush;	//��д����������д���������ڱ����ļ��򿪵�����±����ļ���
					buff_len = 0;
				}

				fout<<log_data.data; //д���ļ�
				buff_len += log_data.data_len;

				if (log_data.data) //ɾ������
				{
					delete[] log_data.data;
					log_data.data = NULL;
				}
			}
			else if (log_data.op_type == 2)
			{
				is_save_soon = true;
			}
		}

		if (is_save_soon)
		{
			// ���ʣ�����־
			if (buff_len > 0)
			{
				fout<<flush;	//��д����������д���������ڱ����ļ��򿪵�����±����ļ���
			}
			is_save_soon = false;
		}
	}

	// ���ʣ�����־
	if (buff_len > 0)
	{
		fout<<flush;	//��д����������д���������ڱ����ļ��򿪵�����±����ļ���
	}
	fout.close();//�ر�

	is_thread_exit_ = true;

	return true;
}

void CLog::StopWaitExit()
{
	is_running_ = false;

	Guard guard(mutex);
	while (!is_thread_exit_) //ֱ���߳��˳�
	{
		SleepMs(200);
	}
}

void CLog::WriteLogFile( int32 log_level, const char* fmt, ... )
{
	Guard guard(mutex);

	if (log_level < log_level_)
		return;

	int length = 0;

	// (1)д������ͷ
	time_t timep;
	time (&timep);
	tm *localTime = localtime(&timep);

	sprintf(buffer_, "%04u-%02u-%02u %02u:%02u:%02u ", 
		localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, 
		localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

	length += 20;

	// (2)д����־��
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf (buffer_ + length, buff_size_ - length - 2, fmt, ap);
	va_end(ap);

	if (len <= 0) //��־̫����
	{
		printf("error: log is to long\n");
		return;
	}

	length += len;

	// (3)д��"\n"
	buffer_[length] = '\n';
	length++;

	switch (log_level)
	{
	case LOG_LEVEL_DEBUG:
		Color(TNORMAL);
		break;
	case LOG_LEVEL_INFO:
		Color(TBLUE);
		break;
	case LOG_LEVEL_WARN:
		Color(TYELLOW);
		break;
	case LOG_LEVEL_ERROR:
		Color(TRED);
		break;
	default:
		break;
	}

	//---------------------------------------------------------------------
	// 1. �������Ļ
	//---------------------------------------------------------------------
	buffer_[length] = '\0';
	length++;
	printf("%s", buffer_);

	Color(TNORMAL); //�ָ�һ��color

	//---------------------------------------------------------------------
	// 2. ������ļ�
	//---------------------------------------------------------------------
	LogData log_data;
	log_data.op_type = 1;
	log_data.data_len = length;
	log_data.data = new char[length];
	memcpy (log_data.data, buffer_, length);
	log_list_.push_back(log_data);

	// �����߳�
	WakeUp();
}

void CLog::WakeUp()
{
	event_.Notify();
}

void CLog::Save()
{
	LogData log_data;
	log_data.op_type = 2;
	log_list_.push_back(log_data);

	// �����߳�
	WakeUp();
}
