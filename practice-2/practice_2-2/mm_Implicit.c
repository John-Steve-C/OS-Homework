/*
 * This is the Implicit Idle Linked-list, I write it by the code from CSAPP
 * Also there are some bugs in CSAPP's code, like
 * - CHUNKSIZE is too large
 * - size in extend_heap need to be the multiple of 8 Bytes,
 *   but the parameter we send into the function is the multiple(DSIZE),
 *   so we needn't extend too much space.
 *
 * And I complete first-fit & next-fit to scan free blocks
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

/* Basic constants and macros */
#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<8) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *) (p))
#define PUT(p, val) (*(unsigned int *) (p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)     // = 1 means this block has been allocated

/* Given block ptr bp, compute address of its header and footer */
#define HEADER(bp) ((char *) (bp) - WSIZE)
#define FOOTER(bp) ((char *) (bp) + GET_SIZE(HEADER(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLOCK(bp) ((char *) (bp) + GET_SIZE(((char *) (bp) - WSIZE)))
#define PREV_BLOCK(bp) ((char *) (bp) - GET_SIZE(((char *) (bp) - DSIZE)))

// choose which kind of fit
//#define FIRST_FIT
#define NEXT_FIT

// point at the pos after the prologue block (size = 2 * WSIZE)
char *heap_list;
char *last_fit_pos;

static void *merge_block(void *bp) {
    size_t prev_alloc = GET_ALLOC(FOOTER(PREV_BLOCK(bp)));
    size_t next_alloc = GET_ALLOC(HEADER(NEXT_BLOCK(bp)));
    size_t size = GET_SIZE(HEADER(bp));

    if (prev_alloc && next_alloc) return bp;
    else if (prev_alloc && !next_alloc) {

#ifdef NEXT_FIT
        if (last_fit_pos == NEXT_BLOCK(bp)) last_fit_pos = bp;
#endif

        size += GET_SIZE(HEADER(NEXT_BLOCK(bp)));
        PUT(HEADER(bp), PACK(size,0));
        PUT(FOOTER(bp), PACK(size,0));
    } else if (!prev_alloc && next_alloc) {

#ifdef NEXT_FIT
        if (last_fit_pos == bp) last_fit_pos = PREV_BLOCK(bp);
#endif

        size += GET_SIZE(HEADER(PREV_BLOCK(bp)));
        PUT(FOOTER(bp), PACK(size,0));
        PUT(HEADER(PREV_BLOCK(bp)), PACK(size,0));
        bp = PREV_BLOCK(bp);
    } else {

#ifdef NEXT_FIT
        if (last_fit_pos == NEXT_BLOCK(bp) || last_fit_pos == bp) last_fit_pos = PREV_BLOCK(bp);
#endif

        size += GET_SIZE(HEADER(PREV_BLOCK(bp))) + GET_SIZE(FOOTER(NEXT_BLOCK(bp)));
        PUT(HEADER(PREV_BLOCK(bp)), PACK(size,0));
        PUT(FOOTER(NEXT_BLOCK(bp)), PACK(size,0));
        bp = PREV_BLOCK(bp);
    }

    return bp;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size = words;

    // Allocate an even number of words to maintain alignment
//    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // words is the multiple of 8(DSIZE)
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    // Initialize free block header/footer and the epilogue header
    PUT(HEADER(bp), PACK(size,0));
    PUT(FOOTER(bp), PACK(size,0));
    PUT(HEADER(NEXT_BLOCK(bp)), PACK(0,1));

    return merge_block(bp);
}

static void *find_fit(size_t size) {

#ifdef FIRST_FIT
    // first-fit search
    for (void *bp = heap_list; GET_SIZE(HEADER(bp)) > 0; bp = NEXT_BLOCK(bp)) {
        if (!GET_ALLOC(HEADER(bp)) && (size <= GET_SIZE(HEADER(bp)))) return bp;
    }
    return NULL;
#endif

#ifdef NEXT_FIT
    // next-fit search
    for (void *bp = last_fit_pos; GET_SIZE(HEADER(bp)) > 0; bp = NEXT_BLOCK(bp)) {
        if (!GET_ALLOC(HEADER(bp)) && (size <= GET_SIZE(HEADER(bp)))) return last_fit_pos = bp;
    }
    for (void *bp = heap_list; bp != last_fit_pos; bp = NEXT_BLOCK(bp)) {
        if (!GET_ALLOC(HEADER(bp)) && (size <= GET_SIZE(HEADER(bp)))) return last_fit_pos = bp;
    }
    return NULL;
#endif

}

static void place(void *bp, size_t size) {
    size_t csize = GET_SIZE(HEADER(bp));

    if ((csize - size) >= (2*DSIZE)) {
        PUT(HEADER(bp), PACK(size,1));
        PUT(FOOTER(bp), PACK(size,1));
        bp = NEXT_BLOCK(bp);
        PUT(HEADER(bp), PACK(csize-size,0));
        PUT(FOOTER(bp), PACK(csize-size,0));
    } else {
        PUT(HEADER(bp), PACK(csize,1));
        PUT(FOOTER(bp), PACK(csize,1));
    }
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {
    // Create the initial empty heap
    if ((heap_list = mem_sbrk(4 * WSIZE)) == (void*)-1) return -1;

    PUT(heap_list, 0);                                       // Alignment padding
    PUT(heap_list + (WSIZE * 1), PACK(DSIZE,1));    // Prologue header
    PUT(heap_list + (WSIZE * 2), PACK(DSIZE,1));    // Prologue footer
    PUT(heap_list + (WSIZE * 3), PACK(0,1));   // Epilogue header
    heap_list += (2*WSIZE);

#ifdef NEXT_FIT
    last_fit_pos = heap_list;
#endif

    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE) == NULL) return -1;
    return 0;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size) {
    size_t asize, extend_size;  // asize: adjusted block size
    char *bp;

    if (size == 0) return NULL;

    // Adjust block size to include overhead and alignment reqs
    // obviously it's the multiple of 8(DSIZE)
    if (size <= DSIZE) asize = 2*DSIZE;
    else asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    // Search the free list for a fit
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    } else {
        // No fit found. Get more memory and place the block
        extend_size = MAX(asize,CHUNKSIZE);
        if ((bp = extend_heap(extend_size)) == NULL) return NULL;
        place(bp, asize);
        return bp;
    }
}

/*
 * free - We don't know how to free a block.  So we ignore this call.
 *      Computers have big memories; surely it won't be a problem.
 */
void free(void *ptr) {
    if (ptr == NULL) return;
    size_t size = GET_SIZE(HEADER(ptr));
    PUT(HEADER(ptr), PACK(size,0));
    PUT(FOOTER(ptr), PACK(size,0));
    merge_block(ptr);
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        free(oldptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (oldptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if (!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = *SIZE_PTR(oldptr);
    if (size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);

    return newptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc(size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check,
 *      so nah!
 */
void mm_checkheap(int verbose) {
    /*Get gcc to be quiet. */
    verbose = verbose;
}
