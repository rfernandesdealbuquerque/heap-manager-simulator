#include <stdio.h>  // needed for size_t etc.
#include <unistd.h> // needed for sbrk etc.
#include <sys/mman.h> // needed for mmap
#include <assert.h> // needed for asserts
#include "dmm.h"

/* 
 * The lab handout and code guide you to a solution with a single free list containing all free
 * blocks (and only the blocks that are free) sorted by starting address.  Every block (allocated
 * or free) has a header (type metadata_t) with list pointers, but no footers are defined.
 * That solution is "simple" but inefficient.  You can improve it using the concepts from the
 * reading.
 */

/* 
 *size_t is the return type of the sizeof operator.   size_t type is large enough to represent
 * the size of the largest possible object (equivalently, the maximum virtual address).
 */

typedef struct metadata {
  size_t size;
  struct metadata* next;
  struct metadata* prev;
} metadata_t;

/*
 * Head of the freelist: pointer to the header of the first free block.
 */

static metadata_t* freelist = NULL;
static metadata_t* heap_head = NULL;

/*
* return address of first free block that fits
* return NULL if there is no block that fits
*/
metadata_t* findFirstFitFreeBlock(metadata_t* freelist, size_t requiredSizeAligned){
    metadata_t* trav = freelist;
    while(trav != NULL){
      if(trav->size >= requiredSizeAligned + METADATA_T_ALIGNED){
        return trav;
      }
      else{
        trav = trav->next;
      }
    }

    return NULL;

}

//this function assumes that free list is always sorted by address
//it coalesces all possible blocks to be coalesced in a single run through the list
void coalesceFreeBlocks(metadata_t* freelist){
  metadata_t* trav = freelist;

  while(trav->next != NULL){
    
    metadata_t* headerOfNextBlock = (char*) trav + METADATA_T_ALIGNED + trav->size;
    bool foundTwoContiguosFreeMemoryBlocks = (headerOfNextBlock == trav->next); 

    if(foundTwoContiguosFreeMemoryBlocks){
      metadata_t* blockToCoalesce = trav->next;
      trav->size = trav->size + METADATA_T_ALIGNED + blockToCoalesce->size;
      
      trav->next = blockToCoalesce->next;
      if(blockToCoalesce->next != NULL){
        (blockToCoalesce->next)->prev = trav;
      }
      blockToCoalesce->next = NULL;
      blockToCoalesce->prev = NULL;
      //now coalesced but don't advance trav yet because there might be another contiguous block to coalesce
    }
    else{
      trav = trav->next;

    }
  }
}

void* dmalloc(size_t numbytes) {
  //DEBUG("-----Before MALLOC---- %u", (size_t) ALIGN(numbytes));
  //print_freelist();

  size_t numBytesAligned = (size_t) ALIGN(numbytes);

  if(freelist == NULL) {
    if(!dmalloc_init()) { //dmalloc_init is successful we don't enter here
      return NULL;
    }
  }

  assert(numbytes > 0);

  /* your code here */
  metadata_t* myBlock = findFirstFitFreeBlock(freelist, numBytesAligned);

  if(myBlock == NULL){ //in case couldn't find suitable free block
    return NULL;
  }
  
  else{
    metadata_t* myNewFreeBlock = (char*) myBlock + METADATA_T_ALIGNED + numBytesAligned; 
    
    if(myBlock->prev == NULL){ //this means that freelist itself was our first fit block, so we need to update it to myNewFreeBlock which becomes the new freelist
      freelist = myNewFreeBlock;
    }

    if(myBlock->prev != NULL){ 
      (myBlock->prev)->next = myNewFreeBlock;
    }

    // if(numBytesAligned == 5120){
    //   DEBUG("myBlock->size: %u METADATA_T_ALIGNED: %u numBytesAligned: %u", myBlock->size, METADATA_T_ALIGNED, numBytesAligned);
    // }
    
    myNewFreeBlock->size = myBlock->size - METADATA_T_ALIGNED - numBytesAligned;
    myNewFreeBlock->prev = myBlock->prev;
    myNewFreeBlock->next = myBlock->next;

    if(myBlock->next != NULL){
      (myBlock->next)->prev = myNewFreeBlock;
    }

    myBlock->size = numBytesAligned;
    myBlock->prev = NULL;
    myBlock->next = NULL;

    //DEBUG("-----After MALLOC----- %u", (size_t) ALIGN(numbytes));
    //print_freelist();
    return (char*) myBlock + METADATA_T_ALIGNED;
  }
}

void dfree(void* ptr) {
  /* your code here */
  // DEBUG("-----Before FREE----- %p", (metadata_t*) (ptr - METADATA_T_ALIGNED));
  // print_freelist();

  metadata_t* trav = freelist;
  metadata_t* ptrToFree = (metadata_t*) (ptr - METADATA_T_ALIGNED);

  bool FreelistIsOneNode = trav->next == NULL && trav->prev == NULL;

  if(FreelistIsOneNode && ptrToFree > trav){ //covers one node insert after trav
    trav->next = ptrToFree; 
    ptrToFree->prev = trav;
    ptrToFree->next = NULL;
    trav = NULL;
  }

  while(trav != NULL){
    bool TravIsFirstNode = trav->prev == NULL;
    bool TravIsLastNode = trav->next == NULL;

    if(trav < ptrToFree && !TravIsLastNode){
      trav = trav->next;
    }

    else if(trav < ptrToFree && TravIsLastNode){
      trav->next = ptrToFree;
      ptrToFree->prev = trav;
      ptrToFree->next = NULL;
    }

    else if(trav == ptrToFree){
      trav = NULL;
    }

    else{ //insert into free list while keeping sorted
      if(TravIsFirstNode){
        trav->prev = ptrToFree;
        ptrToFree->prev = NULL;
        ptrToFree->next = trav;
        freelist = ptrToFree;
        trav = NULL;
      }

      else if(!TravIsFirstNode){
        (trav->prev)->next = ptrToFree;
        ptrToFree->prev = trav->prev;
        ptrToFree->next = trav;
        trav->prev = ptrToFree;
        trav = NULL;
      }
    }
  }
  coalesceFreeBlocks(freelist);

  // DEBUG("-----After FREE----- %p", ptrToFree);
  // print_freelist();
  
}
/*
 * Allocate heap_region slab with a suitable syscall.
 */
bool dmalloc_init() {

  size_t max_bytes = ALIGN(MAX_HEAP_SIZE);

  /*
   * Get a slab with mmap, and put it on the freelist as one large block, starting
   * with an empty header.
   */
  freelist = (metadata_t*)
     mmap(NULL, max_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (freelist == (void *)-1) {
    perror("dmalloc_init: mmap failed");
    return false;
  }
  freelist->next = NULL;
  freelist->prev = NULL;
  freelist->size = max_bytes-METADATA_T_ALIGNED;

  heap_head = freelist; //pointer to the beginning of the heap

  return true;
}


/* for debugging; can be turned off through -NDEBUG flag*/


//This code is here for reference.  It may be useful.
//Warning: the NDEBUG flag also turns off assert protection.



void print_freelist() {
  metadata_t *freelist_head = freelist;
  while(freelist_head != NULL) {
    DEBUG("\tFreelist Size:%zd, Head:%p, Prev:%p, Next:%p\t",
	  freelist_head->size,
	  freelist_head,
	  freelist_head->prev,
	  freelist_head->next);
    freelist_head = freelist_head->next;
  }
  DEBUG("\n");
}

