#include "delaymemcpy.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

#define MAX_PENDING_COPIES 20

/* Data structure use to keep a list of pending memory copies. */
typedef struct pending_copy {
  
  /* Source, destination and size of the memory regions involved in the copy */
  void *src;
  void *dst;
  size_t size;
  int count;
  
} pending_copy_t;

/* First element of the pending copy linked list */
static pending_copy_t pending_copies[MAX_PENDING_COPIES] = {};

/* Global variable to keep the current page size. Initialized in
   initialize_delay_memcpy_data.
 */
static long page_size = 0;

// returns the number of pages within a pending object
int count_pages(pending_copy_t *pend) {
  int count = 0;
  intptr_t page_index = page_start(pend->src);
  while (page_index < page_start(pend->src + pend->size) + page_size) {
    page_index += page_size;
    count ++;
  }
  return count;
}

/* Returns the pointer to the start of the page that contains the
   specified memory address.
 */
static void *page_start(void *ptr) {
  
  return (void *) (((intptr_t) ptr) & -page_size);
}

/* Returns TRUE (non-zero) if the address 'ptr' is in the range of
   addresses that starts at 'start' and has 'end' bytes (i.e., ends at
   'start+end-1'). Returns FALSE (zero) otherwise.
 */
static int address_in_range(void *start, size_t size, void *ptr) {

  if ((ptr < start) || (ptr > start + size - 1)) {
    return 0;
  } 
  else return 1;
}

/* Returns TRUE (non-zero) if the address 'ptr' is in the same page as
   any address in the range of addresses that starts at 'start' and
   has 'end' bytes (i.e., ends at 'start+end-1'). In other words,
   returns TRUE if the address is in the range [start, start+end-1],
   or is in the same page as either start or start+end-1. Returns
   FALSE (zero) otherwise.
 */
static int address_in_page_range(void *start, size_t size, void *ptr) {
  
return address_in_range(page_start(ptr), page_start(start + size) + page_size - page_start(ptr), ptr);
}

/* mprotect requires the start address to be aligned with the page
   size. This function calls mprotect with the start of the page that
   contains ptr, and adjusts the size accordingly to include the extra
   bytes required for that adjustment.
 */
static int mprotect_full_page(void *ptr, size_t size, int prot) {
  
  void *page = page_start(ptr);
  return mprotect(page, size + (ptr - page), prot);
}

/* Adds a pending copy object to the list of pending copies. If the
   number of pending copies is larger than the maximum number allowed,
   returns 0, otherwise returns 1.
 */
static int add_pending_copy(void *dst, void *src, size_t size) {

  int i;
  for (i = 0; i < MAX_PENDING_COPIES; i++) {
    if (!pending_copies[i].src) {
      pending_copies[i].src = src;
      pending_copies[i].dst = dst;
      pending_copies[i].size = size;
      pending_copies[i].count = 0;
      return 1;
    }
  }
  return 0;
}

/* Returns the first pending copy object that contains the specified
   address, or NULL if no such object exists in the list. The
   specified address may be in either the source or destination range,
   or even outside one of these ranges, but in the same page in the
   page table.
 */
static pending_copy_t *get_pending_copy(void *ptr) {

  int i;
  for (i = 0; i < MAX_PENDING_COPIES; i++) {
    pending_copy_t *pend = &pending_copies[i];
    if (pend->src &&
	(address_in_page_range(pend->src, pend->size, ptr) ||
	 address_in_page_range(pend->dst, pend->size, ptr)))
      return pend;
  }
  return NULL;
}

/* Segmentation fault handler. If the address that caused the
   segmentation fault (represented by info->si_addr) is part of a
   pending copy, this function will perform the copy for the page that
   contains the address. The pending copy object corresponding to the
   address should be removed from the list, but if the pending copy
   involves more than one page, the remaining pages (before and after
   the affected page) will be re-added to the list. If the address is
   not part of a pending copy page, the process will write a message
   to the standard error process and kill the process.
 */
static void delay_memcpy_segv_handler(int signum, siginfo_t *info, void *context) {

  void *ptr = info->si_addr;
  pending_copy_t *pend = get_pending_copy(ptr);

  if (pend == NULL) {
    write(2, "Segmentation fault!\n", 20);
    raise(SIGKILL);
  } else {
      // case 1: ptr is in page that contains src entirely
      if (address_in_page_range(pend->src, 0, ptr) && address_in_page_range(pend->src + pend->size, 0, ptr)) {
         mprotect_full_page(page_start(pend->src), 1, PROT_READ|PROT_WRITE);
         mprotect_full_page(page_start(pend->dst), 1, PROT_READ|PROT_WRITE);
         memcpy(pend->dst, pend->src, pend->size);
         pend->src = NULL;
         // case 2: ptr is in page that contains dst entirely
      } else if (address_in_page_range(pend->dst, 0, ptr) && address_in_page_range(pend->dst + pend->size, 0, ptr)) {
        mprotect_full_page(page_start(pend->src), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(pend->dst), 1, PROT_READ|PROT_WRITE);
        memcpy(pend->dst, pend->src, pend->size);
        pend->src = NULL;
        // case 3: ptr is in first page of src
      } else if (address_in_page_range(pend->src, 0, ptr)) {
        mprotect_full_page(page_start(pend->src), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(pend->dst), 1, PROT_READ|PROT_WRITE);
        memcpy(pend->dst, pend->src, page_start(ptr) + page_size - pend->src);
        pend->count++;
        if (pend->count == count_pages(pend)) {
          pend->src = NULL;
        }
        // case 4: ptr is in first page of dst
      } else if (address_in_page_range(pend->dst, 0, ptr)) {
        mprotect_full_page(page_start(pend->src), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(pend->dst), 1, PROT_READ|PROT_WRITE);
        memcpy(pend->dst, pend->src, page_start(ptr) + page_size - pend->dst);
        pend->count++;
        if (pend->count == count_pages(pend)) {
          pend->src = NULL;
        }
        // case 5: ptr is in last page of src
      } else if (address_in_page_range(pend->src + pend->size, 0, ptr)) {
        mprotect_full_page(page_start(pend->src + pend->size), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(pend->dst + pend->size), 1, PROT_READ|PROT_WRITE);
        memcpy(page_start(pend->dst + pend->size), page_start(ptr), pend->src + pend->size - page_start(ptr));
        pend->count++;
        if (pend->count == count_pages(pend)) {
          pend->src = NULL;
        }
        // case 6: ptr is in last page of dst
      } else if (address_in_page_range(pend->dst + pend->size, 0, ptr)) {
        mprotect_full_page(page_start(pend->src + pend->size), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(pend->dst + pend->size), 1, PROT_READ|PROT_WRITE);
        memcpy(page_start(ptr), page_start(pend->src + pend->size), pend->dst + pend->size - page_start(ptr));
        pend->count++;
        if (pend->count == count_pages(pend)) {
          pend->src = NULL;
        }
        // case 7: ptr is within range of src, but not on first or last page
      } else if (address_in_range(pend->dst, pend->size, ptr)) {
        mprotect_full_page(page_start(ptr), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(ptr - pend->src + pend->dst), 1, PROT_READ|PROT_WRITE);
        memcpy(page_start(ptr), page_start(ptr) - pend->src + pend->dst, page_size);
        pend->count++;
        if (pend->count == count_pages(pend)) {
          pend->src = NULL;
        }
        // case 8: ptr is within range of dst, but not on first or last page
      } else if (address_in_range(pend->src, pend->size, ptr)) {
        mprotect_full_page(page_start(ptr), 1, PROT_READ|PROT_WRITE);
        mprotect_full_page(page_start(ptr - pend->dst + pend->src), 1, PROT_READ|PROT_WRITE);
        memcpy(page_start(ptr) - pend->dst + pend->src, page_start(ptr), page_size);
        pend->count++;
        if (pend->count == count_pages(pend)) {
          pend->src = NULL;
        }
      } 
  } 
}

/* Initializes the data structures and global variables used in the
   delay memcpy process. Changes the signal handler for segmentation
   fault to the handler used by this process.
 */
void initialize_delay_memcpy_data(void) {

  struct sigaction sa;
  sa.sa_sigaction = delay_memcpy_segv_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);

  page_size = sysconf(_SC_PAGESIZE);
}

/* Starts the copying process of 'size' bytes from 'src' to 'dst'. The
   actual copy of data is performed in the signal handler for
   segmentation fault. This function only stores the information
   related to the copy in the internal data structure, and protects
   the pages (source as read-only, destination as no access) so that
   the signal handler is invoked when the copied data is needed. If
   the maximum number of pending copies is reached, the copy is
   performed immediately. Returns the value of dst.
 */
void *delay_memcpy(void *dst, void *src, size_t size) {

  if(!add_pending_copy(dst, src, size)) {
    memcpy(dst, src, size);
  } else {
    mprotect_full_page(src, size, PROT_READ);
    mprotect_full_page(dst, size, PROT_NONE);
  }
  return dst;
}
