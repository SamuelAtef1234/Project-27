/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

/************************************************************************************/
//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//TODO: [PROJECT'26.GM#1] DYNAMIC ALLOCATOR - [0] GIVEN FUNCTIONS
//==================================
// [1] INITIALIZE BUDDY FREE LISTS:
//==================================
void buddy_init()
{
	for (int l = BUDDY_LOWER_LEVEL; l <= BUDDY_UPPER_LEVEL; ++l)
	{
		LIST_INIT(&(freeBlockLists[l]));//y5ly list ellevel fadya
	}
}
//====================================
// [2] PRINT SIZE OF FREE LISTS:
//====================================
void PrintFreeBlockLists()
{
	for (int i = BUDDY_LOWER_LEVEL; i <= BUDDY_UPPER_LEVEL; ++i)
	{
		cprintf("Level#%d: %d\n", i, LIST_SIZE(&freeBlockLists[i]));//y3rf 3dd elblocks elfadya felevel
	}
}
/************************************************************************************/

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===========================
// [1] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'26.GM#1] DYNAMIC ALLOCATOR - [1] get_block_size
	if(va == NULL) // 3l4an lo al Virtual Address = NULL
		return 0;
	else{
		struct BlockMetaData *bmData = (struct BlockMetaData *)((uint32)va - META_DATA_SIZE);//bt3rf ellevel
		return BLOCK_SIZE(bmData->level);//t7wl l byte size
	}
}

//===========================
// [2] ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 requiredSize)
{
	//TODO: [PROJECT'26.GM#1] DYNAMIC ALLOCATOR - [2] alloc_block
	if (requiredSize == 0 || requiredSize > DYN_ALLOC_MAX_BLOCK_SIZE - META_DATA_SIZE)
		return NULL;
	uint32 tSize = requiredSize + META_DATA_SIZE;//fe 7agm sabt l metadatasize lazm ta5odha
	int level = BUDDY_LOWER_LEVEL;
	while (BLOCK_SIZE(level) < tSize)//l8ayt mawsal elmsa7a elkafya
		level++;
	int curLvl = level;
	while (curLvl <= BUDDY_UPPER_LEVEL && LIST_EMPTY(&freeBlockLists[curLvl]))
		curLvl++;
	struct BlockMetaData *bmData;
	if (curLvl > BUDDY_UPPER_LEVEL){
		void *page = create_buddy_space();//y3ml page gdeda law mafesh pages
		if (page == NULL)
			return NULL;
		bmData = (struct BlockMetaData *)page;
		curLvl = BUDDY_UPPER_LEVEL;
	}else{
		bmData = LIST_FIRST(&freeBlockLists[curLvl]);
		LIST_REMOVE(&freeBlockLists[curLvl], bmData);
	}
	while (curLvl > level)
	{
		curLvl--;//by2smha nosen 2d ba3d
		struct BlockMetaData *buddy = (struct BlockMetaData *)((uint8 *)bmData + BLOCK_SIZE(curLvl));
		buddy->level = curLvl;
		buddy->is_free = 1;
		LIST_INSERT_HEAD(&freeBlockLists[curLvl], buddy);
	}
	bmData->level = level;
	bmData->is_free = 0;//b2a taken
	return (void *)((uint32)bmData + META_DATA_SIZE);
}

//===================================================
// [3] FREE BLOCK WITH COALESCING:
//===================================================
void free_block(void *va)
{
	//TODO: [PROJECT'26.GM#1] DYNAMIC ALLOCATOR - [3] free_block
	if(va == NULL)
		return;
	struct BlockMetaData *bmData = (struct BlockMetaData *)((uint8 *)va - META_DATA_SIZE);
	if(bmData-> is_free)
		return;
	int lev = bmData->level;
	while(lev < BUDDY_UPPER_LEVEL){
		struct BlockMetaData *bmd = (struct BlockMetaData *)((uint32)bmData ^ BLOCK_SIZE(lev));
		if(!bmd->is_free||bmd->level!=lev)
			break;
		LIST_REMOVE(&freeBlockLists[lev], bmd);
		if(bmd<bmData)
			bmData = bmd;
		lev++;
	}
	bmData->level = lev;
	bmData->is_free = 1;
	if (lev >= BUDDY_UPPER_LEVEL){
		delete_buddy_space((void *)bmData);
	    return;
	}
	LIST_INSERT_HEAD(&freeBlockLists[lev], bmData);
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 newSize)
{
	//TODO: [PROJECT'26.BONUS#1] KERNEL REALLOC - realloc_block
	//Your code is here
	//Comment the following line
	panic("realloc_block() Not implemented yet");
}
