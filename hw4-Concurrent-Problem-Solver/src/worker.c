#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

#include "debug.h"
#include "polya.h"
void sigcont_handler(int sig);
void sigterm_handler(int sig);
void sighup_handler(int sig);

volatile sig_atomic_t sigcont_active = 0;
volatile sig_atomic_t sigterm_active = 0;
volatile sig_atomic_t canceledp = 0;

/*
 * worker
 * (See polya.h for specification.)
 */
int worker(void) {
    debug("Initializing Worker with pid: %d", getpid());
    signal(SIGCONT, sigcont_handler);
    signal(SIGTERM, sigterm_handler);
    signal(SIGHUP, sighup_handler);

    while (1) {
        struct problem* prob;
        struct result* resul;
        debug("Worker is about to stop");
        raise (SIGSTOP);
        debug("Worker has continued");
        if (sigterm_active) {
            debug("Worker %d about to exit", getpid());
            sigterm_active = 0;
            exit(EXIT_SUCCESS);
        }
        if (sigcont_active) {
            debug("Worker %d about to solve a problem", getpid());
            prob = malloc(sizeof(struct problem));
            fread(prob, sizeof(struct problem), 1, stdin);
            debug("Initial problem worker size %ld", prob->size);
            if (ferror(stdin)) { return EXIT_FAILURE; }
            prob = realloc(prob, prob->size);
            int prob_data_size = prob->size - sizeof(struct problem);
            debug("Worker %d data size is %d and size is %ld", getpid(), prob_data_size, prob->size);
            if (prob_data_size) {
                fread(prob->data, sizeof(char), prob_data_size, stdin);
                if (ferror(stdin)) { return EXIT_FAILURE; }
            }
            resul = solvers[prob->type].solve(prob, &canceledp);
            debug("Worker %d has solved a problem", getpid());       // This needs to change
            canceledp = 0;
            if (!(resul)) {
                debug("Writing empty size");
                resul = malloc(sizeof(struct result));
                resul->size = 16;
                resul->failed = -1;
                fwrite(resul, sizeof(struct result), 1, stdout);
                fflush(stdout);
                free(prob);
                free(resul);
            } else {
                fwrite(resul, sizeof(struct result), 1, stdout);
                int result_size = resul->size - sizeof(struct result);
                if (result_size) {
                    fwrite(resul->data, sizeof(char), resul->size -  sizeof(struct result), stdout);
                }
                fflush(stdout);
                free(prob);
                free(resul);
            }
            sigcont_active = 0;
        }
    }
    return EXIT_FAILURE;
}

void sigcont_handler(int sig) {
    debug("Worker %d has received a Sigcont Signal and is entering handler", getpid());
    sigcont_active = 1;
}

void sighup_handler(int sig) {
    debug("Worker %d has received a Sighup Signal and is entering handler", getpid());
    canceledp = 1;
}

void sigterm_handler(int sig) {
    debug("Worker %d has received a Sigterm Signal and is entering handler", getpid());
    sigterm_active = 1;
}