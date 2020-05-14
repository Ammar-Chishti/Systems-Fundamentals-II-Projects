#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

#include "server.h"
#include "csapp.h"
#include "pbx.h"
#include "debug.h"

int isValidDialCommand(char* command);
char* isValidChatCommand(char* command);

void* pbx_client_service(void* argpv) {
    int connfd = *((int*) argpv);
    free(argpv);
    pthread_detach(pthread_self());

    TU* telephoneUnit = pbx_register(pbx, connfd);
    //debug("Successfully registered TU extension %d with addr %p", connfd, telephoneUnit);
    int clientfd = tu_fileno(telephoneUnit);
    FILE* clientRead = fdopen(clientfd, "r");

    int ch;
    char* operation;
    int operationSize = 0;
    int reachedEOF = 0;
    while (1) {
        operation = malloc(sizeof(char) * 1000000000);
        while ((ch = fgetc(clientRead))) {  // This while loop is to get the input from the user
            if (ch == EOF) {
                debug("EOF received on TU extension %d", tu_extension(telephoneUnit));
                reachedEOF = 1;
                break;
            } else if (ch == 10) {
                operation[operationSize] = ch;
                operationSize++;

                debug("Newline received on TU extension %d. Reallocing to size %d", tu_extension(telephoneUnit), operationSize);
                operation = realloc(operation, operationSize);
                break;
            } else {
                operation[operationSize] = ch;
                operationSize++;
            }
        }
        if (reachedEOF) {
            break;
        }

        if (strcmp(operation, "pickup\r\n") == 0) {
            debug("About to pickup TU extension %d", tu_extension(telephoneUnit));
            tu_pickup(telephoneUnit);
        }
        if (strcmp(operation, "hangup\r\n") == 0) {
            debug("About to hang up TU extension %d", tu_extension(telephoneUnit));
            tu_hangup(telephoneUnit); 
        }
        int extensionNumber = isValidDialCommand(operation);
        if (extensionNumber != -1) {
            debug("About to dial TU extension %d from original TU extension %d", extensionNumber, tu_extension(telephoneUnit));
            tu_dial(telephoneUnit, extensionNumber);
        }
        char* chatString = isValidChatCommand(operation);
        if (chatString != NULL) {
            debug("Chat Message is: %s", chatString);
            tu_chat(telephoneUnit, chatString);
        }

        free(operation);
        operationSize = 0;
    }

    debug("About to close TU extension %d", tu_extension(telephoneUnit));
    pbx_unregister(pbx, telephoneUnit);
    close(clientfd);
    return NULL;
}

int isValidDialCommand(char* command) {     // Returns -1 if the dial command string is invalid, else returns the dial # extension
    if ((command[0] != 'd') || (command[1] != 'i') || (command[2] != 'a') || (command[3] != 'l')) {
        return -1;
    }
    char* extensionNumberAddr = command + 4;
    int extensionNumber = atoi(extensionNumberAddr);
    if (extensionNumber < 1 || extensionNumber > 1023) {
        return -1;
    } 
    return extensionNumber;
}

char* isValidChatCommand(char* command) {   // Returns NULL if the chat command string is invalid, else returns the string to send
    if ((command[0] != 'c') || (command[1] != 'h') || (command[2] != 'a') || (command[3] != 't')) {
        return NULL;
    }
    return (command + 4);
}