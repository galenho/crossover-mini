#include "scheduler.h"
#include "timer.h"
#include "console_poller.h"
#include "lua_wrapper.h"
#include "lua_fix.h"
#include "lua_bson.h"
#include "lua_pb.h"
#include "lua_seri.h"
#include "lua_filesystem.h"
#include "lua_cjson/lua_cjson.h"

std::string g_log_file;
int g_log_level = 0;

std::string g_lua_file;


CLog* g_logger;

bool running_ = false;

static const luaL_Reg lua_reg_libs[] = {
	{ "crossover",		luaopen_lua_crossover},
	{ "tcpserver",		luaopen_lua_tcpserver},
	{ "tcpclient",		luaopen_lua_tcpclient},
	{ "udpserver",		luaopen_lua_udpserver},
	{ "udpclient",		luaopen_lua_udpclient},
	{ "httpclient",		luaopen_lua_httpclient},
	{ "mysql",			luaopen_lua_mysql},
	{ "mongo",			luaopen_lua_mongo},

	{ "pb",				luaopen_lua_pb},
	{ "bson",			luaopen_lua_bson},
	{ "seri",			luaopen_lua_seri},
	{ "lfs",			luaopen_lua_lfs},
	{ "cjson",			luaopen_lua_cjson},
	{ NULL, NULL }
};

int PrintLuaStack(lua_State* L)
{
	lua_State* pLuaState = L;

	int stackTop = lua_gettop(pLuaState);//��ȡջ��������ֵ
	int nIdx = 0;
	int nType;
	std::string strErro = "";

	//������Ϣ
	for (nIdx = stackTop; nIdx > 0; --nIdx)
	{
		nType = lua_type(pLuaState, nIdx);
		strErro += lua_typename(pLuaState, nType);
		strErro += " ";
		strErro += lua_tostring(pLuaState, nIdx);
	}
	
	PRINTF_ERROR("Error: %s", strErro.c_str());

	lua_getglobal(pLuaState, "PrintTraceInfo");
	int iError = lua_pcall(pLuaState,    //VMachine    
		0,    //Argument Count    
		0,    //Return Value Count    
		0);

	g_logger->Save();

	return 0;
}

void Init()
{
	g_lua_state = luaL_newstate();

	luaL_openlibs(g_lua_state);

	// ��ȫ��ע����Ա��ں����Ļص�����
	toluafix_open(g_lua_state);

	::lua_pushcfunction(g_lua_state, PrintLuaStack);
	g_erro_func = lua_gettop(g_lua_state);

	//ע����luaʹ�õĿ�
	const luaL_Reg* lua_reg = lua_reg_libs;
	for (; lua_reg->func; ++lua_reg) 
	{
		luaL_requiref(g_lua_state, lua_reg->name, lua_reg->func, 1);
		lua_pop(g_lua_state, 1);
	}
	
	//���ؽű�
	int ret = luaL_loadfile(g_lua_state, g_lua_file.c_str());       //����main.lua
	if (ret != 0)
	{
		if (lua_isstring(g_lua_state, -1))
		{
			printf("load lua file error:%s\n", lua_tostring(g_lua_state, -1));
			return;
		}
	}

	//ִ�нű�
	ret = lua_pcall(g_lua_state, 0, 0, g_erro_func);
	if (ret != 0)
	{
		if (lua_isstring(g_lua_state, -1))
		{
			printf("load lua file error:%s\n", lua_tostring(g_lua_state, -1));
		}
	}
}

bool LoadXML()
{
	TiXmlDocument* doc = new TiXmlDocument();

	if (!doc->LoadFile("core.xml"))
	{
		delete doc;
		doc = NULL;
		return false;
	}

	TiXmlElement* root_emt = doc->RootElement();
	if (root_emt != NULL)
	{
		TiXmlElement* log_level_emt = root_emt->FirstChildElement("log_level");
		ASSERT(log_level_emt);
		g_log_level = atoi(log_level_emt->GetText());

		TiXmlElement* log_file_emt = root_emt->FirstChildElement("log_file");
		ASSERT(log_file_emt);
		g_log_file = log_file_emt->GetText();

		TiXmlElement* lua_file_emt = root_emt->FirstChildElement("lua_file");
		ASSERT(lua_file_emt);
		g_lua_file = lua_file_emt->GetText();
	}

	delete doc;
	doc = NULL;
	return true;
}

int main(int argc, char* argv[])
{
	bool ret = LoadXML();
	ASSERT(ret);

	if (argc == 2)
	{
		g_lua_file = argv[1];
	}

	srand((unsigned int)time(NULL));
	
	g_logger = new CLog(g_log_level);
	g_logger->set_log_name(g_log_file);
	g_logger->Start();

	Scheduler::get_instance()->set_log(g_logger);
	Scheduler::get_instance()->startup(bind(&Init));

	ConsolePoller::get_instance()->Activate();

	running_ = true;

	while (running_)
	{
		SleepMs(200);
	}

	ConsolePoller::get_instance()->Stop();

	Scheduler::get_instance()->shutdown();
	
	while (Scheduler::get_instance()->get_thread_count() > 0)
	{
		SleepMs(200); //�ȴ��������̵߳��˳������߳�����˳�
		//printf("get_thread_count() = %d\n", Scheduler::get_instance()->get_thread_count());
		Scheduler::get_instance()->PrintThreadInfos();
	}

	Scheduler::get_instance()->CleanupNetwork();

	LOG_INFO(g_logger, "exit success!");
	g_logger->Save();
	g_logger->StopWaitExit();
	delete g_logger;

	return 0;
}