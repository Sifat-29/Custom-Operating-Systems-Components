#include "thread_pool.h"

#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>



// =================================================
//                    Structs
// =================================================

typedef struct Job {

    void (*func_to_the_job)(void*);
    void* args;
    struct Job* next;

} Job;



typedef struct Queue_job {

    Job* head;
    Job* tail;
    int queue_size;

} Queue_job;



// =================================================
//                 GLOBAL VARIABLES
// =================================================

static Queue_job* QUEUE_JOB;                   /* Shared resource between the threads, need to handle race conditions using mutexes */
static volatile int JOBS_PENDING;              /* Shared resource between the threads, need to handle race conditions using mutexes */

static pthread_mutex_t LOCK_POOL;              /* Single lock for the whole pool */
static pthread_cond_t COND_WORKER;             /* Conditional variable for the workers */
static pthread_cond_t COND_COMPLETED;          /* Conditional variable for the completion of all jobs */


static pthread_t *WORKERS;                     /* Array of threads */
static int NUMBER_OF_WORKERS;                  /* Number of threads */

static volatile int SHUTDOWN_WORKERS;          /* Initially 0, changed to 1 to exit the threads */



// =================================================
//                Internal Functions
// =================================================

static Job* _create_job(void (*func_to_the_job)(void*), void* args);
static void _free_job(Job** job);

static Queue_job* _create_queue();
static int _add_job_to_queue(Queue_job* queue, Job* job_to_add);
static Job* _pop_job(Queue_job* queue);
static void _free_queue(Queue_job** queue);

static void* _worker();



/**
 * Initialises the thread pool.
 * Returns 0 if error.
 * 
 * @param num_threads The number of threads (excluding the main thread calling this) to have on standby (and later working).
*/
int thread_pool_init(int num_threads) {

    /* Initialise the queue */
    QUEUE_JOB = _create_queue();
    if (!QUEUE_JOB) {printf("Malloc for QUEUE_JOB failed\n"); return 0;}


    /* Initialise the mutex locks and conditional variables */
    if (pthread_mutex_init(&LOCK_POOL, NULL) != 0) {
        printf("Init of LOCK_POOL failed\n"); 
        _free_queue(&QUEUE_JOB);
        return 0;
    }

    if (pthread_cond_init(&COND_WORKER, NULL) != 0) {
        printf("Init of COND_POOL failed\n"); 
        _free_queue(&QUEUE_JOB);
        pthread_mutex_destroy(&LOCK_POOL);
        return 0;
    }

    if (pthread_cond_init(&COND_COMPLETED, NULL) != 0) {
        printf("Init of COND_POOL failed\n"); 
        _free_queue(&QUEUE_JOB);
        pthread_cond_destroy(&COND_WORKER);
        pthread_mutex_destroy(&LOCK_POOL);
        return 0;
    }


    JOBS_PENDING = 0;
    SHUTDOWN_WORKERS = 0;

    /* Initialise the array of threads/workers */
    WORKERS = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);
    if (!WORKERS) {
        printf("Malloc for WORKERS failed\n"); 
        _free_queue(&QUEUE_JOB);
        pthread_cond_destroy(&COND_COMPLETED);
        pthread_cond_destroy(&COND_WORKER);
        pthread_mutex_destroy(&LOCK_POOL);
        return 0;
    }
   
    /* Create the threads/workers */
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&WORKERS[i], NULL, _worker, NULL) != 0) {
    
            pthread_mutex_lock(&LOCK_POOL);
            SHUTDOWN_WORKERS = 1;
            pthread_mutex_unlock(&LOCK_POOL);
            
            pthread_cond_broadcast(&COND_WORKER);

            for (int j = 0; j < i; j++) {pthread_join(WORKERS[j], NULL);}

            free(WORKERS);
            _free_queue(&QUEUE_JOB);

            pthread_cond_destroy(&COND_COMPLETED);
            pthread_cond_destroy(&COND_WORKER);
            pthread_mutex_destroy(&LOCK_POOL);
            
            return 0;
        }
    }
    
    NUMBER_OF_WORKERS = num_threads; 

    return 1;
}




static void* _worker() {
    while (1) {     /* Infinite loop */

        pthread_mutex_lock(&LOCK_POOL);

        /* This will prevent race conditions and exiting this loop means that this worker has the lock on queue and will work */
        while (QUEUE_JOB->queue_size == 0 && SHUTDOWN_WORKERS == 0) {
            pthread_cond_wait(&COND_WORKER, &LOCK_POOL);
        }

        if (QUEUE_JOB->queue_size == 0 && SHUTDOWN_WORKERS == 1) {     /* True when no more jobs and shutdown is requested */
            pthread_mutex_unlock(&LOCK_POOL);
            break;
        }

        Job* job_to_do = _pop_job(QUEUE_JOB);
        pthread_mutex_unlock(&LOCK_POOL);


        /* Execute the job */
        if (!job_to_do) continue;
        job_to_do->func_to_the_job(job_to_do->args);
        _free_job(&job_to_do);


        pthread_mutex_lock(&LOCK_POOL);
        JOBS_PENDING--;

        if (JOBS_PENDING == 0) {
            pthread_cond_signal(&COND_COMPLETED);
        }
        pthread_mutex_unlock(&LOCK_POOL);

    }
    return NULL;
}



/**
 * Adds task to be completed by the thread workers.
 * Will be executed when a thread worker is free. Execution order is currently FIFO.
 * Returns 0 on error.
 * 
 * @param func_ptr_to_task The function pointer to the task to be done. Argument to the function must be a void* pointer and return type void.
 * @param args The arguments to the function pointer, must be a void* pointer.
*/
int thread_pool_add_job(void (*func_ptr_to_task)(void*), void* args) {

    Job* job_to_add = _create_job(func_ptr_to_task, args);
    if (!job_to_add) {
        printf("Job struct could not be alloced\n"); 
        return 0;
    }

    if (pthread_mutex_lock(&LOCK_POOL) != 0) {printf("Error in acquistion of LOCK_QUEUE_JOB\n"); return 0;}

    if (_add_job_to_queue(QUEUE_JOB, job_to_add) == 0) {
        printf("Job could not be added\n"); 
        _free_job(&job_to_add);
        pthread_mutex_unlock(&LOCK_POOL);
        return 0;
    }

    JOBS_PENDING++;

    if (pthread_cond_signal(&COND_WORKER) != 0) {printf("Error in signaling of COND_QUEUE_JOB\n"); return 0;}
    if (pthread_mutex_unlock(&LOCK_POOL) != 0) {printf("Error in releasing of LOCK_QUEUE_JOB\n"); return 0;}

    return 1;
}



/**
 * Wait untill all the jobs given to the thread pool are completed (all the workers will be free after the completion of this call).
*/
void thread_pool_wait() {
    pthread_mutex_lock(&LOCK_POOL);
    
    while (JOBS_PENDING > 0) {
        pthread_cond_wait(&COND_COMPLETED, &LOCK_POOL);
    }

    pthread_mutex_unlock(&LOCK_POOL);
}



/**
 * Completely cleans up the thread pool.
*/
void thread_pool_cleanup() {
    pthread_mutex_lock(&LOCK_POOL);
    SHUTDOWN_WORKERS = 1;
    pthread_mutex_unlock(&LOCK_POOL);

    pthread_cond_broadcast(&COND_WORKER);

    for (int i = 0; i < NUMBER_OF_WORKERS; i++) {
        pthread_join(WORKERS[i], NULL);
    }
    free(WORKERS);

    pthread_mutex_destroy(&LOCK_POOL);
    pthread_cond_destroy(&COND_COMPLETED);
    pthread_cond_destroy(&COND_WORKER);

    _free_queue(&QUEUE_JOB);
}



// =================================================
//                 Queue Functions
// =================================================

/**
 * Creates a job object.
 * Returns NULL if any error.
 * 
 * @param func_to_the_job The function pointer of the job
 * @param args Void* pointer to struct in which the func_to_the_job operates
 */
static Job* _create_job(void (*func_to_the_job)(void*), void* args) {
    if (!func_to_the_job) {
        if (!func_to_the_job) printf("func_to_the_job job is NULL\n");
        return NULL;
    }    

    Job* new_job = (Job*) malloc(sizeof(Job));
    if (!new_job) {printf("Malloc for new_job failed\n"); return NULL;}

    new_job->func_to_the_job = func_to_the_job;
    new_job->args = args;
    new_job->next = NULL;

    return new_job;
}



/**
 * Completely frees a job.
 */
static void _free_job(Job** job) {
    if (job && *job) {
        free(*job);
        *job = NULL;
    }
} 



/**
 * Creates a queue and returns a pointer to it, returns NULL if error.
*/
static Queue_job* _create_queue() {
    Queue_job* queue = (Queue_job*) malloc(sizeof(Queue_job));
    if (!queue) {return NULL;}

    queue->head = NULL;
    queue->tail = NULL;
    queue->queue_size = 0;

    return queue;
}



/**
 * Adds job to the queue.
 * Returns 0 if error.
 * 
 * @param queue The queue from which the job is removed
 * @param job_to_add The job which is to be added
*/
static int _add_job_to_queue(Queue_job* queue, Job* job_to_add) {
    if (!queue || !job_to_add) {
        if (!queue) printf("queue is NULL\n");
        if (!job_to_add) printf("job_to_add is NULL\n");
        return 0;
    }
    
    if (queue->queue_size == 0) {
        queue->head = job_to_add;
        queue->tail = job_to_add;
    } else {
        queue->tail->next = job_to_add;
        queue->tail = job_to_add;
        job_to_add->next = NULL;
    }

    queue->queue_size++;

    return 1;
}



/**
 * Returns the next job to execute. 
 * Returns NULL if error or queue is empty.
 * 
 * @param queue The queue from which the job is removed
*/
static Job* _pop_job(Queue_job* queue) {
    if (!queue) {
        if (!queue) printf("queue is NULL\n");
        return NULL;
    }
    
    Job* job_to_pop = NULL;

    if (queue->queue_size != 0) {
        job_to_pop = queue->head;
        queue->head = job_to_pop->next;

        job_to_pop->next = NULL;
        queue->queue_size--;
        if (queue->head == NULL) queue->tail = NULL;
    }

    return job_to_pop;
}



/**
 * Completely frees a queue.
*/
static void _free_queue(Queue_job** queue) {
    if (queue && *queue) {
        Job* temp = NULL;
        Job* current = (*queue)->head;

        while (current) {
            temp = current;
            current = current->next;

            _free_job(&temp);
        }

        free(*queue);
        *queue = NULL;
    }
}