#ifndef THREAD_POOL_API
#define THREAD_POOL_API



/**
 * Initialises the thread pool.
 * Returns 0 if error.
 * 
 * @param num_threads The number of threads (excluding the main thread calling this) to have on standby (and later working).
*/
int thread_pool_init(int num_threads);



/**
 * Adds task to be completed by the thread workers.
 * Will be executed when a thread worker is free. Execution order is currently FIFO.
 * Returns 0 on error.
 * 
 * @param func_ptr_to_task The function pointer to the task to be done. Argument to the function must be a void* pointer and return type void.
 * @param args The arguments to the function pointer, must be a void* pointer.
*/
int thread_pool_add_job(void (*func_ptr_to_task)(void*), void* args);



/**
 * Wait untill all the jobs given to the thread pool are completed (all the workers will be free after the completion of this call).
*/
void thread_pool_wait();



/**
 * Completely cleans up the thread pool.
*/
void thread_pool_cleanup();



#endif