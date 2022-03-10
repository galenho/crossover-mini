#include "common.h"
#include "referable.h"
#include "scheduler.h"

Referable::Referable()
{
	count_ = 0;
}

Referable::~Referable()
{
	
}

void Referable::AddRef()
{
#ifdef WIN32
	InterlockedIncrement(&count_);
#else
	__sync_add_and_fetch(&count_, 1);
#endif

	printf("\n");
	PRINTF_INFO("AddRef----%d", count_);
}

bool Referable::Release()
{
#ifdef WIN32
	InterlockedDecrement(&count_);
#else
	__sync_sub_and_fetch(&count_, 1);
#endif
	printf("\n");
	PRINTF_INFO("ReleaseRef----%d", count_);
	Scheduler::get_instance()->get_logger()->Save();

	if (count_ == 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}
