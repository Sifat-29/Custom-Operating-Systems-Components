#include "swap_manager.h"
#include "loader.h"

#define MIN_SWAP_ENTRIES       1024    /* Minimum number of swap entries */
#define MAX_ENTRY_MULTIPLIER   5       /* Multiple of Max Pages Allowed (high for safety) */

#define IMAGE_FILE_NAME        "swap.img"

/* Private Global Variables for Swapping */
static int SWAP_FD = -1;
static SwapEntry *SWAP_TABLE = NULL;
static int MAX_SWAP_ENTRIES = 0;

void init_swap_system(long max_pages) {
    MAX_SWAP_ENTRIES = (int)max_pages * MAX_ENTRY_MULTIPLIER;
    if (MAX_SWAP_ENTRIES < MIN_SWAP_ENTRIES) MAX_SWAP_ENTRIES = MIN_SWAP_ENTRIES;

    SWAP_TABLE = (SwapEntry*) calloc(MAX_SWAP_ENTRIES, sizeof(SwapEntry));
    if (!SWAP_TABLE) {
        perror("Failed to allocate swap table");
        exit(1);
    }

    /* Open/Create the swap file */
    SWAP_FD = open(IMAGE_FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (SWAP_FD == -1) {
        perror("Failed to create swap file");
        exit(1);
    }
}

void handle_page_eviction_to_swap(void *page_addr) {
    int seg_idx = find_segment_of_fault(page_addr);
    
    if (seg_idx != -1) {
        if ((PHDRS_OF_SEGMENTS_TO_LOAD[seg_idx].p_flags & PF_W) == 0) {    /* Identification of read-only page */
            return;     /* No need to add to swap, can be read from the elf only */
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_SWAP_ENTRIES; i++) {
        /* Finding the slot of this addr (if exists) or the first available */
        if (SWAP_TABLE[i].is_active && SWAP_TABLE[i].vaddr == page_addr) {
            slot = i;
            break;
        }
        if (!SWAP_TABLE[i].is_active && slot == -1) {
            slot = i;
        }
    }

    if (slot == -1) {
        printf("ERROR: Swap table full. Increase MAX_SWAP_ENTRIES.\n");
        exit(1);
    }

    /* Writing to disk */
    off_t offset = slot * PAGE_SIZE_IN_BYTES;
    if (lseek(SWAP_FD, offset, SEEK_SET) == -1) {
        perror("Swap lseek failed"); 
        exit(1);
    }
    
    if (write(SWAP_FD, page_addr, PAGE_SIZE_IN_BYTES) != PAGE_SIZE_IN_BYTES) {
        perror("Swap write failed");
        exit(1);
    }

    SWAP_TABLE[slot].vaddr = page_addr;
    SWAP_TABLE[slot].swap_offset = offset;
    SWAP_TABLE[slot].is_active = 1;
}

int load_from_swap_if_exists(void *page_addr) {
    for (int i = 0; i < MAX_SWAP_ENTRIES; i++) {
        if (SWAP_TABLE[i].is_active && SWAP_TABLE[i].vaddr == page_addr) {
            
            if (lseek(SWAP_FD, SWAP_TABLE[i].swap_offset, SEEK_SET) == -1) return 0;
            
            if (read(SWAP_FD, page_addr, PAGE_SIZE_IN_BYTES) != PAGE_SIZE_IN_BYTES) {
                perror("Swap read failed");
                exit(1);
            }

            return 1;    /* Found in swap */
        }
    }
    return 0;    /* Not found in swap */ 
}

void cleanup_swap_system() {
    if (SWAP_TABLE) free(SWAP_TABLE);
    if (SWAP_FD != -1) {
        close(SWAP_FD);
        remove("swap.img");    /* Deleting the file */
    }
}