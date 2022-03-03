/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*/

#ifndef _MONGO_DATABASE_H_
#define _MONGO_DATABASE_H_

#include "common.h"
#include "mongo_task.h"

class MongoDatabase_impl;
class MongoDatabase
{
public:
	MongoDatabase();
	~MongoDatabase();

	bool Initialize(const string uri, const string db_name, uint32 conn_count);

	void Shutdown();

	void PushTask(MongoTask* task, uint32 conn_idx);

	// �õ���һ������, һ����δָ��ִ�������̵߳�sql����õ�
	uint32 get_next_connect();

	// һ���Ǹ�memcache����, ��Ϊmemcache��new��delete
	uint32 get_free_connect();
	// ����ĳ�����̵߳����ü���
	void add_connect_ref(uint32 conn_idx);
	// �Ƴ�ĳ�����̵߳����ü���
	void remove_connect_ref(uint32 conn_idx);

	void Ping();

private:
	MongoDatabase_impl *imp_;
};

#endif //_MONGO_DATABASE_H_