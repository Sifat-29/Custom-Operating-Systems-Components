#include "replacement_algos.h"
#include "swap_manager.h"


/* ============================================================================================== */
/*                            Global definitions and variables                                    */
/* ============================================================================================== */

#define FIFO_REPLACEMENT_MODE 0
#define RANDOM_REPLACEMENT_MODE 1
#define UNDEFINED_REPLACEMENT_MODE -1

Elf32_Ehdr *EHDR;
Elf32_Phdr *PHDR;
int FD;

Elf32_Phdr *PHDRS_OF_SEGMENTS_TO_LOAD = NULL;
int *PAGES_ALLOCED_TO_SEGMENT = NULL;
int NUMBER_OF_SEGMENTS_TO_LOAD = 0;

int PAGE_FAULTS = 0;
int PAGE_ALLOCATIONS = 0;
long TOTAL_INTERNAL_FRAGMENTATION = 0;

int MODE;




/* ============================================================================================== */
/*                                   Function Declarations                                        */
/* ============================================================================================== */

/* Replace algo methods */
void (*ADD_PAGE)(void *);
void (*INIT_REPLACEMENT_ALGO)(long);
void (*CLEANUP_REPLACEMENT)();
long (*GET_NUMBER_OF_PAGE_EVICTIONS)();


void load_and_run_elf(char** exe);

void assign_replacement_mode(char *mode);

void initialise_global_data_structures(char **exe);

void setup_signal_handler();

void segfault_handler(int sig, siginfo_t *info, void *context);

int find_segment_of_fault(void *fault_addr);

void allocate_page(int idx_of_segment_having_fa, void* fault_addr);

long calculate_page_waste(void* page_addr);

void calculate_internal_fragmentation();

void print_stats();

void loader_cleanup();



/* ============================================================================================== */
/*                                           Functions                                            */
/* ============================================================================================== */

/* Load and run the ELF executable file */
void load_and_run_elf(char** exe) {
    /* Assigning fucntion pointers according to the replacement mode chosen by the user */
    assign_replacement_mode(exe[2]);

    /* Initializing SEGMENTS array with the all the info of the phdrs of type load */
    initialise_global_data_structures(exe);

    /* Custom signal handler for SIGSEGV */
    setup_signal_handler();

    /* Address of entry point */
    void *entry = (void*) EHDR->e_entry;

    int (*start_func)(void) = (int (*)(void))entry;
    int result = start_func();
    printf("\n-----------------------------------------------------------------------------\n");
    printf("------------------------- User executable result ----------------------------\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("User _start return value = %d\n",result);

    print_stats();
}


void assign_replacement_mode(char *mode) {

    if (strcmp("RANDOM", mode) == 0) {
        MODE = RANDOM_REPLACEMENT_MODE;
        ADD_PAGE = add_page_random;
        INIT_REPLACEMENT_ALGO = init_random;
        CLEANUP_REPLACEMENT = cleanup_random;
        GET_NUMBER_OF_PAGE_EVICTIONS = page_evictions_random;
    } 
    else {
        if (strcmp("FIFO", mode) == 0) MODE = FIFO_REPLACEMENT_MODE;
        else MODE = UNDEFINED_REPLACEMENT_MODE;
        ADD_PAGE = add_page_fifo;
        INIT_REPLACEMENT_ALGO = init_fifo;
        CLEANUP_REPLACEMENT = cleanup_fifo;
        GET_NUMBER_OF_PAGE_EVICTIONS = page_evictions_fifo;
    }
}



/* Initialises the required global data structures for loading the executable */
void initialise_global_data_structures(char **exe) {

    /* Allocating memory for global pointers to the ELF Header and current Program Header */
	EHDR = (Elf32_Ehdr*) malloc(sizeof(Elf32_Ehdr));
	if (EHDR == NULL) {printf("Memory Allocation for ELF header failed\n"); exit(2);}
	PHDR = (Elf32_Phdr*) malloc(sizeof(Elf32_Phdr));
	if (PHDR == NULL) {printf("Memory Allocation for Program header failed\n"); exit(2);}
  

    FD = open(exe[1], O_RDONLY);
    if (FD == -1) {printf("File opening failed\n"); exit(2);}

	// Read ELF header
	ssize_t read_bytes_header = read(FD, EHDR, sizeof(Elf32_Ehdr));
	if (read_bytes_header != sizeof(Elf32_Ehdr)) {
		printf("Unable to load header, bytes read = %zd, expected %d\n", read_bytes_header, sizeof(Elf32_Ehdr));
        exit(1);
	}

	// Information for iterating in PHT
    unsigned int phoffset = EHDR->e_phoff;
    unsigned short phnumber = EHDR->e_phnum;
    unsigned short phsize = EHDR->e_phentsize;

    /* Count number of PT_LOAD Segments (Stored in NUMBER_OF_SEGMENTS) */
    for (int i = 0; i < phnumber; i++) {
        if (lseek(FD, phoffset + i * phsize, SEEK_SET) == -1) {
			printf("Error in getting fd to phoffset + i*phsize\n");
			printf("phoffset: %i, i: %d, phsize: %hu\n", phoffset, i, phsize);
			exit(2);
		}

        ssize_t bytes_read_phdr = read(FD, PHDR, phsize);
        if (bytes_read_phdr != phsize) {
            printf("Unable to read full program header %d: got %zd bytes, expected %hu\n", i, bytes_read_phdr, phsize);
			exit(2);
        }

        if (PHDR->p_type == PT_LOAD) NUMBER_OF_SEGMENTS_TO_LOAD++;
    }

    /* Allocate memory depending on number of segments for the SEGMENTS array */
    PHDRS_OF_SEGMENTS_TO_LOAD = (Elf32_Phdr *) malloc(NUMBER_OF_SEGMENTS_TO_LOAD * sizeof(Elf32_Phdr));
    PAGES_ALLOCED_TO_SEGMENT = (int *) calloc(NUMBER_OF_SEGMENTS_TO_LOAD, sizeof(int));

    /* Adding each Segment to load to SEGMENTS and searching for entry address */
    int idx_of_segment_to_load = 0;
    for (int i = 0; i < phnumber; i++) {
        if (lseek(FD, phoffset + i * phsize, SEEK_SET) == -1) {
			printf("Error in getting fd to phoffset + i*phsize\n");
			printf("phoffset: %i, i: %d, phsize: %hu\n", phoffset, i, phsize);
			exit(2);
		}

        ssize_t bytes_read_phdr = read(FD, PHDR, phsize);
        if (bytes_read_phdr != phsize) {
            printf("Unable to read full program header %d: got %zd bytes, expected %hu\n", i, bytes_read_phdr, phsize);
			exit(2);
        }

        if (PHDR->p_type == PT_LOAD) {
            /* Error checking as idx_of_segment_to_load cannot exceed NUMBER_OF_SEGMENTS_TO_LOAD */
            if (idx_of_segment_to_load >= NUMBER_OF_SEGMENTS_TO_LOAD) {printf("Mismatch in idx and number of segments to load during iteration"); exit(2);}

            memcpy(&PHDRS_OF_SEGMENTS_TO_LOAD[idx_of_segment_to_load], PHDR, phsize);  
            idx_of_segment_to_load++;
        }
    }
    if (idx_of_segment_to_load != NUMBER_OF_SEGMENTS_TO_LOAD) {printf("Mismatch in idx and number of segments to load after iteration"); exit(2);}
    
    if (exe[3]) {
        long max_pages = atol(exe[3]);
        if (max_pages <= 0) {printf("Invalid number of max pages entered"); exit(2);}
        INIT_REPLACEMENT_ALGO(max_pages);   /* Initialising the replacement system */
        init_swap_system(max_pages);    /* Initialising the swap system */
    } else {printf("Invalid number of max pages entered"); exit(2);}
    
}




void setup_signal_handler() {
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(struct sigaction));   /* Clear the structure */
    
    sa.sa_sigaction = segfault_handler;     /* Set the handler function */
    
    sa.sa_flags = SA_SIGINFO;   /* Set flags, Use sa_sigaction instead of sa_handler */ 
    
    /* Register the handler for SIGSEGV */
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(1);
    }
}




void segfault_handler(int sig, siginfo_t *info, void *context) {

    /* Incrementing number of page faults for book keeping */
    PAGE_FAULTS++;

    /* Obtain fault address (Virtual) */
    void *fault_addr = info->si_addr; 

    /* Find segment in which page fault occurred */
    int seg_idx = find_segment_of_fault(fault_addr);

    if (seg_idx != -1) {
        /* Allocate page to this segment */
        allocate_page(seg_idx, fault_addr); 
    }
    else {
        char msg1[] = "Unable to find segment, not a page fault\n";
        write(STDOUT_FILENO, msg1, sizeof(msg1) - 1);
        _exit(1);
    }
}




/* Returns the idx of phdr of segment of a given fault address, returns null if not in any segment */
int find_segment_of_fault(void *fault_addr) {
    unsigned long fa = (unsigned long) fault_addr;    /* Virtual address of the fault */

    for (int i = 0; i < NUMBER_OF_SEGMENTS_TO_LOAD; i++) {
        unsigned long seg_start = (unsigned long) PHDRS_OF_SEGMENTS_TO_LOAD[i].p_vaddr;
        unsigned long seg_end = seg_start + (unsigned long) PHDRS_OF_SEGMENTS_TO_LOAD[i].p_memsz;

        if (seg_start <= fa && fa < seg_end) {
            return i;
        }    
    }
    return -1;
}




/* Allocate page for the fault address and store it's access info in the segment */
void allocate_page(int idx_of_segment_having_fa, void* fault_addr) {
    Elf32_Phdr* segment = &PHDRS_OF_SEGMENTS_TO_LOAD[idx_of_segment_having_fa];
    
    if (segment == NULL || fault_addr == NULL) {
        char msg1[] = "ERROR: segment or fault_addr is NULL (Segmentation Fault in executable)\n";
        write(STDOUT_FILENO, msg1, sizeof(msg1) - 1);
        _exit(1);
    }

    /* Compute page-aligned start address for the faulting page */
    unsigned long fa = (unsigned long) fault_addr;
    unsigned long page_start = (fa / PAGE_SIZE_IN_BYTES) * PAGE_SIZE_IN_BYTES;

    /* Calculate file offset */
    unsigned long page_offset_in_segment = page_start - segment->p_vaddr;
    unsigned long file_offset = segment->p_offset + page_offset_in_segment;

    /* Using MAP_FIXED to map at exact virtual address */
    void *virtual_mem = mmap(
        (void*)page_start,
        PAGE_SIZE_IN_BYTES, 
        PROT_READ|PROT_WRITE|PROT_EXEC, 
        MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
        -1, 0
    );
    
    if (virtual_mem == MAP_FAILED) {
        perror("mmap page");
        _exit(1);
    }

    
    if (load_from_swap_if_exists((void *)page_start)) {
        /* Will just load data if the page exists in the swap file */
    }
    /* Load data from file if within p_filesz */
    else if (page_offset_in_segment < segment->p_filesz) {
        size_t bytes_to_read = PAGE_SIZE_IN_BYTES;
        if (page_offset_in_segment + PAGE_SIZE_IN_BYTES > segment->p_filesz) {
            bytes_to_read = segment->p_filesz - page_offset_in_segment;
        }

        if (lseek(FD, file_offset, SEEK_SET) == -1) {
            perror("lseek");
            _exit(1);
        }
        
        ssize_t bytes_read = read(FD, virtual_mem, bytes_to_read);
        if (bytes_read < 0) {
            perror("read");
            _exit(1);
        }

        if (bytes_to_read < PAGE_SIZE_IN_BYTES) {
            memset((char*)virtual_mem + bytes_to_read, 0, PAGE_SIZE_IN_BYTES - bytes_to_read);
        }

    } else {
        memset(virtual_mem, 0, PAGE_SIZE_IN_BYTES);
    }

    /* Update number of pages of this segment */
    PAGES_ALLOCED_TO_SEGMENT[idx_of_segment_having_fa]++;

    /* Adding page, algo specific fucntion */
    ADD_PAGE((void*)page_start);


    PAGE_ALLOCATIONS++;
}




/* Calculates total internal fragmentation after the execution */
void calculate_internal_fragmentation() {
    TOTAL_INTERNAL_FRAGMENTATION = 0;

    if (MODE == FIFO_REPLACEMENT_MODE && FIFO_QUEUE) {
        FIFOPage *curr = FIFO_QUEUE->Head;
        while (curr) {
            TOTAL_INTERNAL_FRAGMENTATION += calculate_page_waste(curr->address);
            curr = curr->next_page;
        }
    } 
    else if (MODE == RANDOM_REPLACEMENT_MODE && PAGES) {
        for (int i = 0; i < PAGES->max_pages; i++) {
            if (PAGES->adresses[i] != NULL) {
                TOTAL_INTERNAL_FRAGMENTATION += calculate_page_waste(PAGES->adresses[i]);
            }
        }
    }
}



/* Helper to calculate fragmentation for a specific page */
long calculate_page_waste(void* page_addr) {
    unsigned long p_start = (unsigned long)page_addr;
    unsigned long p_end = p_start + PAGE_SIZE_IN_BYTES;

    int seg_idx = find_segment_of_fault(page_addr);
    if (seg_idx == -1) return 0;

    unsigned long s_start = (unsigned long)PHDRS_OF_SEGMENTS_TO_LOAD[seg_idx].p_vaddr;
    unsigned long s_end = s_start + (unsigned long)PHDRS_OF_SEGMENTS_TO_LOAD[seg_idx].p_memsz;

    unsigned long intersect_start = (p_start > s_start) ? p_start : s_start;
    unsigned long intersect_end = (p_end < s_end) ? p_end : s_end;

    if (intersect_start >= intersect_end) return 0;

    long useful_bytes = (long)(intersect_end - intersect_start);
    return (long)PAGE_SIZE_IN_BYTES - useful_bytes;
}



void print_stats() {
    char *replacement_mode;
    if (MODE == FIFO_REPLACEMENT_MODE) replacement_mode = "FIFO";
    else if (MODE == RANDOM_REPLACEMENT_MODE) replacement_mode = "RANDOM";
    else replacement_mode = "FIFO (By Default, was unable to recognize mode entered)";

    calculate_internal_fragmentation();

    printf("\n-----------------------------------------------------------------------------\n");
    printf("---------------------------- SmartLoader Stats ------------------------------\n");
    printf("-----------------------------------------------------------------------------\n");
    printf("PAGE REPLACEMENT MODE: %s\n", replacement_mode);
    printf("Page faults: %d\n", PAGE_FAULTS);
    printf("Page allocations: %d\n", PAGE_ALLOCATIONS);
    printf("Total internal fragmentation: %ld Bytes (%.3f Kb) (%.3f Kib)\n", 
        TOTAL_INTERNAL_FRAGMENTATION, 
        (double)TOTAL_INTERNAL_FRAGMENTATION/1000.0,
        (double)TOTAL_INTERNAL_FRAGMENTATION/1024.0);
    printf("Page evictions: %ld\n", GET_NUMBER_OF_PAGE_EVICTIONS());  
    printf("\n-----------------------------------------------------------------------------\n");
    printf("-----------------------------------------------------------------------------\n");
}



/* release memory and other cleanups */
void loader_cleanup() {
    
    CLEANUP_REPLACEMENT();
    cleanup_swap_system();
   
    /* Free segments if it is not null */
    if (PHDRS_OF_SEGMENTS_TO_LOAD) free(PHDRS_OF_SEGMENTS_TO_LOAD);
    if (PAGES_ALLOCED_TO_SEGMENT) free(PAGES_ALLOCED_TO_SEGMENT);

    /* Free EHDR and PHDR if they are not null */
    if (EHDR) free(EHDR);
    if (PHDR) free(PHDR);

    /* Close FD if open */
    if (FD != -1) close(FD);
}