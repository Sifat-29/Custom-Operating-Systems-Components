#include "loader.h"
#include "swap_manager.h"


/* ================================================================================================================ */
/*                                             FIFO REPLACEMENT POLICY                                              */
/* ================================================================================================================ */

typedef struct FIFOPage{
    void *address;
    struct FIFOPage *next_page;
} FIFOPage;

typedef struct FIFOQueue {
    FIFOPage *Head;
    FIFOPage *Tail;
    long num_of_pages;
    long max_pages;
} FIFOQueue;


FIFOQueue *FIFO_QUEUE; 
long PAGE_EVICTIONS_FIFO;


void free_page_fifo(FIFOPage *page) {
    if (page->address) {
        handle_page_eviction_to_swap(page->address);
        munmap(page->address, PAGE_SIZE_IN_BYTES);
    }
    page->next_page = NULL;
    free(page);
}


void evict_page_fifo() {
    if (FIFO_QUEUE->Head == NULL || FIFO_QUEUE->num_of_pages == 0) {printf("FifoQueue already empty or head null, still instructed to evict page"); exit(1);}

    FIFOPage *temp = FIFO_QUEUE->Head;
    FIFO_QUEUE->Head = temp->next_page;
    if (FIFO_QUEUE->Head == NULL) FIFO_QUEUE->Tail = NULL;
    free_page_fifo(temp);
    FIFO_QUEUE->num_of_pages--;

    PAGE_EVICTIONS_FIFO++;
}



void init_fifo(long max_pages) {
    FIFO_QUEUE = (FIFOQueue *) malloc(sizeof(FIFOQueue));
    if (!FIFO_QUEUE) {printf("Unable to malloc fifo queue\n"); exit(1);};

    FIFO_QUEUE->Head = NULL;
    FIFO_QUEUE->Tail = NULL;
    FIFO_QUEUE->num_of_pages = 0;
    FIFO_QUEUE->max_pages = max_pages;

    PAGE_EVICTIONS_FIFO = 0;
}


void cleanup_fifo() {
    /* Iterate over all the pages and unmapping and freeing them */
    FIFOPage *current = FIFO_QUEUE->Head;
    while (current != NULL) {
        FIFOPage *temp = current;
        current = current->next_page;
        free_page_fifo(temp);
    }

    if (FIFO_QUEUE) free(FIFO_QUEUE);    /* Free the fifo queue if not null */
}


long page_evictions_fifo() {return PAGE_EVICTIONS_FIFO;}


void add_page_fifo(void *page_start_addr) {

    while (FIFO_QUEUE->num_of_pages >= FIFO_QUEUE->max_pages) evict_page_fifo();

    FIFOPage *new_page = (FIFOPage *) malloc(sizeof(FIFOPage));
    new_page->address = page_start_addr;
    new_page->next_page = NULL;
    
    if (FIFO_QUEUE->Head == NULL && FIFO_QUEUE->Tail == NULL) {
        FIFO_QUEUE->Head = new_page;
        FIFO_QUEUE->Tail = new_page;
    } else {
        FIFO_QUEUE->Tail->next_page = new_page;
        FIFO_QUEUE->Tail = new_page;
    }

    FIFO_QUEUE->num_of_pages++;
}





/* ================================================================================================================ */
/*                                           RANDOM REPLACEMENT POLICY                                              */
/* ================================================================================================================ */

typedef struct RandomArray {
    void **adresses;
    long max_pages;
    long alloc_idx;
} RandomArray;



RandomArray* PAGES = NULL;
long PAGE_EVICTIONS_RANDOM;



void free_page_random(int idx) {
    if (PAGES->adresses[idx]) {
        handle_page_eviction_to_swap(PAGES->adresses[idx]);
        munmap(PAGES->adresses[idx], PAGE_SIZE_IN_BYTES);
    }
    PAGES->adresses[idx] = NULL;
}


void init_random(long max_pages) {
    srand(time(NULL));    /* Initialise seed for random number generator */

    PAGES = (RandomArray *) malloc(sizeof(RandomArray));
    if (!PAGES) {printf("Unable to malloc random array of pages\n"); exit(1);};

    PAGES->adresses = (void **) malloc(sizeof(void *) * (int)max_pages);
    PAGES->max_pages = max_pages;
    PAGES->alloc_idx = 0;

    PAGE_EVICTIONS_RANDOM = 0;

    for (int i = 0; i < PAGES->max_pages; i++) PAGES->adresses[i] = NULL;
}


void cleanup_random() {
    for (int i = 0; i < PAGES->max_pages; i++) free_page_random(i);
    if (PAGES->adresses) free(PAGES->adresses);
    free(PAGES);
}


long page_evictions_random() {return PAGE_EVICTIONS_RANDOM;}

void add_page_random(void *page_start_addr) {
    /* Evict a random page in this case replace it with the new one */
    if (PAGES->alloc_idx == PAGES->max_pages) {
        int idx_to_operate = rand() % PAGES->max_pages;
        free_page_random(idx_to_operate);
        PAGE_EVICTIONS_RANDOM++;
        PAGES->adresses[idx_to_operate] = page_start_addr;
    } 
    /* When array is not fully filled */
    else {
        PAGES->adresses[PAGES->alloc_idx] = page_start_addr;
        PAGES->alloc_idx++;
    }
}


