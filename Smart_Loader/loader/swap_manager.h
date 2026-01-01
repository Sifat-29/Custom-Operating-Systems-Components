#ifndef SWAP_MANAGER_H
#define SWAP_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

/* Structure to track swapped pages */
typedef struct SwapEntry {
    void *vaddr;            // Virtual address of the page
    int swap_offset;        // Offset in the swap file
    int is_active;          // 1 if valid, 0 if empty
} SwapEntry;

/* Public Function Prototypes */
void init_swap_system(long max_pages);
void handle_page_eviction_to_swap(void *page_addr);
int load_from_swap_if_exists(void *page_addr);
void cleanup_swap_system();

#endif