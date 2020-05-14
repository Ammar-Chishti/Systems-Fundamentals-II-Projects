#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
/*  MY INCLUDES */
#include <stdlib.h>
#include <semaphore.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"
/* MY INCLUDES */
#include "csapp.h"

static void terminate(int status);
void sighup_handler(int sig);

/*
 * "PBX" telephone exchange simulation.
 *
 * Usage: pbx <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    if (argc != 3) {
        debug("3 command line arguments were not found, program terminating");
        return EXIT_FAILURE;
    }
    if (strcmp("-p", argv[1]) != 0) {
        debug("Second command line argument is not '-p', program terminating");
        return EXIT_FAILURE;
    }
    char* portNumberAddr = argv[2];

    struct sigaction action;
    action.sa_handler = sighup_handler;
    if (sigaction(SIGHUP, &action, NULL) < 0) {
        debug("Installing Sighup handler failed");
    }

    // Perform required initialization of the PBX module.
    debug("Initializing PBX...");
    pbx = pbx_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function pbx_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.
    int* connfdp;
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    listenfd = open_listenfd(portNumberAddr);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA*) &clientaddr, &clientlen);
        pthread_create(&tid, NULL, (void*) pbx_client_service, connfdp);
    }

    fprintf(stderr, "You have to finish implementing main() "
	    "before the PBX server will function.\n");

    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    debug("Shutting down PBX...");
    pbx_shutdown(pbx);
    debug("PBX server terminating");
    exit(status);
}

void sighup_handler(int sig) {
    terminate(EXIT_SUCCESS);
}
