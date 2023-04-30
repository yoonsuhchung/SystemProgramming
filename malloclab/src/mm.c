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
#include <limits.h>
#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// word size, double word size
 #define WSIZE 4 
 #define DSIZE 8

#define CHUNKSIZE (1<<12) 

#define MAX(x, y) ((x) > (y)? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7) 
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE) 
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// macros for block pointers
#define NEXT_PTR(bp) ((char *)(bp))
#define PREV_PTR(bp) ((char *)(bp)+WSIZE)
#define NEXT_FREE(bp) ((char *) GET(NEXT_PTR(bp))) 
#define PREV_FREE(bp) ((char *) GET(PREV_PTR(bp)))
#define PUTPTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))
#define NEXT_CLASS_PTR(bp, class) ((char*)bp+WSIZE*class)
#define NEXT_CLASS(bp, class) ((char*) GET(NEXT_CLASS_PTR(bp, class)))


/*********************************************************
* <Implemetation>
*
*  Each free block is in a specific free list according to its class(size).
*  void* free_listp has 14 pointers for each class in its prologue.
*  When a new memory should be allocated, its best match is searched from 
*  a free list of the smallest (fittable) class. 
*  If the match is not found, loop next smallest class until all bigger 
*  classes are searched. 
*  Still not found, then extend the heap to make space.
*  If the free block is much bigger than the size needed, split the block
*  and place the leftover in the front of the free list of its size.
*  When a block is naturally freed, the freed block is also placed back in the front of the
*  free list of its size. 
*  Every time a free block is made, it should be coalesced.s
*  Normal free blocks have next/prev pointers pointing to the next/prev free block
*  in their free list.
*  The 'prev pointer' of the first free block in a specific free list points to 
*  free_listp. 
*  The 'next pointer' of the last free block in a specific free list must be null.
*********************************************************/



// segregated list of classes, where each class is implemented with explicit list 
void* free_listp;


/*********************************************************
* helper functions
*********************************************************/

// returns whether ptr is a valid pointer of a block.
// a valid pointer should be placed within the heap range,
// aligned to DSIZE, and must have identical boundary tags.
static int isptr(void* ptr){
    void* he = mem_heap_hi();
    if((char*)ptr <=(char*)he && ptr>=free_listp && !((unsigned int)(ptr)&0x7) && GET(HDRP(ptr))==GET(FTRP(ptr)))
        return 1;
    return 0;
}


// checking validity of memory allocation
static void mm_check(void){
    char *bp;

    for(bp = free_listp;GET_SIZE(HDRP(bp))>0; bp = NEXT_BLKP(bp)){
        // checks if any contiguous free block has escaped from coalescing
        if(!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))){
            printf("coasecse failed.\n");
            exit(1);
        }
        // checks if all free blocks are in the free list.
        // if both pointers are illegal, than the block is not in the free list.
        if(!GET_ALLOC(HDRP(bp))){
            // a free block must have at least one valid pointer(whether prev or next)
            // that connects it to the free list.
            if(!isptr(NEXT_FREE(bp)) && !isptr(PREV_FREE(bp))){
                printf("free block is not in free list.\n");
                exit(1);
            }
        }
    }
    // checks if any dirty block(allocated) is in the free list.
    // searches for each segregated class.
    for(int i=0;i<14;i++){
        bp = NEXT_CLASS(free_listp, i);
        for(;isptr(bp); bp=NEXT_FREE(bp)){
            if(GET_ALLOC(HDRP(bp))){
                printf("[%u]free list containing dirty block in class %d.\n", (unsigned int)bp, i+1);
                exit(1);
            }
        }
    }
}

// tells the segregated class for the given size of the block.
int classof(size_t size){
    int cnt=0;
    int class;
    unsigned int mask = 1<<(8*sizeof(unsigned int)-1);
    while(!(size&mask)) {
        cnt++;
        size<<=1;
    }
    class = 8*sizeof(unsigned long)-cnt-4<14? 8*sizeof(unsigned long)-cnt-5: 13 ;
    return class;
}

// key function for adding a new free block to the free list.
// it adds a new free block to the front of the free list of the 
// according segregated class.
static void addfirst(void* bp, size_t size){ 
    void* next_ptr;
    int class= classof(size);

    // might be null if the list is empty. 
    // otherwise will be set to the first element of the list
    next_ptr = NEXT_CLASS(free_listp, class);
    // insert the block to the front of the list and set prev/next pointers
    PUTPTR(NEXT_CLASS_PTR(free_listp, class), (char *) bp);
    PUTPTR(NEXT_PTR(bp), (char *) next_ptr);
    PUTPTR(PREV_PTR(bp), (char *) free_listp);
    if(next_ptr!=NULL) PUTPTR(PREV_PTR(next_ptr), (char*) bp);
}

// coalesces a free block in a constant time.
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); 
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); 
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(bp)));
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
    void* next_free= NEXT_BLKP(bp);
    void* prev_free= PREV_BLKP(bp);

    // if neither of the neighboring blocks are free, just return.
    if (prev_alloc && next_alloc) {
        return bp;
        }

    // if the next block is free
    else if (prev_alloc && !next_alloc) {
        int class = classof(next_size);
        // takes out the next block from the original list.
        if(isptr(PREV_FREE(next_free))) {
            if(PREV_FREE(next_free)==free_listp) // when block is the first element of its free list
                PUTPTR(NEXT_CLASS_PTR(free_listp, class), NEXT_FREE(next_free));
            else PUTPTR(NEXT_PTR(PREV_FREE(next_free)), NEXT_FREE(next_free));
        }
        if(isptr(NEXT_FREE(next_free))) 
            PUTPTR(PREV_PTR(NEXT_FREE(next_free)), PREV_FREE(next_free));
        // concatenating two free blocks
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); 
        PUT(HDRP(bp), PACK(size,0)); 
        PUT(FTRP(bp), PACK(size,0));
    }
    // if the previous block is free
    else if (!prev_alloc && next_alloc) {
        int class = classof(prev_size);
        // takes out the previous block from the original list.
        if(isptr(PREV_FREE(prev_free))){
            if(PREV_FREE(prev_free)==free_listp) // when block is the first element of its free list
                PUTPTR(NEXT_CLASS_PTR(free_listp, class), NEXT_FREE(prev_free));
            else
                PUTPTR(NEXT_PTR(PREV_FREE(prev_free)), NEXT_FREE(prev_free));
        }
        if(isptr(NEXT_FREE(prev_free)))
            PUTPTR(PREV_PTR(NEXT_FREE(prev_free)), PREV_FREE(prev_free));
        // concatenating two free blocks
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); 
        PUT(FTRP(bp), PACK(size, 0)); 
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
        bp = PREV_BLKP(bp);
        }
    // if the next/previous blocks are both free
    else {
        int prev_class = classof(prev_size);
        int next_class = classof(next_size);
        // takes out the neighboring next/prev free block from the original list.
        if(isptr(PREV_FREE(next_free))) {
            if(PREV_FREE(next_free)==free_listp)
                PUTPTR(NEXT_CLASS_PTR(free_listp, next_class), NEXT_FREE(next_free));
            else PUTPTR(NEXT_PTR(PREV_FREE(next_free)), NEXT_FREE(next_free));
        }
        if(isptr(NEXT_FREE(next_free))) 
            PUTPTR(PREV_PTR(NEXT_FREE(next_free)), PREV_FREE(next_free));
        if(isptr(PREV_FREE(prev_free))){
            if(PREV_FREE(prev_free)==free_listp)
                PUTPTR(NEXT_CLASS_PTR(free_listp, prev_class), NEXT_FREE(prev_free));
            else
                PUTPTR(NEXT_PTR(PREV_FREE(prev_free)), NEXT_FREE(prev_free));
        }if(isptr(NEXT_FREE(prev_free))) 
            PUTPTR(PREV_PTR(NEXT_FREE(prev_free)), PREV_FREE(prev_free));
        // concatenating the blocks
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +GET_SIZE(FTRP(NEXT_BLKP(bp))); 
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        }
    return bp;
    }

// extends the size of the heap when there is no space in the free list.
static void *extend_heap(size_t words){
    char *bp; 
    size_t size;
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    PUT(HDRP(bp), PACK(size, 0)); 
    PUT(FTRP(bp), PACK(size, 0)); 
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); 

    // extended part of the heap becomes a new free block,
    // so coalesce it and insert it to the according free list.
    bp = coalesce(bp);
    size = GET_SIZE(HDRP(bp));
    addfirst(bp, size);
    return bp;
    }



// static void *find_fit(size_t asize){
//     void *bp;
//     int class = classof(asize);
//     for(int i=class;i<14;i++){
//         bp = NEXT_CLASS(free_listp, i);
//         for(;isptr(bp); bp = NEXT_FREE(bp)){
//             if(asize<=GET_SIZE(HDRP(bp))){
//                 return bp;
//             }
//     }
//     }
//     return NULL;
// }

// finds the best fit free block for a particular size.
static void *find_bestfit(size_t asize){
    void *bp;
    void* res;
    int class = classof(asize);
    int flag=0;
    size_t min=INT_MAX;

    for(int i=class;i<14;i++){
        bp = NEXT_CLASS(free_listp, i);
        for(;isptr(bp); bp = NEXT_FREE(bp)){
            if(asize<=GET_SIZE(HDRP(bp))&&GET_SIZE(HDRP(bp))<min){
                min = GET_SIZE(HDRP(bp));
                res = bp;
                flag=1;
            }
        }
        if(flag==1) return res;
    }
    return NULL;
}

// allocates a space for 'asize'd memory.
static void place(void* bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));
    void* next_ptr = NEXT_FREE(bp);
    void* prev_ptr = PREV_FREE(bp);

    // if the size of the free block is quite bigger than 'asize'
    // so that a new free block can be splitted from it.
    if((csize-asize) >= (2*DSIZE)){
        // first 'asize'd block is marked as allocated.
        PUT(HDRP(bp), PACK(asize,1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        // leftover is grouped as a new block, marked as unallocated.
        PUT(HDRP(bp), PACK(csize-asize,0));
        PUT(FTRP(bp), PACK(csize-asize,0));
        // deals with pointers pointing to this block.
        if(prev_ptr==free_listp) PUTPTR(NEXT_CLASS_PTR(prev_ptr, classof(csize)), next_ptr);
        else if(prev_ptr!=NULL) PUTPTR(NEXT_PTR(prev_ptr), next_ptr);
        if(next_ptr!=NULL) PUTPTR(PREV_PTR(next_ptr), prev_ptr);
        bp = coalesce(bp);
        addfirst(bp, GET_SIZE(HDRP(bp)));
        // printf("%u place 1 class %d\n", (unsigned int)bp, classof(csize-asize)+1);
    }

    else{
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        if(prev_ptr==free_listp) PUTPTR(NEXT_CLASS_PTR(prev_ptr, classof(csize)), next_ptr);
        else if(prev_ptr!=NULL) PUTPTR(NEXT_PTR(prev_ptr), next_ptr);
        if(next_ptr!=NULL) PUTPTR(PREV_PTR(next_ptr), prev_ptr);
    }
}

/*********************************************************
* main functions
*********************************************************/

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    mem_init();
    if ((free_listp = mem_sbrk(18*WSIZE)) == (void *)-1)
        return -1;

    PUT(free_listp, 0);
    PUT(free_listp + (1*WSIZE), PACK(8*DSIZE, 1)); //prologue header
    // prologue has only 'next pointers', which points to each first elem of the segregated class.
    // this free list consists of 14 classes.
    for(int i=0;i<14;i++){
        PUT(free_listp + ((i+2)*WSIZE), 0); 
    }
    PUT(free_listp + (16*WSIZE), PACK(8*DSIZE, 1)); //prologue footer
    PUT(free_listp + (17*WSIZE), PACK(0, 1)); // epilogue header
    free_listp += (2*WSIZE);
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by finding the best fit or extending the heap.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size){
    size_t asize; 
    size_t extendsize; 
    char *bp;
    if (size == 0)
        return NULL;
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    if ((bp = find_bestfit(asize)) != NULL) {
        place(bp, asize); 
        return bp;
      }
    // if there is no space in the free list, extend the heap and allocate
    // memory to the new space(might or might not start from the new heapspace)
    extendsize = MAX(asize,CHUNKSIZE); 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize); 
    // mm_check();
    return bp;
    }


/*
 * mm_free - Frees a block and coalesce. Then insert it to the beginning of the free list.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    ptr = coalesce(ptr);
    size = GET_SIZE(HDRP(ptr));
    // after coalescing the free block, insert it to the beginning of the appropriate free list.
    addfirst(ptr, size);
    // mm_check();
}

/*
 * mm_realloc - Use mm_malloc only when constant approach fails.
 *      i.e. when next free block is not big enough,
 *      or when next block is a plain allocated block.
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    size_t asize;
    
    if(size<=0){
        mm_free(ptr);
        return NULL;
    }
    if(ptr==NULL){
        return mm_malloc(size);
    }

    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    copySize = GET_SIZE(HDRP(oldptr));

    // when asize is smaller than the size of the original block
    if (asize <= copySize)
        return ptr;
    
    // if next block is unallocated, and its size is big enough
    if(!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && GET_SIZE(HDRP(NEXT_BLKP(ptr)))+copySize>=asize){
        void* next_free = NEXT_BLKP(ptr);
        size_t nextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        // gets ready to use the block.. changes pointers pointing to this block.
        int class = classof(nextsize);
        if(isptr(PREV_FREE(next_free))) {
            if(PREV_FREE(next_free)==free_listp) 
                PUTPTR(NEXT_CLASS_PTR(free_listp, class), NEXT_FREE(next_free));
            else PUTPTR(NEXT_PTR(PREV_FREE(next_free)), NEXT_FREE(next_free));
        }
        if(isptr(NEXT_FREE(next_free))) 
            PUTPTR(PREV_PTR(NEXT_FREE(next_free)), PREV_FREE(next_free));
        // the original block has been concatenated with the next free block.
        copySize += nextsize;
        PUT(HDRP(ptr), PACK(copySize,1)); 
        PUT(FTRP(ptr), PACK(copySize,1));
        return ptr;
    }
    // if next block is an epilogue block, extend the heap and use that space
    if(GET_SIZE(HDRP(NEXT_BLKP(ptr)))==0){
        void* bp;
        size_t extendsize = asize - copySize;
        if ((long)(bp = mem_sbrk(extendsize)) == -1)
            return NULL;
        PUT(HDRP(bp), PACK(extendsize, 0)); 
        PUT(FTRP(bp), PACK(extendsize, 0)); 
        PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); 
        // the original block has been concatenated with the new heap space.
        PUT(HDRP(ptr), PACK(asize,1)); 
        PUT(FTRP(ptr), PACK(asize,1));
        return ptr;
    }
    
    // after all tries fail, just call mm_malloc
    if((newptr = mm_malloc(size))==NULL){
            return NULL;
    }
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    // mm_check();
    return newptr;
}






