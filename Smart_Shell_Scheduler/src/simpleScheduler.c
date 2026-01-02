#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/wait.h>


// =====================================================================
// ================================ STRUCTS ============================
// =====================================================================

typedef struct ProcessInfoNode {
    pid_t pid;
    long run_slices;
    long wait_slices;
    struct ProcessInfoNode *next;
    struct ProcessInfoNode *prev;
} ProcessInfoNode;

typedef struct Queue { 
    ProcessInfoNode *head, *tail; 
    int count; 
} Queue;

typedef struct {
    pid_t pid;
    long run_slices;
    long wait_slices;
    long completion_slices;  // run + wait
} JobResult;


// =====================================================================
// ======================== FUNCTION SIGNATURES ======================== 
// =====================================================================

static void initQueue(Queue *q);
static ProcessInfoNode *createNode(pid_t pid);
static void pushNode(Queue *q, ProcessInfoNode *n);
static ProcessInfoNode *popNode(Queue *q);
static ProcessInfoNode *removeNodeByPid(Queue *q, pid_t pid);

static int send_job_result(int result_fd, ProcessInfoNode *node);
static void send_all_results(int result_fd, Queue *complete);
static int is_process_dead(pid_t pid);
static void mark_complete_node(ProcessInfoNode *n, Queue *complete);
static void mark_complete_pid(pid_t pid, Queue *complete);
static void scan_and_mark_dead(Queue *q, Queue *ready, Queue *running, Queue *complete);
static int set_nonblock(int fd);

static void handle_sigint(int s);


static volatile sig_atomic_t shutdown_requested = 0;


// =====================================================================
// =============================== MAIN ================================ 
// =====================================================================
int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s NCPU tslice-ms submit-read-fd result-write-fd\n", argv[0]);
        return 2;
    }

    int NCPU = atoi(argv[1]);
    long TMS = atol(argv[2]);
    int submit_fd = atoi(argv[3]);
    int result_fd = atoi(argv[4]);

    if (NCPU <= 0 || TMS <= 0) { 
        fprintf(stderr, "Error: bad args - NCPU and TSLICE must be positive\n"); 
        return 2; 
    }

    if (set_nonblock(submit_fd) == -1) { 
        perror("Error: set_nonblock on submit pipe"); 
        return 2; 
    }

    /* ignore SIGPIPE (writing to closed FIFO) and handle SIGINT to request shutdown */
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa; 
    memset(&sa, 0, sizeof(sa)); 
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL); 
    sigaction(SIGTERM, &sa, NULL);


    Queue ready, running, buffer, complete;
    initQueue(&ready); 
    initQueue(&running); 
    initQueue(&buffer); 
    initQueue(&complete);

    /* tick sleep interval derived from requested timeslice */
    struct timespec req;
    req.tv_sec = TMS / 1000;
    req.tv_nsec = (TMS % 1000) * 1000000L;

    long tick = 0;
    int shell_closed = 0;

    while (1) {
        /* Sleep for one tick */
        struct timespec rem = req;
        while (nanosleep(&rem, &rem) == -1 && errno == EINTR) {}
        tick++;
        
        /* Stop all running processes, credit their run slice, move to buffer */
        while (running.count > 0) {
            ProcessInfoNode *node = popNode(&running);
            if (!node) break;

            node->run_slices++;  /* Credit the slice they just consumed */    
                  
            if (is_process_dead(node->pid)) {
                mark_complete_node(node, &complete);
                continue;
            }

            if (kill(node->pid, SIGSTOP) == -1) {
                if (errno == ESRCH) {
                    mark_complete_node(node, &complete);
                    continue;
                } else {
                    fprintf(stderr, "Warning: SIGSTOP failed for PID %d: %s\n", 
                            (int)node->pid, strerror(errno));
                }
            }

            pushNode(&buffer, node);
        }
        
        /* Increment wait slices ONLY for processes already in ready queue */
        for (ProcessInfoNode *cur = ready.head; cur; cur = cur->next) {
            cur->wait_slices++;
        }
        
        /* Move buffer back to ready (check for deaths) */
        while (buffer.count > 0) {
            ProcessInfoNode *node = popNode(&buffer);
            if (!node) break;
            
            if (is_process_dead(node->pid)) {
                mark_complete_node(node, &complete);
            } else {
                pushNode(&ready, node);
            }
        }

        /* Drain submission pipe (non-blocking) */
        while (1) {
            pid_t newpid;
            ssize_t r = read(submit_fd, &newpid, sizeof(newpid));
            if (r > 0) {
                if (r == (ssize_t)sizeof(newpid)) {
                    ProcessInfoNode *n = createNode(newpid);
                    if (n) { 
                        pushNode(&ready, n); 
                    } else {
                        fprintf(stderr, "Error: malloc failed for PID %d\n", (int)newpid);
                    }
                } else {
                    fprintf(stderr, "Warning: partial read of PID (%zd bytes)\n", r);
                }
                continue;
            }
            if (r == 0) { 
                if (!shell_closed) {
                    shell_closed = 1; 
                }
                break; 
            }
            if (r == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                fprintf(stderr, "Error: read from submission pipe failed: %s\n", strerror(errno));
                break;
            }
        }
        
        /* Dispatch up to NCPU processes from ready to running */
        int to_dispatch = (ready.count < NCPU) ? ready.count : NCPU;
        
        for (int i = 0; i < to_dispatch; i++) {
            ProcessInfoNode *node = popNode(&ready);
            if (!node) break;
            
            if (is_process_dead(node->pid)) {
                mark_complete_node(node, &complete);
                i--;
                continue;
            }
            
            if (kill(node->pid, SIGCONT) == -1) {
                if (errno == ESRCH) {
                    mark_complete_node(node, &complete);
                    i--;
                } else {
                    fprintf(stderr, "Warning: SIGCONT failed for PID %d: %s, returning to ready queue\n",
                            (int)node->pid, strerror(errno));
                    pushNode(&ready, node);
                    i--;
                }
                continue;
            }
            
            pushNode(&running, node);
        }
        
        /* Exit condition, only exit when truly no work remains */
        int all_queues_empty = (ready.count == 0 && running.count == 0 && buffer.count == 0);
        
        if (shutdown_requested && all_queues_empty) {
            break;
        }
        
        /* Alternative exit: shell closed pipe AND all jobs finished */
        if (shell_closed && all_queues_empty) {
            break;
        }
    }

    
    /* Final sweep - reap any zombies or processes that just finished */
    for (int sweep = 0; sweep < 3; sweep++) {
        usleep(50000);  // 50ms between sweeps
        scan_and_mark_dead(&ready, &ready, &running, &complete);
        scan_and_mark_dead(&running, &ready, &running, &complete);
        scan_and_mark_dead(&buffer, &ready, &running, &complete);
    }

    /* final reporting - send to shell */
    if (complete.count > 0) {
        send_all_results(result_fd, &complete);
    } else {
        /* Send end marker even if no jobs */
        JobResult end_marker = {-1, 0, 0, 0};
        ssize_t written = write(result_fd, &end_marker, sizeof(end_marker));
        if (written != sizeof(end_marker)) {
            fprintf(stderr, "Error: failed to send end marker: %s\n", (written == -1) ? strerror(errno) : "partial write");
        }
    }
    
    close(result_fd);

    /* free remaining nodes */
    ProcessInfoNode *t;
    while ((t = popNode(&ready)) != NULL) free(t);
    while ((t = popNode(&running)) != NULL) free(t);
    while ((t = popNode(&buffer)) != NULL) free(t);
    while ((t = popNode(&complete)) != NULL) free(t);

    return 0;
}


// =====================================================================
// ========================= FUNCTIONS  ================================ 
// =====================================================================

static void initQueue(Queue *q) { 
    q->head = q->tail = NULL; 
    q->count = 0; 
}

static ProcessInfoNode *createNode(pid_t pid) {
    ProcessInfoNode *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->pid = pid;
    n->run_slices = 0;
    n->wait_slices = 0;
    n->next = n->prev = NULL;
    return n;
}

static void pushNode(Queue *q, ProcessInfoNode *n) {
    if (!n) return;
    n->next = n->prev = NULL;
    if (q->count == 0) q->head = q->tail = n;
    else { n->prev = q->tail; q->tail->next = n; q->tail = n; }
    q->count++;
}

static ProcessInfoNode *popNode(Queue *q) {
    if (q->count == 0) return NULL;
    ProcessInfoNode *n = q->head;
    if (q->head == q->tail) q->head = q->tail = NULL;
    else { q->head = n->next; q->head->prev = NULL; n->next = NULL; }
    n->prev = NULL; q->count--;
    return n;
}

static ProcessInfoNode *removeNodeByPid(Queue *q, pid_t pid) {
    ProcessInfoNode *cur = q->head;
    while (cur) {
        if (cur->pid == pid) {
            if (cur->prev) cur->prev->next = cur->next; else q->head = cur->next;
            if (cur->next) cur->next->prev = cur->prev; else q->tail = cur->prev;
            cur->next = cur->prev = NULL; q->count--;
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static int send_job_result(int result_fd, ProcessInfoNode *node) {
    JobResult result;
    result.pid = node->pid;
    result.run_slices = node->run_slices;
    result.wait_slices = node->wait_slices;
    result.completion_slices = node->run_slices + node->wait_slices;
    
    ssize_t written = write(result_fd, &result, sizeof(result));
    if (written != sizeof(result)) {
        fprintf(stderr, "Error: failed to send result for PID %d: %s\n", 
                (int)node->pid, written == -1 ? strerror(errno) : "partial write");
        return -1;
    }
    return 0;
}

static void send_all_results(int result_fd, Queue *complete) {
    for (ProcessInfoNode *cur = complete->head; cur; cur = cur->next) {
        send_job_result(result_fd, cur);
    }
    
    // Send end marker (pid = -1)
    JobResult end_marker = {-1, 0, 0, 0};
    ssize_t written = write(result_fd, &end_marker, sizeof(end_marker));
    if (written != sizeof(end_marker)) {
        fprintf(stderr, "Error: failed to send end marker: %s\n", 
                written == -1 ? strerror(errno) : "partial write");
    }
}

static int is_process_dead(pid_t pid) {
    /* Initially try to signal the process */
    if (kill(pid, 0) == -1) {
        if (errno == ESRCH) {
            /* Process doesn't exist */
            return 1;
        }
        /* Other error, assuming alive */
        return 0;
    }
    
    /* Process exists, but check if it's a zombie */
    char stat_path[64];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(stat_path, "r");
    if (!f) {
        /* Can't open stat file, process probably gone */
        return 1;
    }
    
    /* Reading the stat file to check process state */
    char buffer[512];
    if (!fgets(buffer, sizeof(buffer), f)) {
        fclose(f);
        return 1;
    }
    fclose(f);
    
    /* Finding the last ')' which marks the end of the command name */
    char *p = strrchr(buffer, ')');
    if (!p) {
        return 1;
    }
    
    /* State is the first character after ') ' */
    char state = *(p + 2);
    
    /* If it's a zombie, consider it dead for our purposes */
    if (state == 'Z' || state == 'X') {  /* Z=zombie, X=dead */
        return 1;
    }
    
    /* Process exists and is not a zombie */
    return 0;
}

static void mark_complete_node(ProcessInfoNode *n, Queue *complete) {
    if (!n) return;
    n->next = n->prev = NULL;
    pushNode(complete, n);
}

static void mark_complete_pid(pid_t pid, Queue *complete) {
    ProcessInfoNode *newn = createNode(pid);
    if (!newn) {
        fprintf(stderr, "Error: malloc failed when marking PID %d as complete\n", (int)pid);
        return;
    }
    newn->run_slices = 0; 
    newn->wait_slices = 0;
    pushNode(complete, newn);
}

static void scan_and_mark_dead(Queue *q, Queue *ready, Queue *running, Queue *complete) {
    ProcessInfoNode *cur = q->head;
    while (cur) {
        ProcessInfoNode *next = cur->next;
        if (is_process_dead(cur->pid)) {
            ProcessInfoNode *removed = removeNodeByPid(ready, cur->pid);
            if (!removed) removed = removeNodeByPid(running, cur->pid);
            if (!removed) removed = removeNodeByPid(q, cur->pid);
            if (removed) mark_complete_node(removed, complete);
            else mark_complete_pid(cur->pid, complete);
        }
        cur = next;
    }
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void handle_sigint(int s) { (void)s; shutdown_requested = 1; }