/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
/* 
 * 优化建议：隐式链表 Next-fit 方案
 * 修复了 rover 指针失效导致的段错误，并优化了 realloc
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

 team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* 宏定义部分保持不变 */
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define MAX(x,y) ((x)> (y)?(x):(y))
#define PACK(size,alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *) (p))
#define PUT(p,val) (*(unsigned int *)(p) =(val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *) (bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *) (bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static char *heap_listp;
static char *rover; // Next-fit 指针

/* 函数原型 */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t size);
static void place(void *bp, size_t size);

/* mm_init: 初始化堆 */
int mm_init(void) {
    if(((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)) return -1;
    
    PUT(heap_listp, 0);                          // 填充对齐
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 序言块头部
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 序言块脚部
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // 结尾块头部
    heap_listp += (2*WSIZE);
    
    rover = heap_listp; // 初始化 rover

    if(extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}

/* mm_malloc */
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extend_size;
    char *bp;

    if(size == 0) return NULL;
    if(size <= DSIZE) asize = 2*DSIZE;
    else asize = ALIGN(size + DSIZE);

    if((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extend_size = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extend_size/WSIZE)) == NULL) return NULL;
    place(bp, asize);
    return bp;
}

/* mm_free */
void mm_free(void *ptr) {
    if(!ptr) return;
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/* coalesce: 合并空闲块，关键是同步更新 rover */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        // 情况 1: 不合并
    } else if (prev_alloc && !next_alloc) {
        // 情况 2: 合并下一块
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        // 情况 3: 合并前一块
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        // 情况 4: 合并前后两块
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    // 关键修复：确保 rover 指向的地址依然是一个块的起始位置
    rover = bp; 
    return bp;
}

/* find_fit: Next-fit 搜索 */
static void *find_fit(size_t asize) {
    char *bp;
    
    // 从 rover 搜索到堆末尾
    for (bp = rover; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            rover = bp;
            return bp;
        }
    }

    // 如果没找到，从头开始搜索到 rover
    for (bp = heap_listp; bp != rover; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            rover = bp;
            return bp;
        }
    }
    
    return NULL;
}

/* place: 分割块 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        rover = bp; // 将 rover 指向分割出来的空闲块，提升后续分配效率
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        rover = NEXT_BLKP(bp); // 指向下一个块
    }
}

/* mm_realloc: 简单的优化版 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }

    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t asize = (size <= DSIZE) ? 2*DSIZE : ALIGN(size + DSIZE);

    if (old_size >= asize) {
        return ptr; // 当前块够大，直接返回（此处也可尝试切割提高利用率）
    } else {
        // 尝试检查下一块是否为空闲且空间足够
        size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        
        if (!next_alloc && (old_size + next_size >= asize)) {
            PUT(HDRP(ptr), PACK(old_size + next_size, 1));
            PUT(FTRP(ptr), PACK(old_size + next_size, 1));
            rover = ptr; // 安全起见重置 rover
            return ptr;
        } else {
            // 实在不行，重新分配
            void *newptr = mm_malloc(size);
            if (newptr == NULL) return NULL;
            memcpy(newptr, ptr, old_size - DSIZE);
            mm_free(ptr);
            return newptr;
        }
    }
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 更新结尾块

    return coalesce(bp);
}