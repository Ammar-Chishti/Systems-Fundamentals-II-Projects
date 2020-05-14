#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <wait.h>

#include "debug.h"
#include "polya.h"
void sigchld_handler(int sig);
struct Worker* find_worker(pid_t workerPid);
int are_all_workers_idle(int workers);
int assign_all_workers_problem(int workers);
void cancel_all_other_workers(int workers);
void terminate_all_workers(int workers);
int are_all_workers_terminated(int workers);

#define PARENT_READ     readpipe[0]
#define CHILD_WRITE     readpipe[1]
#define CHILD_READ      writepipe[0]
#define PARENT_WRITE    writepipe[1]

int wstatus;
struct Worker {
    pid_t pid;
    int id;
    struct problem* prob;
    int writepipe[2];   /* parent -> child */
    int readpipe[2];    /* child -> parent */
    int prev_state;
    int state;
};

volatile sig_atomic_t workersIdle = 0;
volatile sig_atomic_t workersStopped = 0;
volatile sig_atomic_t problemsSolved = 0;
volatile sig_atomic_t workersAlive = 0;
volatile sig_atomic_t isCurrentProblemSolved = 0;
int totalWorkers;
struct Worker* problemWorkers;

/*
 * master
 * (See polya.h for specification.)
 */
int master(int workers) {
    sf_start();
    signal(SIGCHLD, sigchld_handler);
    totalWorkers = workers;
    problemWorkers = malloc(workers * sizeof(struct Worker));

    for (int i = 0; i < workers; ++i) {     // This for loop is responsible for initializing everything
        struct Worker* currentWorker = &problemWorkers[i];

        currentWorker->readpipe[0] = -1;
        currentWorker->readpipe[1] = -1;
        currentWorker->writepipe[0] = -1;
        currentWorker->writepipe[1] = -1;
        if (pipe(currentWorker->readpipe) < 0 || pipe(currentWorker->writepipe) < 0) {
            debug("Creating the read/write pipes for worker %d has failed", currentWorker->pid);
        }

        currentWorker->id = i;
        currentWorker->state = WORKER_STARTED;

        currentWorker->pid = fork();
        if (currentWorker->pid < 0) {
            debug("Fork has failed for worker #%d", i);
        } else if (currentWorker->pid == 0) { // child process
            dup2(currentWorker->CHILD_READ, STDIN_FILENO);
            close(currentWorker->PARENT_WRITE);
            dup2(currentWorker->CHILD_WRITE, STDOUT_FILENO);
            close(currentWorker->PARENT_READ);

            debug("Starting worker %d", i);
            if (execl("./bin/polya_worker", "poly_worker", NULL) < 0) {
                debug("Execl failed for worker with process %d", currentWorker->pid);
            }
        }
    }

    struct result* resul;
    int allProblemsSolved = 0;
    while (!allProblemsSolved) {
        while (!are_all_workers_idle(workers)) {
            debug("Workers Stopped Currently = %d, Workers Idle Currently = %d", workersStopped, workersIdle);
            if (workersStopped) {
                for (int i = 0; i < workers; ++i) {
                    if (problemWorkers[i].state == WORKER_STOPPED) {
                        if (!(isCurrentProblemSolved)) {
                            resul = malloc(sizeof(struct result));
                            debug("Read result (fd = %d)", problemWorkers[i].PARENT_READ);
                            read(problemWorkers[i].PARENT_READ, resul, sizeof(struct result));
                            if (!(resul)) {
                                debug("Result for worker (pid = %d, id = %d) is NULL, setting Worker to IDLE", problemWorkers[i].pid, problemWorkers[i].id);
                                problemWorkers[i].prev_state = WORKER_STOPPED;
                                problemWorkers[i].state = WORKER_IDLE;
                                problemWorkers[i].prob = NULL;
                                workersStopped--;
                                workersIdle++;
                                continue;
                            }
                            resul = realloc(resul, resul->size);

                            int resul_data_size = resul->size - sizeof(struct result);
                            if (resul_data_size) {
                                debug("Read result data (fd = %d, nbytes = %d)", problemWorkers[i].PARENT_READ, resul_data_size);
                                read(problemWorkers[i].PARENT_READ, resul->data, resul_data_size);
                            }

                            debug("Got result from worker %d, (pid = %d): size = %ld, failed = %d", problemWorkers[i].id, problemWorkers[i].pid, resul->size, resul->failed);
                            sf_recv_result(problemWorkers[i].pid, resul);
                            
                            int failed = resul->failed;
                            int isSolved;
                            if (failed != 0) {
                                debug("Since worker %d (pid = %d) could not solve the problem, we are not going to post the result and continue to search for a solution", problemWorkers[i].id, problemWorkers[i].pid);
                                free(resul);
                                continue;
                            } else {
                                isSolved = post_result(resul, problemWorkers[i].prob);
                                debug("isSolved is %d", isSolved);
                                problemsSolved++;
                                isCurrentProblemSolved = 1;
                            }

                            sf_change_state(problemWorkers[i].pid, problemWorkers[i].state, WORKER_IDLE);
                            debug("Set state of worker %d (pid = %d): %d -> %d", problemWorkers[i].id, problemWorkers[i].pid, problemWorkers[i].state, WORKER_IDLE);
                            problemWorkers[i].prev_state = WORKER_STOPPED;
                            problemWorkers[i].state = WORKER_IDLE;
                            problemWorkers[i].prob = NULL;
                            workersStopped--;
                            workersIdle++;
                            free(resul);

                            debug("Workers stopped: %d", workersStopped);
                            debug("Workers idle: %d", workersIdle);
                            debug("Problems solved: %d", problemsSolved);
                            cancel_all_other_workers(workers);
                        } else {
                            debug("We have already solved the problem. Changing from stopped to idle.");
                            debug("Set state of worker %d (pid = %d): %d -> %d", problemWorkers[i].id, problemWorkers[i].pid, problemWorkers[i].state, WORKER_RUNNING );
                            sf_change_state(problemWorkers[i].pid, WORKER_STOPPED, WORKER_IDLE);
                            problemWorkers[i].prev_state = WORKER_STOPPED;
                            problemWorkers[i].state = WORKER_IDLE;
                            problemWorkers[i].prob = NULL;                         
                        }
                    }
                }
            }
            debug("Waiting because we have not solved all problems yet");
            sleep(1);
        }
        if (!(assign_all_workers_problem(workers))) {
            debug("Terminating all workers");
            terminate_all_workers(workers);
            sf_end();
            return EXIT_SUCCESS;
        }
    }
    
    return EXIT_FAILURE;
}

void sigchld_handler(int sig) {
    debug("Entering sigchld handler");
    pid_t workerPid;
    while ((workerPid = waitpid(-1, &wstatus, WCONTINUED | WSTOPPED | WNOHANG)) > 0) {
        if (workerPid < 0) {
            debug("Waitpid failed in master");
        }
        struct Worker* currentWorker = find_worker(workerPid);
        if (!(currentWorker)) {
            debug("Could not find currentWorker from workers list");
            exit(EXIT_FAILURE);
        }

        if (WIFSTOPPED(wstatus)) {
            debug("Worker %d (pid = %d, state = %d) has stopped with status %d", currentWorker->id, currentWorker->pid, currentWorker->state, wstatus);
            if (currentWorker->state == WORKER_STARTED) {
                debug("Set state of worker %d (pid = %d): %d -> %d", currentWorker->id, currentWorker->pid, currentWorker->state, WORKER_IDLE);
                sf_change_state(currentWorker->pid, WORKER_STARTED, WORKER_IDLE);
                workersIdle++;
                workersAlive++;
                currentWorker->prev_state = WORKER_STARTED;
                currentWorker->state = WORKER_IDLE;
                currentWorker->prob = NULL;
            } else if (currentWorker->state == WORKER_RUNNING || currentWorker->state == WORKER_CONTINUED) {
                debug("Set state of worker %d (pid = %d): %d -> %d", currentWorker->id, currentWorker->pid, currentWorker->state, WORKER_STOPPED);
                sf_change_state(currentWorker->pid, currentWorker->state, WORKER_STOPPED);
                currentWorker->prev_state = currentWorker->state;
                currentWorker->state = WORKER_STOPPED;
                workersStopped++;
            }
        }
        if (WIFCONTINUED(wstatus)) {
            if (currentWorker->state == WORKER_CONTINUED) {
                debug("Set state of worker %d (pid = %d): %d -> %d", currentWorker->id, currentWorker->pid, currentWorker->state, WORKER_RUNNING );
                sf_change_state(currentWorker->pid, WORKER_CONTINUED, WORKER_RUNNING);
                currentWorker->prev_state = WORKER_CONTINUED;
                currentWorker->state = WORKER_RUNNING;
            }
        }
        if (WIFEXITED(wstatus)) {
            debug("Set state of worker %d (pid = %d): %d -> %d", currentWorker->id, currentWorker->pid, currentWorker->prev_state, WORKER_EXITED);
            sf_change_state(currentWorker->pid, currentWorker->state, WORKER_EXITED);
            workersAlive--;
            currentWorker->state = WORKER_EXITED;
        }
    }
}

struct Worker* find_worker(pid_t workerPid) {
    for (int i = 0; i < totalWorkers; ++i) {
        if (workerPid == problemWorkers[i].pid) {
            return (struct Worker*) (&problemWorkers[i]);
        }
    }
    return NULL;
}

int are_all_workers_idle(int workers) {
    for (int i = 0; i < workers; ++i) {
        if (problemWorkers[i].state != WORKER_IDLE) {
            return 0;
        }
    }
    return 1;
}

int assign_all_workers_problem(int workers) {
    for (int i = 0; i < workers; ++i) {
        struct problem* prob = get_problem_variant(workers, i);
        isCurrentProblemSolved = 0;
        if (!(prob)) {
            return 0;
        }
        debug("Set state of worker %d (pid = %d): %d -> %d", problemWorkers[i].id, problemWorkers[i].pid, problemWorkers[i].state, WORKER_CONTINUED);
        sf_send_problem(problemWorkers[i].pid, prob);
        sf_change_state(problemWorkers[i].pid, problemWorkers[i].state, WORKER_CONTINUED);
        problemWorkers[i].prev_state = WORKER_IDLE;
        problemWorkers[i].state = WORKER_CONTINUED;
        workersIdle--;
        problemWorkers[i].prob = prob;

        debug("Write problem (fd = %d, size = %ld)", problemWorkers[i].PARENT_WRITE, prob->size);
        write(problemWorkers[i].PARENT_WRITE, prob, prob->size);
        kill(problemWorkers[i].pid, SIGCONT);
    }
    return 1;
}

void cancel_all_other_workers(int workers) {
    debug("Cancelling all other workers");
    for (int i = 0; i < workers; ++i) {
        debug("Worker state is %d", problemWorkers[i].state);
        if (problemWorkers[i].state == WORKER_RUNNING || problemWorkers[i].state == WORKER_CONTINUED) {
            sf_cancel(problemWorkers[i].pid);
            debug("Signaling worker %d (pid = %d) to abandon solving problem", problemWorkers[i].id, problemWorkers[i].pid);
            kill(problemWorkers[i].pid, SIGHUP);
        }
    }
}

void terminate_all_workers(int workers) {
    for (int i = 0; i < workers; ++i) {
        kill(problemWorkers[i].pid, SIGCONT);
        kill(problemWorkers[i].pid, SIGTERM);
    }
    while (!(are_all_workers_terminated(workers))) {
        debug("Waiting for all workers to terminate. Workers alive = %d", workersAlive);
        sleep(1);
    }
}

int are_all_workers_terminated(int workers) {
    for (int i = 0; i < workers; ++i) {
        if (problemWorkers[i].state != WORKER_EXITED) {
            return 0;
        }
    }
    return 1;
}
