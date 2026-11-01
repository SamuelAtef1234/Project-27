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
	if (requiredSize == 0)
			return NULL;
	if (requiredSize > DYN_ALLOC_MAX_BLOCK_SIZE - META_DATA_SIZE)
		panic("alloc_block: requested size is larger than the maximum block size");
	uint32 tSize = requiredSize + META_DATA_SIZE;//fe 7agm sabt l metadatasize lazm ta5odha
	int lvl = BUDDY_LOWER_LEVEL;
	while (BLOCK_SIZE(lvl) < tSize)//l8ayt mawsal elmsa7a elkafya
		lvl++;
	if (lvl > BUDDY_UPPER_LEVEL)
		return NULL;
	int curLvl = lvl;
	while (curLvl <= BUDDY_UPPER_LEVEL && LIST_EMPTY(&freeBlockLists[curLvl]))
		curLvl++;
	struct BlockMetaData *bmData;
	if (curLvl > BUDDY_UPPER_LEVEL)
	{
		void *page = create_buddy_space();//y3ml page gdeda law mafesh pages
		if (page == NULL)
			return NULL;
		bmData = (struct BlockMetaData *)page;
		curLvl = BUDDY_UPPER_LEVEL;
		for (uint32 off = BLOCK_SIZE(BUDDY_UPPER_LEVEL); off < PAGE_SIZE; off += BLOCK_SIZE(BUDDY_UPPER_LEVEL))
		{
			struct BlockMetaData *other = (struct BlockMetaData *)((uint32)page + off);
			other->level = BUDDY_UPPER_LEVEL;
			other->is_free = 1;
			LIST_INSERT_HEAD(&freeBlockLists[BUDDY_UPPER_LEVEL], other);
		}
	}
	else
	{
		bmData = LIST_FIRST(&freeBlockLists[curLvl]);
		LIST_REMOVE(&freeBlockLists[curLvl], bmData);
	}
	while (curLvl > lvl)
	{
		curLvl--;//by2smha nosen 2d ba3d
		struct BlockMetaData *buddy = (struct BlockMetaData *)((uint32)bmData + BLOCK_SIZE(curLvl));
		buddy->level = curLvl;
		buddy->is_free = 1;
		LIST_INSERT_HEAD(&freeBlockLists[curLvl], buddy);
	}
	bmData->level = lvl;
	bmData->is_free = 0;//b2a taken
	return (void *)((uint32)bmData + META_DATA_SIZE);
}

//===================================================
// [3] FREE BLOCK WITH COALESCING:
//===================================================
void free_block(void *va)
{
	//TODO: [PROJECT'26.GM#1] DYNAMIC ALLOCATOR - [3] free_block
	if (va == NULL)
		return;
	struct BlockMetaData *bmData = (struct BlockMetaData *)((uint32)va - META_DATA_SIZE);
	if (bmData->is_free)
		return;
	int lvl = bmData->level;
	while (lvl < BUDDY_UPPER_LEVEL)
	{
		struct BlockMetaData *buddy = (struct BlockMetaData *)((uint32)bmData ^ BLOCK_SIZE(lvl));
		if (!buddy->is_free || buddy->level != lvl)//law ma7goz aw felevel as8r
			break;
		LIST_REMOVE(&freeBlockLists[lvl], buddy);//5leh fady
		if (buddy < bmData)
			bmData = buddy;
		lvl++;
	}
	bmData->level = lvl;
	bmData->is_free = 1;
	if (lvl == BUDDY_UPPER_LEVEL)//el block wasal l a3la level , nshof nos el page el tany
	{
		struct BlockMetaData *other = (struct BlockMetaData *)((uint32)bmData ^ BLOCK_SIZE(lvl));
		if (other->is_free && other->level == lvl)//el nos el tany fady kaman (mat2smsh w mafe4 7aga m7goza fih)
		{
			LIST_REMOVE(&freeBlockLists[lvl], other);//4elo mn el list 3shan el page kolha htetmsa7
			delete_buddy_space((void *)bmData);//el page kolha fadya , nrg3ha ll kernel heap
			return;
		}
	}
	LIST_INSERT_HEAD(&freeBlockLists[lvl], bmData);//5leh available tany
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
	if (va == NULL)
			return alloc_block(newSize);//law mafesh va a3ml 1 gded
	if (newSize == 0)
	{
		free_block(va);
		return NULL;
	}
	if (newSize > DYN_ALLOC_MAX_BLOCK_SIZE - META_DATA_SIZE)//law akbr mn akbr level
		return NULL;
	struct BlockMetaData *blk = (struct BlockMetaData *)((uint32)va - META_DATA_SIZE);
	int oldLevel = blk->level;
	uint32 totalSize = newSize + META_DATA_SIZE;
	int newLevel = BUDDY_LOWER_LEVEL;
	while (BLOCK_SIZE(newLevel) < totalSize)
		newLevel++;
	if (newLevel == oldLevel)
		return va;
	if (newLevel < oldLevel)
	{
		for (int l = newLevel; l < oldLevel; l++)
		{
			struct BlockMetaData *upper = (struct BlockMetaData *)((uint32)blk + BLOCK_SIZE(l));
			upper->level = l;
			upper->is_free = 1;
			LIST_INSERT_HEAD(&freeBlockLists[l], upper);
		}
		blk->level = newLevel;
		return va;//m7sl4 ay t8eer elsize
	}
	if (((uint32)blk & (BLOCK_SIZE(newLevel) - 1)) == 0)
	{
		int canExpand = 1;
		for (int l = oldLevel; l < newLevel; l++)
		{
			struct BlockMetaData *buddy = (struct BlockMetaData *)((uint32)blk + BLOCK_SIZE(l));
			if (!buddy->is_free || buddy->level != l)//lw elmkan ma7goz aw as8r mn el va
			{
				canExpand = 0;
				break;
			}
		}
		if (canExpand)
		{
			for (int l = oldLevel; l < newLevel; l++)
			{
				struct BlockMetaData *buddy = (struct BlockMetaData *)((uint32)blk + BLOCK_SIZE(l));
				LIST_REMOVE(&freeBlockLists[l], buddy);//lw buddies m2soma gm3ha 3shan elblock kolo
			}
			blk->level = newLevel;
			return va;
		}
	}
	void *newVa = alloc_block(newSize);//tgeb msa7a gdeda ll address algded
	if (newVa == NULL)
		return NULL;
	memcpy(newVa, va, BLOCK_SIZE(oldLevel) - META_DATA_SIZE);//copy el address elgded mn eladem
	free_block(va);//4el msa7a eladdress eladem
	return newVa;
}
