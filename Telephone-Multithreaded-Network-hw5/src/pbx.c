#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <string.h>

#include "pbx.h"
#include "debug.h"
#include "csapp.h"

#define ON_HOOK "ON HOOK %d\n"
#define RINGING "RINGING\n"
#define DIAL_TONE "DIAL TONE\n"
#define RING_BACK "RING BACK\n"
#define BUSY_SIGNAL "BUSY SIGNAL\n"
#define CONNECTED "CONNECTED %d\n"
#define PBX_ERROR "ERROR\n"
//#define CHAT "CHAT..."

int writeState(TU_STATE state, int fd, int dialOrConnectedNumber);
int tu_fileno(TU* tu);

sem_t mutex;

typedef struct tu {
    TU_STATE state;
    int extensionNumber;
    int connectedNumber;
} TU;

typedef struct pbx {
    TU telephoneUnits[PBX_MAX_EXTENSIONS];
    int size;
} PBX;

PBX* pbx_init() {
    debug("New PBX created");
    PBX* newPBX = malloc(sizeof(PBX));
    sem_init(&mutex, 0, 1);
    for (int i = 0; i < PBX_MAX_EXTENSIONS; ++i) {
        newPBX->telephoneUnits[i].state = -1;
        newPBX->telephoneUnits[i].extensionNumber = -1;
        newPBX->telephoneUnits[i].connectedNumber = -1;
    }
    return newPBX;
}

void pbx_shutdown(PBX* pbx) {
    P(&mutex);
    free(pbx);
    debug("PBX is shutting down");
    V(&mutex);
}

TU* pbx_register(PBX* pbx, int fd) {
    P(&mutex);
    debug("REGISTERING TU %d", fd);
    TU* currentTU = &pbx->telephoneUnits[fd];
    if ((currentTU->state == -1) && (currentTU->extensionNumber == -1) && (currentTU->connectedNumber == -1)) {
        debug("Successfully registering TU with fd %d", fd);
        currentTU->state = TU_ON_HOOK;
        currentTU->extensionNumber = fd;
        pbx->size += 1;
        writeState(TU_ON_HOOK, fd, fd);
    } else {
        debug("TU with fd %d has already been registered. Returning registered TU", fd);
    }
    V(&mutex);
    return currentTU;
}

int pbx_unregister(PBX* pbx, TU* tu) {
    P(&mutex);
    if ((tu->state == -1) && (tu->extensionNumber == -1) && (tu->connectedNumber == -1)) {
        debug("TU with fd %d is unregistered already. returning -1", tu_fileno(tu));
        return 0;
    } else {
        if (tu->connectedNumber != -1) {
            TU* callee = &pbx->telephoneUnits[tu->connectedNumber];
            if ((callee->state == TU_CONNECTED) || (callee->state == TU_RING_BACK)) {
                callee->state = TU_DIAL_TONE;
                writeState(TU_DIAL_TONE, callee->extensionNumber, -1);
            } else if (callee->state == TU_RINGING) {
                callee->state = TU_ON_HOOK;
                writeState(TU_ON_HOOK, callee->extensionNumber, callee->extensionNumber);
            } 
            callee->connectedNumber = -1;
        }
        debug("Successfully unregistering TU with fd %d", tu_fileno(tu));
        tu->state = -1;
        tu->extensionNumber = -1;
        tu->connectedNumber = -1;
        pbx->size -= 1;
    }
    V(&mutex);
    return 0;
}

int tu_fileno(TU* tu) {
    return tu->extensionNumber;
}

int tu_extension(TU* tu) {
    return tu->extensionNumber;
}

int tu_pickup(TU* tu) {
    P(&mutex);
    if (tu->state == TU_ON_HOOK) {
        tu->state = TU_DIAL_TONE;
        writeState(TU_DIAL_TONE, tu->extensionNumber, -1);
    } else if (tu->state == TU_RINGING) {
        tu->state = TU_CONNECTED;
        TU* caller = &pbx->telephoneUnits[tu->connectedNumber];
        caller->state = TU_CONNECTED;
        writeState(TU_CONNECTED, tu->extensionNumber, tu->connectedNumber);
        writeState(TU_CONNECTED, caller->extensionNumber, caller->connectedNumber);
    } else if (tu->state == TU_CONNECTED) {
        writeState(TU_CONNECTED, tu->extensionNumber, tu->connectedNumber);
    } else {
        writeState(tu->state, tu->extensionNumber, -1);
    }
    V(&mutex);
    return 0;
}

int tu_hangup(TU* tu) {
    P(&mutex);
    tu->state = TU_ON_HOOK;
    writeState(TU_ON_HOOK, tu->extensionNumber, tu->extensionNumber);
    if (tu->connectedNumber != -1) {
        TU* callee = &pbx->telephoneUnits[tu->connectedNumber];
        if ((callee->state == TU_CONNECTED) || (callee->state == TU_RING_BACK)) {
            callee->state = TU_DIAL_TONE;
            writeState(TU_DIAL_TONE, callee->extensionNumber, -1);
        } else if (callee->state == TU_RINGING) {
            callee->state = TU_ON_HOOK;
            writeState(TU_ON_HOOK, callee->extensionNumber, callee->extensionNumber);
        }
        tu->connectedNumber = -1;
        callee->connectedNumber = -1;
    }
    V(&mutex);
    return 0;
}

int tu_dial(TU* tu, int ext) {
    P(&mutex);
    TU* callee = &pbx->telephoneUnits[ext];
    if ((tu->state == TU_ERROR) || (tu->state == TU_BUSY_SIGNAL)) {
        writeState(tu->state, tu->extensionNumber, -1);
        V(&mutex);
        return 0;
    } else if (tu->state == TU_DIAL_TONE) {
        if (callee->state == -1) {  // If the callee hasn't been registered yet
            tu->state = TU_ERROR;
            writeState(TU_ERROR, tu->extensionNumber, -1);
            V(&mutex);
            return 0;
        } else if (callee->state == TU_ON_HOOK) {
            tu->state = TU_RING_BACK;
            callee->state = TU_RINGING;
            tu->connectedNumber = callee->extensionNumber;
            callee->connectedNumber = tu->extensionNumber;
            writeState(TU_RING_BACK, tu->extensionNumber, -1);
            writeState(TU_RINGING, callee->extensionNumber, -1);
            V(&mutex);
            return 0;
        } else {
            tu->state = TU_BUSY_SIGNAL;
            writeState(TU_BUSY_SIGNAL, tu->extensionNumber, -1);
            V(&mutex);
            return 0;
        }
    } else {
        writeState(tu->state, tu->extensionNumber, tu->extensionNumber);
    }
    V(&mutex);
    return 0;
}

int tu_chat(TU* tu, char* msg) {
    P(&mutex);
    if ((tu->state == TU_ERROR) || (tu->state == TU_BUSY_SIGNAL)) {
        writeState(tu->state, tu->extensionNumber, -1);
        V(&mutex);
        return 0;
    }
    if (tu->state != TU_CONNECTED) {
        writeState(tu->state, tu->extensionNumber, tu->extensionNumber);
        V(&mutex);
        return 0;
    }
    if (tu->connectedNumber != -1) {
        TU* callee = &pbx->telephoneUnits[tu->connectedNumber];
        write(callee->extensionNumber, "CHAT ", 5);
        for (int i = 0; i < strlen(msg); ++i) {
            if (msg[i] != '\r' && msg[i] != '\n') {
                write(callee->extensionNumber, msg+i, 1);
            }
        }
        write(callee->extensionNumber, "\n", 1);
        writeState(TU_CONNECTED, tu->extensionNumber, tu->connectedNumber);
    }
    V(&mutex);
    return 0;
}

int writeState(TU_STATE state, int fd, int dialOrConnectedNumber) {
    char buff[50];
    if (state == 0) {
        int length = sprintf(buff, ON_HOOK, dialOrConnectedNumber);
        if (write(fd, buff, length) < 0)    { return -1; }
    } else if (state == 1) {
        int length = sprintf(buff, RINGING);
        if (write(fd, buff, length) < 0) { return -1; }
    } else if (state == 2) {
        int length = sprintf(buff, DIAL_TONE);
        if (write(fd, buff, length) < 0) { return -1; } 
    } else if (state == 3) {
        int length = sprintf(buff, RING_BACK);
        if (write(fd, buff, length) < 0) { return -1; } 
    } else if (state == 4) {
        int length = sprintf(buff, BUSY_SIGNAL);
        if (write(fd, buff, length) < 0) { return -1; }
    } else if (state == 5) {
        int length = sprintf(buff, CONNECTED, dialOrConnectedNumber);
        if (write(fd, buff, length) < 0) { return -1; }
    } else if (state == 6) {
        int length = sprintf(buff, PBX_ERROR);
        if (write(fd, buff, length) < 0) { return -1; }
    }
    return 1;
}



