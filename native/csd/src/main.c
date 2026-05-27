#include "core.h"

int main(int argc, char *argv[]) {
    char logmsg[256];
    engineinstancet engine;
    memset(&engine, 0, sizeof(engineinstancet));
    engine.port = DEFAULTPORT;
    size_t ramallocationmb = 128;
    int actionfound = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "start") == 0 || strcmp(argv[1], "begin") == 0) {
            actionfound = 1;
        }
    }
    handleshareargument(argc, argv);
    handlereceiveargument(argc, argv);
    executeexternalprocess(argc, argv);
    parsesystemarguments(argc, argv, &engine.port, &ramallocationmb);
    if (!actionfound && argc > 1 && argv[1][0] != '-') {
        engine.port = atoi(argv[1]);
        if (argc > 2 && argv[2][0] != '-') {
            ramallocationmb = (size_t)strtoul(argv[2], NULL, 10);
        }
    }
    writelog("Initializing socket infrastructure");
    initializerampool(&engine, ramallocationmb);
    engine.serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (engine.serverfd < 0) {
        snprintf(logmsg, sizeof(logmsg), "Socket generation failed %s", strerror(errno));
        writelog(logmsg);
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(engine.serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        snprintf(logmsg, sizeof(logmsg), "Socket options configuration failed %s", strerror(errno));
        writelog(logmsg);
    }
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY;
    serveraddr.sin_port = htons(engine.port);
    if (bind(engine.serverfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        snprintf(logmsg, sizeof(logmsg), "Binding failed on port %d %s", engine.port, strerror(errno));
        writelog(logmsg);
        close(engine.serverfd);
        exit(EXIT_FAILURE);
    }
    if (listen(engine.serverfd, SOMAXCONN) < 0) {
        snprintf(logmsg, sizeof(logmsg), "Listening configuration failed %s", strerror(errno));
        writelog(logmsg);
        close(engine.serverfd);
        exit(EXIT_FAILURE);
    }
    setnonblocking(engine.serverfd);
    snprintf(logmsg, sizeof(logmsg), "Engine listening on port %d", engine.port);
    writelog(logmsg);
    fd_set readfds;
    int maxfd;
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(engine.serverfd, &readfds);
        maxfd = engine.serverfd;
        for (int i = 0; i < MAXCLIENTS; i++) {
            int fd = engine.sessions[i].clientfd;
            if (fd != -1) {
                FD_SET(fd, &readfds);
                if (fd > maxfd) {
                    maxfd = fd;
                }
            }
        }
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        int activity = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (activity < 0 && errno != EINTR) {
            snprintf(logmsg, sizeof(logmsg), "Select operation failure %s", strerror(errno));
            writelog(logmsg);
        }
        if (activity == 0) {
            continue;
        }
        if (FD_ISSET(engine.serverfd, &readfds)) {
            acceptnewconnection(&engine);
        }
        for (int i = 0; i < MAXCLIENTS; i++) {
            int fd = engine.sessions[i].clientfd;
            if (fd != -1 && FD_ISSET(fd, &readfds)) {
                handleclientdata(&engine, i);
            }
        }
    }
    close(engine.serverfd);
    munmap(engine.rampool, engine.rampoolsize);
    return 0;
}
