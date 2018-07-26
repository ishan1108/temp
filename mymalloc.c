#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <mcheck.h>

#ifndef _MALLOC_H
#define _MALLOC_H 1


/* struct to hold block metadata
 * size represents the free block's size in bytes.
 * next points to next free block.
 */
typedef struct block_info
{
   int size;
   struct block_info *next;
}block_info;

/*mutex for global heap.*/
pthread_mutex_t global_heap_mutex = PTHREAD_MUTEX_INITIALIZER;

/* mutex for updating the stats variables */
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t test = PTHREAD_MUTEX_INITIALIZER;


// total arena size allocated in bytes.
unsigned long total_arena_size_allocated = 0;

// total size allocated through mmap system call.
unsigned long total_mmap_size_allocated = 0;

// total number of blocks in heap.
unsigned long total_number_of_blocks = 0;

// total number of allocation done. It is count of number of times malloc called
unsigned long total_allocation_request = 0;

// total free requests made.
unsigned long total_free_request = 0;

// total number of free blocks available (of all threads.)
unsigned long total_free_blocks = 0;


/* Four bins of size 8 byte, 64 byte, 512 byte and every thing else greater
 *  than 512 bytes.
 *
 * Each thread will have its own bin and storage arena.
 * Iniially all the bins are empty. The list gets build up on successive free
 * calls after malloc.
 */
__thread block_info *bin_8     = NULL;
__thread block_info *bin_64    = NULL;
__thread block_info *bin_512   = NULL;
__thread block_info *bin_large = NULL;


/*
 * A pointer to heap memory upto which the heap addresses are assigned to
 * the threads of process. Addresses beyond this upto heap end are available
 * for future thread of more memory expansion to threads.
 */
void *heap_used_memory_end = NULL;

/*
 *  pointer to a location from which hepa memory allocated to thread has not
 *  been
 *  used yet.
 */
__thread void *thread_unused_heap_start = NULL;


/*
 * End point for the thread heap memory area.
 */
__thread void *thread_heap_end = NULL;



/*
  Aligns pointer to 8 byte address.
  params : pointer to align.
  returns: a pointer (aligned to 8 bytes)
 */
void * align8(void *x);




/*
 *  returns the pointer to elated bin based on the size.
 *  params: size of bin.
 *  returns: pointer to bin corresponding size.
 */
block_info ** get_bin(size_t size);




/*
 * Allocate memory from heap area. For memory request of sizes < 512, chunks are
 * allocated from heap.
 * params : size.
 * returns: pointer to allocated area.
 */
void * heap_allocate(size_t size);




/*
 * Finds best fit block from bin_large. On memory request > 512, first
 * it is checked if any of large free memory chunks fits to request.
 *
 * params: size of block to allocate.
 * returns: pointer to best fitting block, NULL on failure.
 */
void * find_best_fit_from_bin_large(size_t size);



/*
 * maps new memory address using mmap system call for size request > 512 bytes.
 * params: requested size in bytes.
 * returns: pointer to block allocated., NULL on failure.
 */
void * mmap_new_memory(size_t size);



/*
 * Performs allocation for request > 512 bytes.
 * params : requested memory size.
 * returns: pointer to allocated memory. NULL on failure.
 */
void * alloc_large(size_t size);




/*
 *  Creates a memory block from unused heap.
 *  params: requested memory size in bytes.
 *  returns: pointer to allocated memory chunk. NULL on failure.
 */
void * block_from_unused_heap(size_t size);




/*
 * Allocates the memory.
 */
void * mymalloc(size_t);




/*
 * Free up the memory allocated at pointer p. It appends the block into free
 * list.
 * params: address to pointer to be freed.
 * returns: NONE.
 */
void myfree(void *p);




/*
 * Allocate size of size bytes for nmemb. Initialize with null bytes.
 * params: total number of elements of size 'size' to be allocated. and size
 *         to allocate.
 * returns: pointer to allocated memory on success. NULL on failure.
 */
void * calloc(size_t nmemb, size_t size);




/*
 * reallocates the pointer with new size size and copies the old data to new
 * location.
 * params: pointer to reallocate. and new size.
 * returns: pointer to new allocated memory chunk.
 */
void * realloc(void *ptr, size_t size);




/* aligns memory.
 */
void * memalign(size_t alignment, size_t s);




/*
 * Prints malloc stats like number of free blocks, total number of memory
 * allocated.
 */
void malloc_stats();

void abortfn(enum mcheck_status status);
#endif


/*
  Aligns pointer to 8 byte address.
  params : pointer to align.
  returns: a pointer (aligned to 8 bytes)
 */
void *align8(void *x)
{
    unsigned long p = (unsigned long)x;
    p = (((p - 1) >> 3) << 3) + 8;
    return (void *)p;
}


/*
 *  returns the pointer to elated bin based on the size.
 *  params: size of bin.
 *  returns: pointer to bin corresponding size.
 */
block_info** get_bin(size_t size)
{
    switch(size)
    {
       case 8   : return &bin_8;
       case 64  : return &bin_64;
       case 512 : return &bin_512;
       default  : return &bin_large;
    }
}


/*
 *  Creates a memory block from unused heap.
 *  params: requested memory size in bytes.
 *  returns: pointer to allocated memory chunk. NULL on failure.
 */
void * block_from_unused_heap(size_t size)
{
    /*If thread heap is not initialized or if available free size is less
      than the block for requested size.*/
    if(NULL == thread_unused_heap_start ||
       (thread_heap_end - thread_unused_heap_start) <
           (size + sizeof(block_info)))
    {

        /*If heap of process is not initialized or available free size of general
          heap is less than the block for requested size.*/
        if(NULL == heap_used_memory_end ||
            (sbrk(0) - heap_used_memory_end) < (size + sizeof(block_info)))
        {
            /*If heap is not initialized.*/
            if(NULL == heap_used_memory_end)
            {
                heap_used_memory_end = sbrk(0);
                if(heap_used_memory_end == (void*) -1)
                {
                    errno = ENOMEM;
                    perror("\n sbrk(0) failed.");
                    return NULL;
                }
            }

            align8(heap_used_memory_end);
        }

        // extend heap, return NULL on failure.
        if(sbrk(sysconf(_SC_PAGESIZE) * 100) == (void *) -1)
        {
            errno = ENOMEM;
            perror("\n sbrk failed to extend heap.");
            return NULL;
        }

        /*If there is smaller chunk remaining, add to free list of a bin.
          to minimize the wastage of memory.*/
        /*if(NULL != thread_unused_heap_start)
        {
            // TODO: add_chunk_to_bin(); possible optimization.
        }*/

        /*create fresh heap of 1 page size. for a thread.*/
        thread_unused_heap_start = heap_used_memory_end;
        thread_heap_end =
            heap_used_memory_end + (sysconf(_SC_PAGESIZE));
        heap_used_memory_end =  thread_heap_end;
    }

    block_info b;
    b.size = size;
    b.next = NULL;

    memcpy(thread_unused_heap_start, &b, sizeof(block_info));
    thread_unused_heap_start += (sizeof(block_info) + size);

    // update stats variables.
    pthread_mutex_lock(&stats_mutex);
    total_number_of_blocks++;
    total_arena_size_allocated += size;
    pthread_mutex_unlock(&stats_mutex);

    return (thread_unused_heap_start - size);
}




/*
 * Allocate memory from heap area. For memory request of sizes < 512, chunks are
 * allocated from heap.
 * params : size.
 * returns: pointer to allocated area.
 */
void *heap_allocate(size_t size)
{

   block_info **bin = get_bin(size);
   void * ret = NULL;
   /* reuse memory block from heap bins if available*/
   if(NULL != *bin)
   {
       block_info *p = *bin;
       *bin =  p->next;
       p->next = NULL;

       pthread_mutex_lock(&stats_mutex);
       total_free_blocks--;
       pthread_mutex_unlock(&stats_mutex);
       ret = (void *)((char*)p + sizeof(block_info));
   }
   else  //request new memory or slice out remaining unused memory.
   {     pthread_mutex_lock(&global_heap_mutex);
         ret =  block_from_unused_heap(size);
         pthread_mutex_unlock(&global_heap_mutex);
   }

   return ret;
}



/*
  Finds best fitting block from large memory bin.
*/
void * find_best_fit_from_bin_large(size_t size)
{
    block_info *b = bin_large;
    block_info *best_fit = NULL;
    int min_fit_size = INT_MAX;
    void *ret = NULL;

    while(b != NULL)
    {
         if(b->size >= size && b->size < min_fit_size)
         {
            best_fit = b;
            min_fit_size = b->size;
         }
         b = b->next;
    }

    /*If best fit found, update list*/
    if(NULL != best_fit)
    {
        // if best_fit is first block.
        if (best_fit == bin_large)
        {
           bin_large = bin_large->next;
           best_fit->next = NULL;
           ret = (void *)((void *)best_fit + sizeof(block_info));
        }
        else
        {
          b = bin_large;
          while(b != NULL && b->next != best_fit)
          {
            b = b->next;
          }
          if(b != NULL)
          {
             b->next = best_fit->next;
          }
          best_fit->next = NULL;
          ret = (void *)((void *)best_fit + sizeof(block_info));
        }
    }

    return ret;
}




/*
 * maps new memory address using mmap system call for size
 * request > 512 bytes.
 * Requests kernel to map new memory at some place decided by kernel.
 * params: requested size in bytes.
 * returns: pointer to block allocated., NULL on failure.
 */
void * mmap_new_memory(size_t size)
{
    int num_pages =
        ((size + sizeof(block_info) - 1)/sysconf(_SC_PAGESIZE)) + 1;
    int required_page_size = sysconf(_SC_PAGESIZE) * num_pages;

    void *ret = mmap(NULL, // let kernel decide.
                     required_page_size,
                     PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS| MAP_PRIVATE,
                     -1, //no file descriptor
                     0); //offset.

    block_info b;
    b.size = (required_page_size - sizeof(block_info));
    b.next = NULL;

    ret = memcpy(ret, &b, sizeof(block_info));
    ret = ((char*)ret + sizeof(block_info));

    // update stats variables.
    pthread_mutex_lock(&stats_mutex);
    total_mmap_size_allocated += size;
    pthread_mutex_unlock(&stats_mutex);

    return ret;
}


/*
 * Performs allocation for request > 512 bytes.
 * params : requested memory size.
 * returns: pointer to allocated memory. NULL on failure.
 */
void *alloc_large(size_t size)
{
   void * ret = NULL;
   if(NULL != bin_large)
   {
       //pthread_mutex_lock(&global_heap_mutex);
       ret = find_best_fit_from_bin_large(size);
      // pthread_mutex_unlock(&global_heap_mutex);
   }

   /*either bin_large is empty or no best fit was found.*/
   if(ret == NULL)
   {
       //pthread_mutex_lock(&global_heap_mutex);
       ret = mmap_new_memory(size);
      // pthread_mutex_unlock(&global_heap_mutex);
   }
    return ret;
}


/*
 * Allocates the memory.
 */
void* mymalloc(size_t size)
{
     pthread_mutex_lock(&stats_mutex);
     total_allocation_request++;
     pthread_mutex_unlock(&stats_mutex);
     pthread_mutex_lock(&test);
     void * ret = NULL;

     if(size < 0)
     {
        perror("\n Invalid memory request.");
	pthread_mutex_unlock(&test);
        return NULL;
     }

     // allocate from either large bin or mmap.
     if(size > 512)
     {  //printf("Alloc large\n");
        ret = alloc_large(size);
     }
     else
     {
       size = (size <= 8)? 8 : ((size<=64)? 64: 512);
       ret = heap_allocate(size);
     }
     pthread_mutex_unlock(&test);
     return ret;
}



/*
 * Free up the memory allocated at pointer p. It appends the block into free
 * list.
 * params: address to pointer to be freed.
 * returns: NONE.
 */
void myfree(void *p)
{
   //update stats variables.
   pthread_mutex_lock(&stats_mutex);
   total_free_request++;
   total_free_blocks++;
   pthread_mutex_unlock(&stats_mutex);
   pthread_mutex_lock(&test);
   if(NULL != p)
   {
      block_info *block  = (block_info *)(p - sizeof(block_info));
      memset(p, '\0', block->size);

      block_info **bin = get_bin(block->size);
      block_info *check_bin = *bin;

      // already freed?
      while(check_bin != NULL)
      {
         if(check_bin == block)
         {
	    pthread_mutex_unlock(&test);
            return;
         }
         check_bin = check_bin->next;
      }

      // attach as head to free list of corresponding bin.
      block->next = *bin;
      *bin = block;
    }
    pthread_mutex_unlock(&test);
}


/*similar to calloc of glibc */
void *calloc(size_t nmemb, size_t size)
{
     void *p = mymalloc(nmemb * size);
     block_info *b = (block_info *)(p - sizeof(block_info));
     memset(p, '\0', b->size);
     return p;
}




/*
 * Allocate size of size bytes for nmemb. Initialize with null bytes.
 * params: total number of elements of size 'size' to be allocated.
 *         and size to allocate.
 * returns: pointer to allocated memory on success. NULL on failure.
 */void *realloc(void *ptr, size_t size)
{
    if(NULL == ptr)
    {
       return mymalloc(size);
    }
    void *newptr = mymalloc(size);

    if(NULL == newptr)
    {
        return NULL;
    }
    block_info *old_block =
        (block_info *)((char*)ptr - sizeof(block_info));

    memcpy(newptr, ptr, old_block->size);

    myfree(ptr);

    return newptr;
}

void *memalign(size_t alignment, size_t s)
{
    return heap_used_memory_end;
}


void malloc_stats()
{
    printf("\n -- malloc stats--\n");
    printf("\n total_arena_size_allocated : %lu", total_arena_size_allocated);
    printf("\n total_mmap_size_allocated  : %lu", total_mmap_size_allocated);
    printf("\n total_number_of_blocks     : %lu", total_number_of_blocks);
    printf("\n total_allocation_request   : %lu", total_allocation_request);
    printf("\n total_free_request         : %lu", total_free_request);
    printf("\n total_free_blocks          : %lu\n", total_free_blocks);
}
