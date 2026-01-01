#include <stdio.h>
#include <time.h>
#include <elf.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>

#define PAGE_SIZE_IN_BYTES 4096     /* Cannot be changed due to internal OS workings */

extern Elf32_Phdr *PHDRS_OF_SEGMENTS_TO_LOAD;
extern int NUMBER_OF_SEGMENTS_TO_LOAD;
int find_segment_of_fault(void *fault_addr);


void load_and_run_elf(char** exe);
void loader_cleanup();


/* Methods specific to FIFO replacement algo */
void add_page_fifo(void *page_start_addr);
void init_fifo(long max_pages);
void cleanup_fifo();
long page_evictions_fifo();


/* Methods specific to Random replacement algo */
void add_page_random(void *page_start_addr);
void init_random(long max_pages);
void cleanup_random();
long page_evictions_random();
