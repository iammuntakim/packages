#include "core.h"

void handleclientdata(engineinstancet *engine, int index) {
    char logmsg[512];
    clientsessiont *session = &engine->sessions[index];
    ssize_t bytesread = recv(session->clientfd, session->rxbuffer, BUFFERSIZE - 1, 0);
    if (bytesread <= 0) {
        if (bytesread == 0) {
            snprintf(logmsg, sizeof(logmsg), "Client disconnected %s %d", inet_ntoa(session->address.sin_addr), ntohs(session->address.sin_port));
            writelog(logmsg);
        } else {
            snprintf(logmsg, sizeof(logmsg), "Read error on client %s %d %s", inet_ntoa(session->address.sin_addr), ntohs(session->address.sin_port), strerror(errno));
            writelog(logmsg);
        }
        close(session->clientfd);
        session->clientfd = -1;
        engine->activeconnections--;
        snprintf(logmsg, sizeof(logmsg), "%d clients connected", engine->activeconnections);
        writelog(logmsg);
        return;
    }
    session->rxbuffer[bytesread] = '\0';
    snprintf(logmsg, sizeof(logmsg), "Received %zd bytes from %s %d", bytesread, inet_ntoa(session->address.sin_addr), ntohs(session->address.sin_port));
    writelog(logmsg);
    ssize_t bytessent = send(session->clientfd, session->rxbuffer, bytesread, 0);
    if (bytessent < 0) {
        snprintf(logmsg, sizeof(logmsg), "Failed to echo data back to %s %d", inet_ntoa(session->address.sin_addr), ntohs(session->address.sin_port));
        writelog(logmsg);
    }
}

void acceptnewconnection(engineinstancet *engine) {
    char logmsg[256];
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof(clientaddr);
    int newfd = accept(engine->serverfd, (struct sockaddr *)&clientaddr, &addrlen);
    if (newfd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            snprintf(logmsg, sizeof(logmsg), "Accept failure %s", strerror(errno));
            writelog(logmsg);
        }
        return;
    }
    setnonblocking(newfd);
    int slotfound = 0;
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (engine->sessions[i].clientfd == -1) {
            engine->sessions[i].clientfd = newfd;
            engine->sessions[i].address = clientaddr;
            engine->sessions[i].rxbytes = 0;
            engine->activeconnections++;
            slotfound = 1;
            snprintf(logmsg, sizeof(logmsg), "Accepted connection from %s %d assigned to slot %d", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), i);
            writelog(logmsg);
            break;
        }
    }
    if (!slotfound) {
        snprintf(logmsg, sizeof(logmsg), "Capacity reached. Rejecting %s %d", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
        writelog(logmsg);
        close(newfd);
    }
}
