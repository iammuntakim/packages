#include "core.h"

void writelog(const char *message) {
    time_t now;
    time(&now);
    struct tm *tminfo = localtime(&now);
    char timestr[9];
    strftime(timestr, sizeof(timestr), "%H:%M:%S", tminfo);
    FILE *logfp = fopen(LOGFILE, "a");
    if (logfp) {
        fprintf(logfp, "[%s] %s\n", timestr, message);
        fclose(logfp);
    }
    printf("[%s] %s\n", timestr, message);
}

void setnonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void initializerampool(engineinstancet *engine, size_t megabytes) {
    char logmsg[256];
    engine->rampoolsize = megabytes * 1024 * 1024;
    engine->rampool = mmap(NULL, engine->rampoolsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (engine->rampool == MAP_FAILED) {
        snprintf(logmsg, sizeof(logmsg), "RAM allocation failed %s", strerror(errno));
        writelog(logmsg);
        exit(EXIT_FAILURE);
    }
    memset(engine->rampool, 0, engine->rampoolsize);
    engine->sessions = (clientsessiont *)engine->rampool;
    for (int i = 0; i < MAXCLIENTS; i++) {
        engine->sessions[i].clientfd = -1;
    }
    snprintf(logmsg, sizeof(logmsg), "Allocated %zu MB memory pool at %p", megabytes, engine->rampool);
    writelog(logmsg);
}

void parsesystemarguments(int argc, char *argv[], int *port, size_t *ram) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0 && i + 1 < argc) {
            *port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-ram") == 0 && i + 1 < argc) {
            *ram = (size_t)strtoul(argv[++i], NULL, 10);
        }
    }
}

void executeexternalprocess(int argc, char *argv[]) {
    int argsindex = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "[-args]") == 0 || strcmp(argv[i], "-args") == 0) {
            argsindex = i + 1;
            break;
        }
    }
    if (argsindex != -1 && argsindex < argc) {
        char execcmd[2048] = {0};
        for (int i = argsindex; i < argc; i++) {
            strcat(execcmd, argv[i]);
            strcat(execcmd, " ");
        }
        char logmsg[2300];
        snprintf(logmsg, sizeof(logmsg), "Launching external custom assets system %s", execcmd);
        writelog(logmsg);
        int sysstatus = system(execcmd);
        snprintf(logmsg, sizeof(logmsg), "External process exited with status %d", sysstatus);
        writelog(logmsg);
        exit(sysstatus);
    }
}
