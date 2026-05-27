#ifndef COREH
#define COREH

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#define DEFAULTPORT 8080
#define MAXCLIENTS 1024
#define BUFFERSIZE 4096
#define LOGFILE "csd.log"

typedef struct {
    int clientfd;
    struct sockaddr_in address;
    char rxbuffer[BUFFERSIZE];
    size_t rxbytes;
} clientsessiont;

typedef struct {
    int serverfd;
    int port;
    size_t rampoolsize;
    void *rampool;
    clientsessiont *sessions;
    int activeconnections;
} engineinstancet;

void writelog(const char *message);
void setnonblocking(int fd);
void initializerampool(engineinstancet *engine, size_t megabytes);
void handleclientdata(engineinstancet *engine, int index);
void acceptnewconnection(engineinstancet *engine);
void executeexternalprocess(int argc, char *argv[]);
void parsesystemarguments(int argc, char *argv[], int *port, size_t *ram);
void handleshareargument(int argc, char *argv[]);
void handlereceiveargument(int argc, char *argv[]);

#endif
