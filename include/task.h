/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*/

#ifndef _TASK_H_
#define _TASK_H_

#include "mutex.h"
#include "database_defines.h"
#include "mongo_task.h"

class TimerTask
{
public:
	static void process(HandleInfo handle, uint32 index);
	static void DeleteTimer(HandleInfo handle, uint32 index);
};

class SocketConnectTask
{
public:
	static void process(HandleInfo connect_handle, uint32 conn_idx, bool is_success);
};

class SocketReadTask
{
public:
	static void process(HandleInfo recv_handle, uint32 conn_idx, char* data, uint32 data_len);
};

class SocketCloseTask
{
public:
	static void process(HandleInfo close_handle, uint32 conn_idx);
};

class SocketClientDeleteTask
{
public:
	static void process(HandleInfo connect_handle, HandleInfo recv_handle, HandleInfo close_handle);
};
//--------------------------------------------------------------
class Task
{
public:
	Task();
	virtual ~Task();

public:
	virtual void process() = 0;
};

class InputTask : public Task
{
public:
	InputTask();
	virtual ~InputTask();

	void Init(HandleInfo handle, string& cmd);

	virtual void process();

public:
	HandleInfo handle_;
	string cmd_;
};
//--------------------------------------------------------------

class DBTask : public Task
{
public:
	DBTask();
	virtual ~DBTask();

	void Init(HandleInfo handle, ResultSet* rs);

	virtual void process();

public:
	HandleInfo handle_;
	ResultSet* rs_;
};
//--------------------------------------------------------------

class MongoDBTask : public Task
{
public:
	MongoDBTask();
	virtual ~MongoDBTask();

	void Init(HandleInfo handle, MongoResultSet* rs, bool is_one_table = false);

	virtual void process();

public:
	HandleInfo handle_;
	MongoResultSet* rs_;
	bool is_one_table_;
};

//--------------------------------------------------------------

class HttpTask : public Task
{
public:
	HttpTask();
	virtual ~HttpTask();

	void Init(HandleInfo handle, bool is_success, string& recv_buff, uint32 use_time);
	virtual void process();

public:
	HandleInfo handle_;
	bool is_success_;
	string recv_buff_;
	uint32 use_time_;
};

#endif //_TASK_H_