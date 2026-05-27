#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>

void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    syscall(SYS_write, 1, s, len);
}

void print_num(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    while (i > 0) {
        char c = buf[--i];
        syscall(SYS_write, 1, &c, 1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 127;

    char *args[1024];
    args[0] = "/usr/bin/gcc";
    args[1] = "-Os";
    args[2] = "-s";
    args[3] = "-fno-plt";
    args[4] = "-fno-stack-protector";
    args[5] = "-fomit-frame-pointer";
    args[6] = "-fno-ident";
    args[7] = "-fno-asynchronous-unwind-tables";
    args[8] = "-fdata-sections";
    args[9] = "-ffunction-sections";
    args[10] = "-Wl,--gc-sections";
    args[11] = "-Wl,-Bsymbolic";
    args[12] = "-Wl,--no-eh-frame-hdr";
    args[13] = "-Wl,--build-id=none";

    int count = 14;
    int hide_warnings = 0;
    char *out_name = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'w') {
                hide_warnings = 1;
            } else if (argv[i][1] == 'o' && i + 1 < argc) {
                out_name = argv[++i];
                args[count++] = "-o";
                args[count++] = out_name;
            } else {
                args[count++] = argv[i];
            }
        } else {
            if (!out_name) out_name = argv[i];
            args[count++] = argv[i];
        }
    }

    if (hide_warnings) args[count++] = "-w";
    args[count++] = NULL;

    time_t t = time(NULL);
    int v1 = (t % 5), v2 = (t % 10), v3 = (t % 10);

    print("Compiling ");
    if (out_name) print(out_name);
    print(" ");
    print_num(v1);
    print(".");
    print_num(v2);
    print(".");
    print_num(v3);
    print(" ...\n");

    if (fork() == 0) {
        execvp(args[0], args);
        _exit(1);
    } else {
        int status;
        syscall(SYS_wait4, -1, &status, 0, 0);
    }

    return 0;
}
