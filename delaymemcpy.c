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
  
} pending_copy_t;

/* First element of the pending copy linked list */
static pending_copy_t pending_copies[MAX_PENDING_COPIES] = {};

/* Global variable to keep the current page size. Initialized in
   initialize_delay_memcpy_data.
 */
static long page_size = 0;

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

  /* TO BE COMPLETED BY THE STUDENT */
}

/* Returns TRUE (non-zero) if the address 'ptr' is in the same page as
   any address in the range of addresses that starts at 'start' and
   has 'end' bytes (i.e., ends at 'start+end-1'). In other words,
   returns TRUE if the address is in the range [start, start+end-1],
   or is in the same page as either start or start+end-1. Returns
   FALSE (zero) otherwise.
 */
static int address_in_page_range(void *start, size_t size, void *ptr) {
  
  /* TO BE COMPLETED BY THE STUDENT */
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
  
  pending_copy_t *pend = get_pending_copy(info->si_addr);
  if (pend == NULL) {
    write(2, "Segmentation fault!\n", 20);
    raise(SIGKILL);
  }
  
  /* TO BE COMPLETED BY THE STUDENT */
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
  
  /* TO BE COMPLETED BY THE STUDENT */
  
  return dst;
}
