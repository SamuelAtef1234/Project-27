// Sleeping locks

#include "inc/types.h"
#include "inc/x86.h"
#include "inc/memlayout.h"
#include "inc/mmu.h"
#include "inc/environment_definitions.h"
#include "inc/assert.h"
#include "inc/string.h"
#include "sleeplock.h"
#include "channel.h"
#include "../cpu/cpu.h"
#include "../proc/user_environment.h"

/************************************************************************************/
//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//TODO: [PROJECT'26.GM#4] BLOCKING - [0] GIVEN FUNCTIONS

void init_sleeplock(struct sleeplock *lk, char *name)
{
	init_channel(&(lk->chan), "sleep lock channel");
	char prefix[30] = "lock of sleeplock - ";
	char guardName[30+NAMELEN];
	strcconcat(prefix, name, guardName);
	init_kspinlock(&(lk->lk), guardName);
	strcpy(lk->name, name);
	lk->locked = 0;
	lk->pid = 0;
}

int holding_sleeplock(struct sleeplock *lk)
{
	int r;
	acquire_kspinlock(&(lk->lk));
	r = lk->locked && (lk->pid == get_cpu_proc()->env_id);
	release_kspinlock(&(lk->lk));
	return r;
}
/************************************************************************************/

//=====================================================================================//
//============================== REQUIRED FUNCTIONS ===================================//
//=====================================================================================//
void acquire_sleeplock(struct sleeplock *lk)
{
    acquire_kspinlock(&(lk->lk));

    //while(){}

    for (; lk->locked; )//for msh if 3shan lma el process tes7a terg3 t check tany
    {
        sleep(&(lk->chan), &(lk->lk));
    }

    lk->locked = 1;
    lk->pid = get_cpu_proc()->env_id;

    release_kspinlock(&(lk->lk));
}

void release_sleeplock(struct sleeplock *lk)
{
	//TODO: [PROJECT'26.GM#4] BLOCKING - [17] SleepLock release_sleeplock
	//Your code is here
	//Comment the following line
	//panic("release_sleeplock() is not implemented yet...!!");
	acquire_kspinlock(&(lk->lk));
	{
		lk->locked = 0;
		lk->pid = 0;
		wakeup_one(&(lk->chan));
	}
	release_kspinlock(&(lk->lk));
}




