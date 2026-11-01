#include "kheap.h" // kheapPageAllocStart ,kheapPageAllocBreak , declaration of functions

#include <inc/memlayout.h> //KERNEL_HEAP_START ,  KERNEL_HEAP_MAX
#include <inc/dynamic_allocator.h> // alloc_block,free_block,META_DATA_SIZE,BLOCK_SIZE
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h> // frames functions
#include "../conc/kspinlock.h" //acquire_kspinlock ,release_kspinlock,init_kspinlock
#include "../conc/ksemaphore.h"
#include <kern/cpu/cpu.h>

#define MAX_KHEAP_FRAMES 0x100000 // equal 1,048,576 frames --> 4GB(size of ram)/4KB (size of frame)
static uint32 frame_va[MAX_KHEAP_FRAMES];
#define KH_NUM_PAGES    ((KERNEL_HEAP_MAX - KERNEL_HEAP_START) / PAGE_SIZE) // result no.of pages in heap
#define PG_IDX(va)      (((uint32)(va) - KERNEL_HEAP_START) / PAGE_SIZE) // return page number which contain specfic va
#define PG_VA(idx)      (KERNEL_HEAP_START + (uint32)(idx) * PAGE_SIZE) // // return va which contain idx (page num)

static uint32 area_tag[KH_NUM_PAGES];
static unsigned char buddy_page[KH_NUM_PAGES];
static struct kspinlock kheap_lock;

/* ================= TAGS =================
 * area_tag[i] describes an "area" (group of pages) that STARTS at page i.
 *   used area : tag = number of pages
 *   free area : tag = FREE_FLAG | number of pages (also written on its LAST page)
 *   any other page inside an area : tag = 0
 */
#define TAG_FREE_FLAG   0x80000000u
#define TAG_USED(n)     ((uint32)(n))
#define TAG_IS_USED(t)  (((t) != 0) && !((t) & TAG_FREE_FLAG))
#define TAG_IS_FREE(t)  (((t) & TAG_FREE_FLAG) != 0)
#define TAG_PAGES(t)    ((t) & ~TAG_FREE_FLAG)

#define KH_PANIC(...)   do { release_kspinlock(&kheap_lock); panic(__VA_ARGS__); } while (0)

/* ================= HELPERS ================= */
// doubly-linked list of FREE areas only (values = page index + 1 , 0 = null)
static uint32 fl_head;
static uint32 fl_next[KH_NUM_PAGES];
static uint32 fl_prev[KH_NUM_PAGES];

static void fl_insert(uint32 idx)
{
	fl_prev[idx] = 0;
	fl_next[idx] = fl_head;
	if (fl_head)
		fl_prev[fl_head - 1] = idx + 1;
	fl_head = idx + 1;
}

static void fl_remove(uint32 idx)
{
	if (fl_prev[idx])
		fl_next[fl_prev[idx] - 1] = fl_next[idx];
	else
		fl_head = fl_next[idx];
	if (fl_next[idx])
		fl_prev[fl_next[idx] - 1] = fl_prev[idx];
}

 //clear the tags of n pages starting at page idx
static void zero_tags(uint32 idx, uint32 n)
{
	for (uint32 i = 0; i < n; i++)
		area_tag[idx + i] = 0;
}

//free area tag (first page +last page )
static void set_free_area(uint32 idx, uint32 n)
{
	zero_tags(idx, n);
	area_tag[idx] = TAG_FREE_FLAG | n;
	area_tag[idx + n - 1] = TAG_FREE_FLAG | n;
	fl_insert(idx);
}

// give n physical frames to n pages starting at va. 0 = success, -1 = failure
static int map_range(uint32 va, uint32 n);

/* remove the mapping of n pages and free their frames */
static void unmap_range(uint32 va, uint32 n)
{
	for (uint32 i = 0; i < n; i++)
	{
		uint32 cur = va + i * PAGE_SIZE;
		uint32 *ptr_pt = NULL;
		struct FrameInfo *fi = get_frame_info(ptr_page_directory, cur, &ptr_pt);
		if (fi != NULL)
		{
			frame_va[to_frame_number(fi)] = 0;
			unmap_frame(ptr_page_directory, cur);
		}
	}
}

static int map_range(uint32 va, uint32 n)
{
	for (uint32 i = 0; i < n; i++)
	{
		uint32 cur = va + i * PAGE_SIZE;
		struct FrameInfo *fi = NULL;

		if (allocate_frame(&fi) != 0)
		{
			unmap_range(va, i);               /* undo what we did */
			return -1;
		}
		if (map_frame(ptr_page_directory, fi, cur, PERM_WRITEABLE) != 0)
		{
			free_frame(fi);
			unmap_range(va, i);
			return -1;
		}
		frame_va[to_frame_number(fi)] = cur;
	}
	return 0;
}

/* return an (already unmapped, tags-cleared) area to the free pool.
 * merges with free neighbours; if it touches the break, the break moves back. */
static void release_area(uint32 idx, uint32 n)
{
	uint32 end = PG_IDX(kheapPageAllocBreak);
	uint32 start = idx;
	uint32 total = n;

	/* merge with the next area */
	if (idx + n < end && TAG_IS_FREE(area_tag[idx + n]))
	{
		fl_remove(idx + n);                       /* it will be merged, so take it out of the free list */
		total += TAG_PAGES(area_tag[idx + n]);
	}

	/* merge with the previous area (look at its last page) */
	if (idx > 0 && TAG_IS_FREE(area_tag[idx - 1]))
	{
		uint32 pn = TAG_PAGES(area_tag[idx - 1]);
		start = idx - pn;
		fl_remove(start);                         /* it will be merged, so take it out of the free list */
		total += pn;
	}

	if (start + total == end)                     /* touches the end: shrink the heap */
	{
		zero_tags(start, total);
		kheapPageAllocBreak = PG_VA(start);
	}
	else
	{
		set_free_area(start, total);
	}
}

/* ================= PAGE ALLOCATOR ================= */
static void *page_alloc(uint32 npages) //CUSTOM FIT
/*
 * 1- Search for exact fit
 * 2- if not found
 * search for WORST fit till break (34an yseb akbr fragmentation )
 * 3-if not found
 * extend break (if available )
 * if not available
 * return NULL
 */
{
	if (npages == 0) // for protection
		return NULL;

	uint32 end = PG_IDX(kheapPageAllocBreak); // a5r page before el break
	uint32 selected_idx  = 0, bestn = 0; //selected_idx -> bedayt el block , bestn -> size of block
	int found = 0;
	int exact = 0; // 1 = an exact fit was found
	uint32 idx = 0;
	uint32 link = fl_head; // walk the FREE list only (not every page up to the break) , 0 = no free areas
	while (link)
	{
		idx = link - 1; // page index of the current free block
		uint32 t = area_tag[idx]; // tag with current block (el tag by7dd el block used&unused)
		uint32 n = TAG_PAGES(t); // no.of pages available in heap (btt8er mn block l block )
		if (n == 0 || !TAG_IS_FREE(t))
			KH_PANIC("page_alloc: corrupted tags at page %d", idx);
		// the free list is not sorted by address , so ties are broken by the LOWEST address
		if (n == npages) //exact ?
		{
			if (!exact || idx < selected_idx) // keep the lowest exact area
			{
				selected_idx  = idx;
				bestn = n; found = 1; exact = 1;
			}
		}
		else if (!exact && n > npages && (!found || n > bestn || (n == bestn && idx < selected_idx))) // npages -> no of pages el kernel 3ayz y7gzha =required pages (thapta)
		{
			selected_idx  = idx; bestn = n; found = 1;
		}
		link = fl_next[idx]; // next free block
	}

	if (found)
	{
		if (map_range(PG_VA(selected_idx ), npages) != 0)
			return NULL;
		fl_remove(selected_idx ); // the area is not free anymore
		zero_tags(selected_idx , bestn);
		area_tag[selected_idx ] = TAG_USED(npages);
		if (bestn > npages)
			set_free_area(selected_idx  + npages, bestn - npages);
		return (void *)PG_VA(selected_idx );
	}

	/* 2) no free area fits: grow the heap by moving the break */
	if (npages > (KERNEL_HEAP_MAX - kheapPageAllocBreak) / PAGE_SIZE) // exist available area for extend or not
		return NULL;
	if (map_range(kheapPageAllocBreak, npages) != 0)
		return NULL;

	zero_tags(end, npages);
	area_tag[end] = TAG_USED(npages);
	void *ret = (void *)kheapPageAllocBreak;
	kheapPageAllocBreak += npages * PAGE_SIZE;
	return ret;
}

static void page_free(uint32 idx)
{
	uint32 n = TAG_PAGES(area_tag[idx]);
	unmap_range(PG_VA(idx), n);
	zero_tags(idx, n);
	release_area(idx, n);
}

/************************************************************************************/
//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//TODO: [PROJECT'26.GM#2] KERNEL HEAP - [0] GIVEN FUNCTIONS
void kheap_init()
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		buddy_init();
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = KERNEL_HEAP_START;
		kheapPageAllocBreak = KERNEL_HEAP_START;
	}
	//==================================================================================
	//==================================================================================
	init_kspinlock(&kheap_lock, "kheap_lock");
}
/************************************************************************************/

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
/**************************************************/
/* BLOCK ALLOCATOR */
/**************************************************/
void* create_buddy_space() // alloc 1 page from kernel heap
{
	void *va = page_alloc(1); // alloc 1 page for buddy_system
	if (va == NULL) // No page available
		return NULL;
	buddy_page[PG_IDX(va)] = 1;                  //mark as a Buddy System Page, update el metadata
	return va; // return 1 page ready to use in buddy_system
}

void  delete_buddy_space(void *va)
{
	uint32 a = ROUNDDOWN((uint32)va, PAGE_SIZE);// convert va l int , rounddown 34an nt3aml m3 bedayt el page
	if (a < KERNEL_HEAP_START || a >= kheapPageAllocBreak || !buddy_page[PG_IDX(a)])
		panic("delete_buddy_space: %x is not a buddy-system page", va);

	buddy_page[PG_IDX(a)] = 0; // Mark the page as not used by the Buddy System
	page_free(PG_IDX(a));
}
/**************************************************/
/* PAGE ALLOCATOR */
/**************************************************/
void* kmalloc(unsigned int size)
{
	if (size == 0)
		return NULL;

	void *ret = NULL;
	acquire_kspinlock(&kheap_lock);
	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE - META_DATA_SIZE)
	{
		ret = alloc_block(size);                          /* BLOCK ALLOCATOR */
	}
	else
	{
		uint32 npages = (size / PAGE_SIZE) + ((size % PAGE_SIZE) ? 1 : 0);
		ret = page_alloc(npages);                         /* PAGE ALLOCATOR  */
	}
	release_kspinlock(&kheap_lock);
	return ret;
}

void kfree(void* virtual_address)
{
	uint32 va = (uint32)virtual_address;

	acquire_kspinlock(&kheap_lock);

	if (va < KERNEL_HEAP_START || va >= kheapPageAllocBreak)
		KH_PANIC("kfree: invalid address %x (outside the used kernel heap)", va);

	uint32 idx = PG_IDX(va);
	if (buddy_page[idx])
	{
		free_block(virtual_address);                      /* BLOCK ALLOCATOR */
	}
	else if ((va % PAGE_SIZE) == 0 && TAG_IS_USED(area_tag[idx]))
	{
		page_free(idx);                                   /* PAGE ALLOCATOR  */
	}
	else
	{
		KH_PANIC("kfree: invalid address %x (not an allocated block)", va);
	}

	release_kspinlock(&kheap_lock);
}

unsigned int kheap_physical_address(unsigned int virtual_address)
{
	uint32 *ptr_pt = NULL;
	struct FrameInfo *fi = get_frame_info(ptr_page_directory, virtual_address, &ptr_pt);
	if (fi == NULL)
		return 0;
	return to_physical_address(fi) + PGOFF(virtual_address);
}

unsigned int kheap_virtual_address(unsigned int physical_address)
{
	if (PPN(physical_address) >= number_of_frames)
		return 0;
	uint32 va = frame_va[PPN(physical_address)];
	if (va == 0)
		return 0;
	return va + PGOFF(physical_address);
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
static int pages_resize_inplace(uint32 idx, uint32 oldn, uint32 newn)
{
	uint32 end = PG_IDX(kheapPageAllocBreak);

	if (newn < oldn)                                      /* shrink: give the tail back */
	{
		unmap_range(PG_VA(idx + newn), oldn - newn);
		zero_tags(idx + newn, oldn - newn);
		area_tag[idx] = TAG_USED(newn);
		release_area(idx + newn, oldn - newn);
		return 1;
	}

	uint32 extra = newn - oldn;

	if (idx + oldn == end)                                /* last area: grow by moving the break */
	{
		if (extra > (KERNEL_HEAP_MAX - kheapPageAllocBreak) / PAGE_SIZE)
			return 0;
		if (map_range(kheapPageAllocBreak, extra) != 0)
			return 0;
		zero_tags(idx + oldn, extra);
		area_tag[idx] = TAG_USED(newn);
		kheapPageAllocBreak += extra * PAGE_SIZE;
		return 1;
	}

	uint32 t = area_tag[idx + oldn];                      /* grow into the free area after us */
	if (TAG_IS_FREE(t) && TAG_PAGES(t) >= extra)
	{

		uint32 m = TAG_PAGES(t);
		if (map_range(PG_VA(idx + oldn), extra) != 0)
			return 0;
		fl_remove(idx + oldn);                            /* the free area is consumed , take it out of the free list */
		zero_tags(idx + oldn, extra);
		area_tag[idx] = TAG_USED(newn);
		if (m > extra)
			set_free_area(idx + newn, m - extra);
		return 1;
	}
	return 0;
}

void *krealloc(void *virtual_address, uint32 new_size)
{
	if (virtual_address == NULL)
		return kmalloc(new_size);
	if (new_size == 0)
	{
		kfree(virtual_address);
		return NULL;
	}

	uint32 va = (uint32)virtual_address;
	uint32 old_bytes;

	acquire_kspinlock(&kheap_lock);

	if (va < KERNEL_HEAP_START || va >= kheapPageAllocBreak)
		KH_PANIC("krealloc: invalid address %x", va);

	uint32 idx = PG_IDX(va);
	if (buddy_page[idx])
	{
		old_bytes = get_block_size(virtual_address) - META_DATA_SIZE;
		if (new_size <= old_bytes)
		{
			release_kspinlock(&kheap_lock);
			return virtual_address;
		}
	}
	else
	{
		uint32 t = area_tag[idx];
		if ((va % PAGE_SIZE) != 0 || !TAG_IS_USED(t))
			KH_PANIC("krealloc: invalid address %x", va);

		uint32 oldn = TAG_PAGES(t);
		old_bytes = oldn * PAGE_SIZE;

		if (new_size > DYN_ALLOC_MAX_BLOCK_SIZE - META_DATA_SIZE)   /* PAGE -> PAGE */
		{
			uint32 newn = (new_size / PAGE_SIZE) + ((new_size % PAGE_SIZE) ? 1 : 0);
			if (newn == oldn || pages_resize_inplace(idx, oldn, newn))
			{
				release_kspinlock(&kheap_lock);
				return virtual_address;
			}
		}
	}
	release_kspinlock(&kheap_lock);

	void *nv = kmalloc(new_size);
	if (nv == NULL)
		return NULL;
	memcpy(nv, virtual_address, (new_size < old_bytes) ? new_size : old_bytes);
	kfree(virtual_address);
	return nv;
}
