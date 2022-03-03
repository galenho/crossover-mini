/*
** CrossOver
** Copyright (C) 2009-2021 RedLight Team
** author: galen
**     qq: 88104725
*/

#ifndef _DATABASE_H_
#define _DATABASE_H_

#include "database_defines.h"

class Database_impl;
class Database
{
public:
	Database();
	~Database();

	bool Initialize(const string hostname, 
					uint16 port, 
					const string username, 
					const string password,
					const string database,
					const string charset_name,
					uint32 conn_count);

	void Shutdown();

	// �õ���һ������, һ����δָ��ִ�������̵߳�sql����õ�
	uint32 get_next_connect();

	// һ���Ǹ�memcache����, ��Ϊmemcache��new��delete
	uint32 get_free_connect();
	// ����ĳ�����̵߳����ü���
	void add_connect_ref(uint32 conn_idx);
	// �Ƴ�ĳ�����̵߳����ü���
	void remove_connect_ref(uint32 conn_idx);

	Statement* CreateStatement(uint32 conn_idx);

	// sqlִ�нӿ�(�������)
	void AddQuery(Statement* stmt, HandleInfo handler, DBTransactionFlag flag = DB_TRANSACTION_NONE);

	// sqlִ�нӿ�(������ǰ)
	void InsertFristQuery(Statement* stmt, HandleInfo handler);

	void Ping();

private:
	Database_impl *imp_;
};

#endif //_DATABASE_H_