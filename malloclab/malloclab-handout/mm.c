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
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
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

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// 全局变量，指向序言块
static char *heap_listp;
/* 函数原型 */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t size);
static void place(void *bp, size_t size);

/* Basic constants and macros */
#define WSIZE 4
/* Word and header/footer size (bytes) */
#define DSIZE 8
/* Double word size (bytes) */
#define CHUNKSIZE (1<<12)
/* Extend heap by this amount (bytes) */
#define MAX(x,y) ((x)> (y)?(x):(y))
/* Pack a size and allocated bit into a word */
#define PACK(size,alloc) ((size) | (alloc))
/* Read and write a word at address p */
#define GET(p) (*(unsigned int *) (p))
#define PUT(p,val) (*(unsigned int *)(p) =(val))
/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *) (bp) + GET_SIZE(HDRP(bp)) - DSIZE)
/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *) (bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if(((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)){
        return -1;
    }
    PUT(heap_listp,0);
    PUT(heap_listp + (1*WSIZE),PACK(DSIZE,1));
    PUT(heap_listp + (2*WSIZE),PACK(DSIZE,1));
    PUT(heap_listp + (3*WSIZE),PACK(0,1));
    heap_listp += (2*WSIZE);

    // 分配初始堆内存
    if(extend_heap(CHUNKSIZE) == NULL){
        return -1;
    }
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
   size_t asize;
   size_t extend_size;
   char *bp;

   if(size == 0){
      return NULL;
   }
   if(size <= DSIZE){
        asize = 2*DSIZE;
   }else{
        asize = ALIGN(size+DSIZE);  
   }
   // 查找匹配的空闲链表
   if((bp = find_fit(asize))!=NULL){
        place(bp,asize);
        return bp;
   }
   // 没有找到，则扩展堆空间
   extend_size = MAX(asize,CHUNKSIZE);
   if((bp = extend_heap(extend_size)) == NULL){
        return NULL;
   }
   place(bp,asize);
   return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if(!ptr){
        return;
    }
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr),PACK(size,0));
    PUT(FTRP(ptr),PACK(size,0));
    coalesce(ptr);
}
/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if(ptr == NULL) return mm_malloc(size);
    if(size == 0){
        mm_free(ptr);
        return NULL;
    }

    size_t old_size = GET_SIZE(HDRP((char*)ptr));
    size_t asize = (size <= DSIZE) ? 2*DSIZE : ALIGN(size + DSIZE);
    // 情形1: 当前块足够大,直接返回,不再切割(性能会有所提升)
    if(asize <= old_size){
        return ptr;
    } else {
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

        if(!next_alloc && (old_size + next_size >= asize)){
            PUT(HDRP(ptr),PACK(old_size+next_size,1));
            PUT(FTRP(ptr),PACK(old_size+next_size,1));
            return ptr;
        }else {
            // 重新分配
            void *newptr = mm_malloc(size);
            if(newptr == NULL) return NULL;
            memcpy(newptr, ptr, old_size - DSIZE);
            mm_free(ptr);
            return newptr;
        }
    }
    
}

static void *extend_heap(size_t bytes){
    char *bp;
    size_t size = ALIGN(bytes);
    if((bp = mem_sbrk(size)) == (void *)-1){
        return NULL;
    }
    PUT(HDRP(bp),PACK(size,0));
    PUT(FTRP(bp),PACK(size,0));
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));

    //合并前一个空闲块
    return coalesce(bp);
}

//合并该空闲块前后的空闲块
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc =  GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    //四种情况
    //1.前后都已经分配
    if(prev_alloc && next_alloc){
        return bp;
    }
    //2.前一个已分配，后面的没分配
    else if(prev_alloc && !next_alloc){
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));   
        PUT(HDRP(bp),PACK(size,0));
        PUT(FTRP(bp),PACK(size,0));
    }
    //3.前一个没分配，后面的已经分配
    else if(!prev_alloc && next_alloc){
        char *prev_bp = PREV_BLKP(bp);
        size += GET_SIZE(HDRP(prev_bp));   
        PUT(HDRP(prev_bp),PACK(size,0));
        PUT(FTRP(prev_bp),PACK(size,0));
        bp = prev_bp;
    }
    //4.前后都是空闲的
    else{
        char *prev_bp = PREV_BLKP(bp);
        size = size + GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(prev_bp),PACK(size,0));
        PUT(FTRP(prev_bp),PACK(size,0));
        bp = prev_bp;
    }
    return bp;

}
static void *find_fit(size_t size){
    //首次适配,从heap_listp开始搜索,到结尾块(只有头部0/1)停止
    char *bp;
    for(bp=heap_listp; GET_SIZE(HDRP(bp))>0; bp=NEXT_BLKP(bp)){
        if(!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp))>=size)){
            return bp;
        }
    }
    return NULL;
}
//如果空闲块的大小-size < 2*DSIZE,则直接全部分配，否则切割
static void place(void *bp,size_t size){
    size_t asize = GET_SIZE(HDRP(bp));
    if((asize - size) < 2 * DSIZE){
        PUT(HDRP(bp),PACK(asize,1));
        PUT(FTRP(bp),PACK(asize,1));
    }else{
        PUT(HDRP(bp),PACK(size,1));
        PUT(FTRP(bp),PACK(size,1));
        char *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp),PACK(asize-size,0));
        PUT(FTRP(next_bp),PACK(asize-size,0));
    }
}