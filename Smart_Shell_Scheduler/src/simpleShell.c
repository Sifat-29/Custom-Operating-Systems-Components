#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>


// =====================================================================
// ============================== STRUCTS ==============================
// =====================================================================

typedef struct ProcessInfo {
    char *command;
    pid_t pid;
    int exit_status;
} ProcessInfo;


typedef struct HistoryEntry {
    char *full_command;
    int command_number;
    ProcessInfo *processes;
    int num_processes;
    struct timeval start_time;
    struct timeval end_time;
    struct HistoryEntry *next;
} HistoryEntry;


typedef struct JobNameEntry {
    pid_t pid;
    char *executable_name;
    struct JobNameEntry *next;
} JobNameEntry;


typedef struct {
    pid_t pid;
    long run_slices;
    long wait_slices;
    long completion_slices;  // run + wait
} JobResult;


// =====================================================================
// ======================== FUNCTION SIGNATURES ======================== 
// =====================================================================
void launch(char *command);
int parseCommands(char* command, char*** commands, const char* delimiter, int capacity);
void cleanupLaunch(ProcessInfo** process_info, char*** commands, int num_commands);
void add_to_history(char *command, ProcessInfo *processes, int num_processes, struct timeval start, struct timeval end);
double get_time_diff_ms(struct timeval start, struct timeval end);
void show_execution_details();
void show_history();
void cleanHistory();
int send_pid_to_scheduler(int pipe_fd, pid_t pid);
void add_job_name(pid_t pid, const char *exec_name);
const char* get_job_name(pid_t pid);
void cleanup_job_names();
void receive_and_print_results(int result_fd);

/* To avoid nullstrings while duping */
char *xstrdup(const char *s) { if (!s) return NULL; return strdup(s); }

/* signal handlers */
static void my_handler(int signum);
static void sigchld_handler(int signum);


// =====================================================================
// ============================= GLOBALS  ============================== 
// =====================================================================
HistoryEntry *HISTORY_HEAD = NULL;
HistoryEntry *HISTORY_TAIL = NULL;
int COMMAND_NUMBER = 0;

JobNameEntry *JOB_NAME_LIST = NULL;

int PIPE_WITH_SCHEDULER_WRITE_FD = -1;
int RESULT_PIPE_READ_FD = -1;
pid_t SCHEDULER_PID = -1;
pid_t SHELL_PID = -1;

static volatile sig_atomic_t SHUTDOWN_REQUESTED = 0;


// =====================================================================
// ================================ MAIN =============================== 
// =====================================================================
int main(int argc, char *argv[]) {
    SHELL_PID = getpid();

    /* Handle SIGINT to shutdown gracefully */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = my_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    
    /* Handle SIGCHLD to reap zombie children (submitted jobs only (exclude scheduler)) */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa_chld.sa_mask);
    sigaction(SIGCHLD, &sa_chld, NULL);

    if (argc != 3) {
        fprintf(stderr, "Usage: ./bin/simpleShell [NCPU] [TSLICE (in ms)]\n");
        exit(EXIT_FAILURE);
    }

    int result_pipe[2];
    if (pipe(result_pipe) == -1) { perror("pipe result"); exit(1); }

    int submit_pipe[2];
    if (pipe(submit_pipe) == -1) { perror("pipe submit"); exit(1); }

    SCHEDULER_PID = fork();
    if (SCHEDULER_PID == -1) { perror("fork"); exit(1); }

    if (SCHEDULER_PID == 0) {
        /* scheduler child */
        setpgid(0, 0);
        
        close(submit_pipe[1]);
        close(result_pipe[0]);
        
        char submit_fd_str[32], result_fd_str[32];
        snprintf(submit_fd_str, sizeof(submit_fd_str), "%d", submit_pipe[0]);
        snprintf(result_fd_str, sizeof(result_fd_str), "%d", result_pipe[1]);
        
        char *args[] = {"./bin/simpleScheduler", argv[1], argv[2], 
                        submit_fd_str, result_fd_str, NULL};
        execv(args[0], args);
        perror("execv scheduler");
        _exit(127);
    }

    /* parent shell */
    close(submit_pipe[0]);
    close(result_pipe[1]);

    PIPE_WITH_SCHEDULER_WRITE_FD = submit_pipe[1];
    RESULT_PIPE_READ_FD = result_pipe[0];

    /* basic prompt loop */
    char *line = NULL;
    size_t buf_sz = 0;
    ssize_t nread;
    char cwd[1024];
    printf("Enter [history] to see command history of this session\n");
    while (SHUTDOWN_REQUESTED != 1) {
        if (getcwd(cwd, sizeof(cwd)) == NULL) strcpy(cwd, "?");
        printf("%s@shell:~%s$ ", getenv("USER") ? getenv("USER") : "user", cwd);
        fflush(stdout);

        nread = getline(&line, &buf_sz, stdin);
        if (nread == -1) {
            if (feof(stdin)) {
                my_handler(SIGINT);
                break;
            }
            clearerr(stdin);
            continue;
        }
        if (nread > 0 && line[nread-1] == '\n') line[nread-1] = '\0';
        if (line[0] == '\0') continue;
        launch(line);
    }
    
    free(line);

    /* show history and cleanup */
    show_execution_details();
    cleanHistory();

    /* close write end so scheduler sees EOF */
    if (PIPE_WITH_SCHEDULER_WRITE_FD != -1) {
        close(PIPE_WITH_SCHEDULER_WRITE_FD);
        PIPE_WITH_SCHEDULER_WRITE_FD = -1;
    }

    /* request scheduler shutdown and wait for it to finish */
    if (SCHEDULER_PID > 0) {
        printf("\nShutting down scheduler...\n");
        fflush(stdout);
        
        /* Send SIGTERM to request graceful shutdown */
        kill(SCHEDULER_PID, SIGTERM);
        
        /* Block SIGINT while waiting for scheduler to prevent interruption */
        sigset_t sigset, oldset;
        sigemptyset(&sigset);
        sigaddset(&sigset, SIGINT);
        sigprocmask(SIG_BLOCK, &sigset, &oldset);
        
        /* Wait indefinitely for scheduler to finish processing all jobs */
        int status;
        pid_t result = waitpid(SCHEDULER_PID, &status, 0);
        
        if (result == SCHEDULER_PID) {
            printf("Scheduler has shut down gracefully.\n");
            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0) {
                    printf("Warning: Scheduler exited with code %d\n", exit_code);
                }
            } else if (WIFSIGNALED(status)) {
                printf("Warning: Scheduler was terminated by signal %d\n", WTERMSIG(status));
            }
        } else if (result == -1) {
            perror("waitpid scheduler");
        }
        
        /* Restore signal mask before reading results */
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        
        /* NOW read results - scheduler has written them before exiting */
        receive_and_print_results(RESULT_PIPE_READ_FD);
        close(RESULT_PIPE_READ_FD);
    }
    
    cleanup_job_names();
    exit(0);
}

/* Launches each command recieved by the shell */
void launch(char *command) {
    if (!command || command[0] == '\0') return;

    char **commands = NULL;
    int num_commands = parseCommands(command, &commands, "|", 3);
    if (num_commands <= 0) { if (commands) free(commands); return; }

    ProcessInfo *processes = calloc((size_t)num_commands, sizeof(ProcessInfo));
    if (!processes) { cleanupLaunch(&processes, &commands, num_commands); return; }

    for (int i = 0; i < num_commands; ++i) { 
        processes[i].command = NULL; 
        processes[i].pid = -1; 
        processes[i].exit_status = -1; 
    }

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    int pipefd[2];
    int prev_pipe_read = -1;

    for (int i = 0; i < num_commands; ++i) {
        processes[i].command = xstrdup(commands[i]);
        
        /* handling for submit command in the parent */
        char **args = NULL;
        int num_args = parseCommands(commands[i], &args, " ", 5);
        if (num_args > 0 && strcmp(args[0], "submit") == 0) {
            /* Fork and submit each job to scheduler */
            for (int j = 1; j < num_args; ++j) {
                pid_t p = fork();
                if (p == 0) {
                    setpgid(0, 0);
                    
                    /* Ignoring terminal signals */
                    signal(SIGINT, SIG_IGN);
                    signal(SIGQUIT, SIG_IGN);
                    signal(SIGTSTP, SIG_IGN);
                    signal(SIGTTIN, SIG_IGN);
                    signal(SIGTTOU, SIG_IGN);
                    
                    char *argv2[] = { args[j], NULL };
                    execvp(argv2[0], argv2);
                    perror("execvp");
                    _exit(127);
                } else if (p > 0) {
                    /* Parent shell: put child in its own group */
                    setpgid(p, p);
                    add_job_name(p, args[j]);
                    
                    /* Send PID to scheduler and print confirmation */
                    if (send_pid_to_scheduler(PIPE_WITH_SCHEDULER_WRITE_FD, p) == 0) {
                        printf("[Submitted job with PID %d: %s]\n", (int)p, args[j]);
                        fflush(stdout);
                    } else {
                        printf("[Unable to submit job with PID %d: %s]\n", (int)p, args[j]);
                        fflush(stdout);
                    }
                } else { perror("fork"); }
            }
            
            /* Clean up args and mark this "command" as successful without forking */
            for (int k = 0; k < num_args; ++k) free(args[k]);
            free(args);
            
            processes[i].pid = getpid();
            processes[i].exit_status = 0;
            
            /* Skip the normal fork/pipelining for this command */
            continue;
        }
        
        /* For non-submit commands, continue with normal pipeline handling */
        if (i != num_commands - 1) {
            if (pipe(pipefd) < 0) { 
                perror("pipe");
                for (int k = 0; k < num_args; ++k) free(args[k]);
                free(args);
                cleanupLaunch(&processes, &commands, num_commands); 
                return; 
            }
        }

        pid_t pid = fork();
        if (pid == -1) { 
            perror("fork");
            for (int k = 0; k < num_args; ++k) free(args[k]);
            free(args);
            cleanupLaunch(&processes, &commands, num_commands); 
            return; 
        }

        if (pid == 0) { /* child process */
            if (i != 0) {
                dup2(prev_pipe_read, STDIN_FILENO);
                close(prev_pipe_read);
            }
            if (i != num_commands - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]); 
                close(pipefd[1]);
            }

            /* Check for history command */
            if (num_args > 0 && strcmp(args[0], "history") == 0) {
                show_history();
                for (int k = 0; k < num_args; ++k) free(args[k]);
                free(args);
                _exit(0);
            }

            /* Regular command execution */
            if (num_args > 0) {
                execvp(args[0], args);
                perror("execvp");
            }
            for (int k = 0; k < num_args; ++k) free(args[k]);
            free(args);
            _exit(127);
        }

        /* Parent shell process */
        processes[i].pid = pid;
        
        /* Clean up args in parent */
        for (int k = 0; k < num_args; ++k) free(args[k]);
        free(args);

        if (i != 0) { close(prev_pipe_read); }
        if (i != num_commands - 1) { 
            prev_pipe_read = pipefd[0]; 
            close(pipefd[1]); 
        }
    }

    /* Wait for all processes in the pipeline (skip submit which has shell's PID) */
    for (int i = 0; i < num_commands; ++i) {
        if (processes[i].pid <= 0 || processes[i].pid == getpid()) {
            /* Skip invalid PIDs or shell's own PID (submit command) */
            if (processes[i].pid == getpid()) { processes[i].exit_status = 0; }
            continue;
        }
        
        int status;
        pid_t w = waitpid(processes[i].pid, &status, 0);
        if (w == -1) {
            if (errno != EINTR) {
                perror("waitpid");
            }
            processes[i].exit_status = -1;
        } else {
            if (WIFEXITED(status)) {
                processes[i].exit_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                processes[i].exit_status = 128 + WTERMSIG(status);
            }
        }
    }

    gettimeofday(&end_time, NULL);
    add_to_history(command, processes, num_commands, start_time, end_time);
    cleanupLaunch(&processes, &commands, num_commands);
}

/* Parser for commands */
int parseCommands(char* command, char*** commands, const char* delimiter, int capacity) {
    if (!command || !commands || !delimiter) return -1;
    char *copy = strdup(command);
    if (!copy) return -1;

    int current_capacity = capacity + 1;
    *commands = malloc(current_capacity * sizeof(char*));
    if (!*commands) { free(copy); return -1; }

    int num_tokens = 0;
    char *saveptr = NULL;
    char *token = strtok_r(copy, delimiter, &saveptr);

    while (token) {
        while (*token == ' ') token++;
        if (*token == '\0') { token = strtok_r(NULL, delimiter, &saveptr); continue; }
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') { *end = '\0'; end--; }

        if (num_tokens + 1 >= current_capacity) {
            current_capacity += capacity;
            char **temp = realloc(*commands, current_capacity * sizeof(char*));
            if (!temp) {
                for (int i = 0; i < num_tokens; ++i) free((*commands)[i]);
                free(*commands); free(copy); return -1;
            }
            *commands = temp;
        }
        (*commands)[num_tokens] = strdup(token);
        if (!(*commands)[num_tokens]) {
            for (int i = 0; i < num_tokens; ++i) free((*commands)[i]);
            free(*commands); free(copy); return -1;
        }
        num_tokens++;
        token = strtok_r(NULL, delimiter, &saveptr);
    }
    (*commands)[num_tokens] = NULL;
    free(copy);
    return num_tokens;
}

/*  Cleanup of Commands */
void cleanupLaunch(ProcessInfo** process_info, char*** commands, int num_commands) {
    if (process_info && *process_info) {
        for (int j = 0; j < num_commands; j++) free((*process_info)[j].command);
        free(*process_info);
        *process_info = NULL;
    }
    if (commands && *commands) {
        for (int j = 0; j < num_commands; j++) free((*commands)[j]);
        free(*commands);
        *commands = NULL;
    }
}

/*  History and Execution details functions */
void add_to_history(char *command, ProcessInfo *processes, int num_processes, struct timeval start, struct timeval end) {
    HistoryEntry *entry = malloc(sizeof(HistoryEntry));
    if (!entry) return;
    entry->full_command = xstrdup(command);
    entry->command_number = ++COMMAND_NUMBER;
    entry->num_processes = num_processes;
    entry->start_time = start;
    entry->end_time = end;
    entry->processes = malloc(sizeof(ProcessInfo) * num_processes);
    if (!entry->processes) { free(entry->full_command); free(entry); return; }
    for (int i = 0; i < num_processes; ++i) {
        entry->processes[i].command = xstrdup(processes[i].command);
        entry->processes[i].pid = processes[i].pid;
        entry->processes[i].exit_status = processes[i].exit_status;
    }
    entry->next = NULL;
    if (!HISTORY_HEAD) HISTORY_HEAD = HISTORY_TAIL = entry;
    else { HISTORY_TAIL->next = entry; HISTORY_TAIL = entry; }
}

double get_time_diff_ms(struct timeval start, struct timeval end) {
    double s = (double)start.tv_sec * 1000.0 + (double)start.tv_usec / 1000.0;
    double e = (double)end.tv_sec * 1000.0 + (double)end.tv_usec / 1000.0;
    return e - s;
}

void show_execution_details() {
    HistoryEntry *cur = HISTORY_HEAD;
    int num = 0;
    printf("\n\n\n========= Execution Details (SHELL) =========\n");
    while (cur) {
        printf("\nCommand %d: %s\n", ++num, cur->full_command ? cur->full_command : "(null)");
        time_t st = cur->start_time.tv_sec; time_t en = cur->end_time.tv_sec;
        printf("Started: %s", ctime(&st));
        printf("Ended:   %s", ctime(&en));
        printf("Duration: %.3f ms\n", get_time_diff_ms(cur->start_time, cur->end_time));
        printf("Process details:\n");
        for (int i = 0; i < cur->num_processes; ++i) {
            printf("  Process %d (PID: %d): %s ", i+1, cur->processes[i].pid, cur->processes[i].command);
            if (cur->processes[i].exit_status == 0) printf(" [SUCCESS]\n");
            else printf(" [EXIT %d]\n", cur->processes[i].exit_status);
        }
        cur = cur->next;
    }
    printf("\n=============================================\n\n");
}

void show_history() {
    HistoryEntry *cur = HISTORY_HEAD;
    int num = 0;
    printf("History of Commands:\n");
    if (!cur) printf("No commands in history.\n");
    while (cur) {
        printf("%d: %s\n", ++num, cur->full_command);
        cur = cur->next;
    }
}

void cleanHistory() {
    HISTORY_TAIL = NULL;
    while (HISTORY_HEAD) {
        HistoryEntry *t = HISTORY_HEAD;
        HISTORY_HEAD = HISTORY_HEAD->next;
        for (int i = 0; i < t->num_processes; ++i) free(t->processes[i].command);
        free(t->processes);
        free(t->full_command);
        free(t);
    }
}

/*  Functions for job name retrieval */
void add_job_name(pid_t pid, const char *job) {
    JobNameEntry *entry = malloc(sizeof(JobNameEntry));
    if (!entry) return;
    
    entry->pid = pid;
    entry->executable_name = strdup(job);
    entry->next = JOB_NAME_LIST;
    JOB_NAME_LIST = entry;
}

const char* get_job_name(pid_t pid) {
    for (JobNameEntry *cur = JOB_NAME_LIST; cur; cur = cur->next) {
        if (cur->pid == pid) {
            return cur->executable_name;
        }
    }
    return NULL;
}

void cleanup_job_names() {
    while (JOB_NAME_LIST) {
        JobNameEntry *temp = JOB_NAME_LIST;
        JOB_NAME_LIST = JOB_NAME_LIST->next;
        free(temp->executable_name);
        free(temp);
    }
}

void receive_and_print_results(int result_fd) {
    printf("\n========= Scheduler Job Results =========\n");
    fflush(stdout);
    
    /* Block SIGINT during result reading to prevent interruption */
    sigset_t sigset, oldset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigprocmask(SIG_BLOCK, &sigset, &oldset);
    
    int job_count = 0;
    while (1) {
        JobResult result;
        ssize_t r = read(result_fd, &result, sizeof(result));
        
        if (r == 0) {
            // EOF - scheduler closed the pipe
            break;
        }
        
        if (r == -1) {
            if (errno == EINTR) {
                continue;  // Retry if interrupted
            }
            perror("read from scheduler");
            break;
        }
        
        if (r != sizeof(result)) {
            fprintf(stderr, "Warning: Partial read from scheduler (%zd bytes)\n", r);
            break;
        }
        
        // Check for end marker
        if (result.pid == -1) {
            break;
        }
        
        // Lookup the executable name
        const char *name = get_job_name(result.pid);
        if (!name) name = "(unknown)";
        
        printf("JOB_FINISHED\t%-10s\tpid=%d\tcompletion_slices=%ld\trun_slices=%ld\twait_slices=%ld\n",
               name, (int)result.pid, result.completion_slices, 
               result.run_slices, result.wait_slices);
        fflush(stdout);
        
        job_count++;
    }
    
    /* Restore signal mask */
    sigprocmask(SIG_SETMASK, &oldset, NULL);
    
    if (job_count > 0) {
        printf("Total jobs completed: %d\n", job_count);
    } else {
        printf("No jobs completed\n");
    }
    printf("=========================================\n\n");
    fflush(stdout);
}

/* Writes to pipe b/w shell and scheduler */
int send_pid_to_scheduler(int pipe_fd, pid_t pid) {
    ssize_t w = write(pipe_fd, &pid, sizeof(pid));
    if (w == (ssize_t)sizeof(pid)) return 0;
    perror("write to scheduler");
    return -1;
}

/* Signal handlers */
static void my_handler(int signum) {
    if (signum != SIGINT) return;
    if (getpid() != SHELL_PID) return;
    SHUTDOWN_REQUESTED = 1;
}

static void sigchld_handler(int signum) {
    (void)signum;
    int saved_errno = errno;
    pid_t pid;
    int status;
    
    /* Reaping all zombie children except the scheduler */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == SCHEDULER_PID) {
            continue;
        }
    }
    
    errno = saved_errno;
}