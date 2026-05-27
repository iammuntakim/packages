#include "core.h"

void handleshareargument(int argc, char *argv[]) {
    int shareindex = -1;
    int port = DEFAULTPORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            shareindex = i + 1;
        }
    }
    if (shareindex == -1 || shareindex >= argc) {
        return;
    }
    char *filepath = argv[shareindex];
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        char errormsg[512];
        snprintf(errormsg, sizeof(errormsg), "Cannot open file '%s'", filepath);
        writelog(errormsg);
        exit(EXIT_FAILURE);
    }
    struct stat st;
    stat(filepath, &st);
    size_t totalbytes = st.st_size;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        writelog("Share socket creation failed");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        char binderr[256];
        snprintf(binderr, sizeof(binderr), "Share bind failed on port %d", port);
        writelog(binderr);
        close(sockfd);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    listen(sockfd, 1);
    char initmsg[512];
    snprintf(initmsg, sizeof(initmsg), "Sharing file %s (%zu bytes) on port %d", filepath, totalbytes, port);
    writelog(initmsg);
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int clientfd = accept(sockfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (clientfd >= 0) {
        char connmsg[256];
        snprintf(connmsg, sizeof(connmsg), "Client %s connected for transfer", inet_ntoa(clientaddr.sin_addr));
        writelog(connmsg);
        char buffer[BUFFERSIZE];
        size_t totalread = 0;
        ssize_t readbytes;
        time_t lastlog = 0;
        int broken = 0;
        while ((readbytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            ssize_t sentbytes = send(clientfd, buffer, readbytes, 0);
            if (sentbytes < 0) {
                broken = 1;
                break;
            }
            totalread += sentbytes;
            time_t now = time(NULL);
            if (now != lastlog || totalread == totalbytes) {
                int percentage = (int)((totalread * 100) / totalbytes);
                printf("\rSent %zu of %zu bytes (%d%%)", totalread, totalbytes, percentage);
                fflush(stdout);
                lastlog = now;
            }
        }
        printf("\n");
        if (broken || totalread < totalbytes) {
            writelog("Client disconnected unexpectedly");
        }
        close(clientfd);
    }
    close(sockfd);
    fclose(file);
    exit(EXIT_SUCCESS);
}

void handlereceiveargument(int argc, char *argv[]) {
    int receiveindex = -1;
    int port = DEFAULTPORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            receiveindex = i + 1;
        }
    }
    if (receiveindex == -1 || receiveindex >= argc) {
        return;
    }
    char *filepath = argv[receiveindex];
    FILE *file = fopen(filepath, "wb");
    if (!file) {
        char errormsg[512];
        snprintf(errormsg, sizeof(errormsg), "Receive operation failure cannot open file %s", filepath);
        writelog(errormsg);
        exit(EXIT_FAILURE);
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        writelog("Receive socket creation failed");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        char binderr[256];
        snprintf(binderr, sizeof(binderr), "Receive bind failed on port %d", port);
        writelog(binderr);
        close(sockfd);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    listen(sockfd, 1);
    char initmsg[512];
    snprintf(initmsg, sizeof(initmsg), "Listening to receive file %s on port %d", filepath, port);
    writelog(initmsg);
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int clientfd = accept(sockfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (clientfd >= 0) {
        char connmsg[256];
        snprintf(connmsg, sizeof(connmsg), "Client %s connected for transfer", inet_ntoa(clientaddr.sin_addr));
        writelog(connmsg);
        char buffer[BUFFERSIZE];
        ssize_t readbytes;
        size_t totalreceived = 0;
        time_t lastlog = 0;
        while ((readbytes = recv(clientfd, buffer, sizeof(buffer), 0)) > 0) {
            size_t written = fwrite(buffer, 1, readbytes, file);
            if (written < (size_t)readbytes) {
                break;
            }
            totalreceived += written;
            time_t now = time(NULL);
            if (now != lastlog) {
                printf("\rReceived %zu bytes", totalreceived);
                fflush(stdout);
                lastlog = now;
            }
        }
        printf("\n");
        char finalmsg[256];
        snprintf(finalmsg, sizeof(finalmsg), "Transfer completed, received %zu bytes", totalreceived);
        writelog(finalmsg);
        close(clientfd);
    }
    close(sockfd);
    fclose(file);
    exit(EXIT_SUCCESS);
}
