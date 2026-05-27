#define _GNU_SOURCE
#include "sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>

static void replace_apt_to_mgr(const char *input, char *output, size_t max_len) {
    output[0] = '\0';
    const char *ptr = input;
    const char *match;
    while ((match = strstr(ptr, "apt")) != NULL) {
        size_t len = match - ptr;
        if (strlen(output) + len + 3 >= max_len) break;
        strncat(output, ptr, len);
        strcat(output, "mgr");
        ptr = match + 3;
    }
    if (strlen(output) + strlen(ptr) < max_len) {
        strcat(output, ptr);
    }
    
    char *cow = strstr(output, "This APT has Super Cow Powers.");
    if (cow) {
        memmove(cow, cow + 30, strlen(cow + 30) + 1);
    }
    cow = strstr(output, "This mgr has Super Cow Powers.");
    if (cow) {
        memmove(cow, cow + 30, strlen(cow + 30) + 1);
    }

    char *warn = strstr(output, "WARNING: mgr does not have a stable CLI interface.");
    if (warn) {
        char *end = strstr(warn, "scripts.");
        if (end) {
            end += 8;
            while (*end == '\r' || *end == '\n') {
                end++;
            }
            memmove(warn, end, strlen(end) + 1);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return 1;
    }

    int is_install = (strcmp(argv[1], "install") == 0);
    int is_android_sdk = 0;
    if (is_install && argc >= 3 && strcmp(argv[2], "android-sdk") == 0) {
        is_android_sdk = 1;
    }

    if (!is_android_sdk) {
        char passthrough_cmd[8192] = "apt";
        for (int i = 1; i < argc; i++) {
            strcat(passthrough_cmd, " ");
            strcat(passthrough_cmd, argv[i]);
        }
        
        int p[2];
        if (pipe(p) < 0) {
            return system(passthrough_cmd);
        }

        pid_t pid = fork();
        if (pid < 0) {
            return system(passthrough_cmd);
        }

        if (pid == 0) {
            close(p[0]);
            dup2(p[1], STDOUT_FILENO);
            dup2(p[1], STDERR_FILENO);
            close(p[1]);
            exit(system(passthrough_cmd));
        } else {
            close(p[1]);
            char buffer[4096];
            ssize_t n;
            while ((n = read(p[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[n] = '\0';
                
                char *output = malloc(n * 2 + 1);
                if (output) {
                    replace_apt_to_mgr(buffer, output, n * 2 + 1);
                    printf("%s", output);
                    free(output);
                } else {
                    printf("%s", buffer);
                }
            }
            close(p[0]);
            int status;
            waitpid(pid, &status, 0);
            return status;
        }
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int result = handle_sdk_installation();
    curl_global_cleanup();

    return result;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *buffer = NULL;
    int len = vasprintf(&buffer, format, args);
    va_end(args);
    if (len < 0 || !buffer) return -1;
    char *output = malloc(len * 2 + 1);
    if (!output) {
        free(buffer);
        return -1;
    }
    replace_apt_to_mgr(buffer, output, len * 2 + 1);
    int ret = fputs(output, stdout);
    free(buffer);
    free(output);
    return ret;
}

int putchar(int c) {
    char buf[2] = {(char)c, '\0'};
    if (c == 'a') {
        return printf("%s", buf);
    }
    return fputc(c, stdout);
}

int puts(const char *str) {
    char *output = malloc(strlen(str) * 2 + 1);
    if (!output) return -1;
    replace_apt_to_mgr(str, output, strlen(str) * 2 + 1);
    int ret = fputs(output, stdout);
    if (ret >= 0) {
        ret = fputc('\n', stdout);
    }
    free(output);
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    typedef ssize_t (*sys_write_t)(int, const void *, size_t);
    sys_write_t real_write = (sys_write_t)dlsym(RTLD_NEXT, "write");
    if (fd == 1 || fd == 2) {
        char *input = malloc(count + 1);
        if (!input) return -1;
        memcpy(input, buf, count);
        input[count] = '\0';
        char *output = malloc(count * 2 + 1);
        if (!output) {
            free(input);
            return -1;
        }
        replace_apt_to_mgr(input, output, count * 2 + 1);
        size_t out_len = strlen(output);
        ssize_t ret = real_write(fd, output, out_len);
        free(input);
        free(output);
        return ret;
    }
    return real_write(fd, buf, count);
}
