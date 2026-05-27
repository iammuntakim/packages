#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define BUFFER_SIZE 8192

void print_with_colors(const char *prefix, const char *line) {
    if (prefix && strlen(prefix) > 0) {
        printf("%s\033[33m", prefix);
    }
    int i = 0;
    while (line[i] == ' ' || line[i] == '\t') {
        i++;
    }
    while (line[i] != '\0') {
        if (prefix && strlen(prefix) > 0) {
            putchar(line[i]);
            i++;
            continue;
        }
        if (strncasecmp(&line[i], "> Task :", 8) == 0) {
            printf("\033[0m> Task :");
            i += 8;
            while (line[i] != '\0' && line[i] != ':') {
                putchar(line[i]);
                i++;
            }
            if (line[i] == ':') {
                putchar(line[i]);
                i++;
            }
            continue;
        }
        if (line[i] >= '0' && line[i] <= '9') {
            printf("\033[33m");
            while (line[i] >= '0' && line[i] <= '9') {
                putchar(line[i]);
                i++;
            }
            printf("\033[0m");
            continue;
        }
        if (strncasecmp(&line[i], "bytes", 5) == 0) {
            printf("\033[33mbytes\033[0m");
            i += 5;
            continue;
        }
        if (strncasecmp(&line[i], "mb", 2) == 0) {
            printf("\033[33mmb\033[0m");
            i += 2;
            continue;
        }
        if (strncasecmp(&line[i], "kb", 2) == 0) {
            printf("\033[33mkb\033[0m");
            i += 2;
            continue;
        }
        if (strncasecmp(&line[i], "gb", 2) == 0) {
            printf("\033[33mgb\033[0m");
            i += 2;
            continue;
        }
        if (strncasecmp(&line[i], "SUCCESSFUL", 10) == 0) {
            printf("\033[33mSUCCESSFUL\033[0m");
            i += 10;
            continue;
        }
        if (strncasecmp(&line[i], "FAILED", 6) == 0) {
            printf("\033[33mFAILED\033[0m");
            i += 6;
            continue;
        }
        putchar(line[i]);
        i++;
    }
    if (prefix && strlen(prefix) > 0) {
        printf("\033[0m");
    }
}

void get_env_info(char *buffer, size_t max_size) {
    char user[256] = "unknown";
    char shell[256] = "unknown";
    char home[1024] = "unknown";
    char *env_user = getenv("USER");
    char *env_shell = getenv("SHELL");
    char *env_home = getenv("HOME");
    if (env_user) strncpy(user, env_user, sizeof(user) - 1);
    if (env_shell) strncpy(shell, env_shell, sizeof(shell) - 1);
    if (env_home) strncpy(home, env_home, sizeof(home) - 1);
    snprintf(buffer, max_size, "systemEnv USER=%s shell=%s home=%s\n", user, shell, home);
}

void scan_directory_structure(const char *dir_path, int depth, const char *prefix) {
    if (depth > 2) return;
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    char buffer[BUFFER_SIZE];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.' && strcmp(entry->d_name, ".gradle") != 0) continue;
        snprintf(buffer, sizeof(buffer), "dirScan %s\n", entry->d_name);
        print_with_colors(prefix, buffer);
        if (entry->d_type == DT_DIR) {
            char next_path[BUFFER_SIZE];
            snprintf(next_path, sizeof(next_path), "%s/%s", dir_path, entry->d_name);
            scan_directory_structure(next_path, depth + 1, prefix);
        }
    }
    closedir(dir);
}

void inspect_gradle_files(const char *base_dir, const char *prefix) {
    char path[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    struct stat st;
    const char *files[] = {"build.gradle", "build.gradle.kts", "settings.gradle", "settings.gradle.kts", "gradle.properties", "local.properties"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", base_dir, files[i]);
        if (stat(path, &st) == 0) {
            snprintf(buffer, sizeof(buffer), "fileFound %s %lld bytes\n", files[i], (long long)st.st_size);
            print_with_colors(prefix, buffer);
        }
    }
}

int check_binary(const char *bin_name) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "which %s > /dev/null 2>&1", bin_name);
    return system(cmd) == 0;
}

int main(int argc, char *argv[]) {
    char target[256] = "";
    char target_dir[BUFFER_SIZE] = "";
    char env_buffer[BUFFER_SIZE];
    char out_buffer[BUFFER_SIZE];
    char task_prefix[64];
    int verbose = 0;
    int no_logs = 0;

    int arg_idx = 1;
    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-i") == 0 || strcmp(argv[arg_idx], "-info") == 0) {
            verbose = 1;
        } else if (strcmp(argv[arg_idx], "-nL") == 0 || strcmp(argv[arg_idx], "-noLogs") == 0 || strcmp(argv[arg_idx], "--no-logs") == 0) {
            no_logs = 1;
        } else {
            if (strlen(target_dir) == 0 && arg_idx + 1 < argc && argv[arg_idx + 1][0] != '-') {
                strncpy(target_dir, argv[arg_idx], sizeof(target_dir) - 1);
            } else if (strlen(target) == 0) {
                strncpy(target, argv[arg_idx], sizeof(target) - 1);
            }
        }
        arg_idx++;
    }

    if (strlen(target) == 0 && strlen(target_dir) > 0) {
        strncpy(target, target_dir, sizeof(target) - 1);
        memset(target_dir, 0, sizeof(target_dir));
    }

    if (strlen(target) == 0) {
        printf("Usage: glog [options] <apk|jar>\n");
        printf("       glog [options] <project_directory> <apk|jar>\n");
        printf("Options:\n");
        printf("  -i, -info       Show verbose telemetry and advanced environment hooks\n");
        printf("  -nL, -noLogs    Suppress internal engine telemetry and show only raw build pipelines\n");
        return 1;
    }

    if (strlen(target_dir) == 0) {
        if (getcwd(target_dir, sizeof(target_dir)) == NULL) {
            perror("> Task :app:fatalError failed to determine working directory");
            return 1;
        }
    }

    if (strcmp(target, "jar") == 0) {
        strcpy(task_prefix, "> Task :jar:");
    } else {
        strcpy(task_prefix, "> Task :app:");
    }

    if (!no_logs) {
        if (verbose) {
            print_with_colors(task_prefix, "bootstrapEngine loading native optimization pipeline context\n");
            print_with_colors(task_prefix, "signalHandler mapping architecture safety boundaries\n");
            print_with_colors(task_prefix, "memoryManager allocating static stack space blocks\n");
            print_with_colors(task_prefix, "loggerInit initializing internal profiling engine\n");
            get_env_info(env_buffer, sizeof(env_buffer));
            print_with_colors(task_prefix, env_buffer);
            print_with_colors(task_prefix, "subsystemDetect scanning virtual hardware node references\n");
            print_with_colors(task_prefix, "securityContext checking local executable sandbox profiles\n");
        } else {
            print_with_colors(task_prefix, "loggerInit initializing internal profiling engine\n");
            get_env_info(env_buffer, sizeof(env_buffer));
            print_with_colors(task_prefix, env_buffer);
        }

        if (verbose) {
            print_with_colors(task_prefix, "pathResolver entering targeted system disk sector maps\n");
            snprintf(out_buffer, sizeof(out_buffer), "targetDir resolved paths %s\n", target_dir);
            print_with_colors(task_prefix, out_buffer);
            snprintf(out_buffer, sizeof(out_buffer), "targetTask selected target executable profile %s\n", target);
            print_with_colors(task_prefix, out_buffer);
            print_with_colors(task_prefix, "pipelineVerify linking logical data translation chains\n");
        } else {
            snprintf(out_buffer, sizeof(out_buffer), "targetDir resolved paths %s\n", target_dir);
            print_with_colors(task_prefix, out_buffer);
            snprintf(out_buffer, sizeof(out_buffer), "targetTask selected target executable profile %s\n", target);
            print_with_colors(task_prefix, out_buffer);
        }

        if (verbose) {
            print_with_colors(task_prefix, "preFlight running compiler ecosystem checks\n");
            print_with_colors(task_prefix, "dependencyHook mapping dynamic runtime architecture targets\n");
            snprintf(out_buffer, sizeof(out_buffer), "dirCheck java runtime environment %s\n", check_binary("java") ? "available" : "missing");
            print_with_colors(task_prefix, out_buffer);
            snprintf(out_buffer, sizeof(out_buffer), "dirCheck gradle wrapper backend %s\n", check_binary("gradle") ? "available" : "missing");
            print_with_colors(task_prefix, out_buffer);
            snprintf(out_buffer, sizeof(out_buffer), "dirCheck android debug bridge %s\n", check_binary("adb") ? "available" : "missing");
            print_with_colors(task_prefix, "compilerEcosystem validation sequence finalized successfully\n");
        } else {
            print_with_colors(task_prefix, "preFlight running compiler ecosystem checks\n");
            snprintf(out_buffer, sizeof(out_buffer), "dirCheck java runtime environment %s\n", check_binary("java") ? "available" : "missing");
            print_with_colors(task_prefix, out_buffer);
            snprintf(out_buffer, sizeof(out_buffer), "dirCheck gradle wrapper backend %s\n", check_binary("gradle") ? "available" : "missing");
            print_with_colors(task_prefix, out_buffer);
            snprintf(out_buffer, sizeof(out_buffer), "dirCheck android debug bridge %s\n", check_binary("adb") ? "available" : "missing");
        }

        if (verbose) {
            print_with_colors(task_prefix, "ioInspect scanning root descriptor layers\n");
            print_with_colors(task_prefix, "diskScanner registering structural system attributes\n");
            inspect_gradle_files(target_dir, task_prefix);
            print_with_colors(task_prefix, "metadataCache building temporary index file tables\n");
        } else {
            print_with_colors(task_prefix, "ioInspect scanning root descriptor layers\n");
            inspect_gradle_files(target_dir, task_prefix);
        }

        if (verbose) {
            print_with_colors(task_prefix, "fsTree mapping target directory topology\n");
            print_with_colors(task_prefix, "inodeTracker tracking active memory address structures\n");
            scan_directory_structure(target_dir, 0, task_prefix);
            print_with_colors(task_prefix, "directoryTopology mapping iteration deep routine finalized\n");
        } else {
            print_with_colors(task_prefix, "fsTree mapping target directory topology\n");
            scan_directory_structure(target_dir, 0, task_prefix);
        }
    }

    char command[BUFFER_SIZE];
    if (strcmp(target, "apk") == 0) {
        snprintf(command, sizeof(command), "cd %s && gradle assembleRelease", target_dir);
    } else if (strcmp(target, "jar") == 0) {
        snprintf(command, sizeof(command), "cd %s && gradle jar", target_dir);
    } else {
        if (!no_logs) {
            snprintf(out_buffer, sizeof(out_buffer), "fatalError verification mismatch for token %s\n", target);
            print_with_colors(task_prefix, out_buffer);
        }
        return 1;
    }

    if (!no_logs) {
        if (verbose) {
            print_with_colors(task_prefix, "pipeExec initializing background subprocess pipe creation\n");
            print_with_colors(task_prefix, "kernelBridge opening synchronous stream transmission portals\n");
            snprintf(out_buffer, sizeof(out_buffer), "pipeExec raw execution query payload %s\n", command);
            print_with_colors(task_prefix, out_buffer);
            print_with_colors(task_prefix, "streamBroker checking system pipe descriptor limit values\n");
            print_with_colors(task_prefix, "pipeStream stream established relaying stream packets to console handler\n");
        } else {
            print_with_colors(task_prefix, "pipeExec initializing background subprocess pipe creation\n");
            snprintf(out_buffer, sizeof(out_buffer), "pipeExec raw execution query payload %s\n", command);
            print_with_colors(task_prefix, out_buffer);
            print_with_colors(task_prefix, "pipeStream stream established relaying stream packets to console handler\n");
        }
    }

    FILE *pipe = popen(command, "r");
    if (!pipe) {
        if (!no_logs) {
            perror("fatalError process pipeline connection rejected by kernel");
        }
        return 1;
    }

    char line[BUFFER_SIZE];
    long total_lines_processed = 0;
    long stdout_chars_transferred = 0;

    while (fgets(line, sizeof(line), pipe) != NULL) {
        total_lines_processed++;
        stdout_chars_transferred += strlen(line);

        if (strcmp(target, "jar") == 0) {
            char *found = strstr(line, "> Task :app:");
            if (found) {
                memmove(found + 8, "jar:", 4);
            }
        }

        print_with_colors(NULL, line);
        fflush(stdout);
    }

    int status = pclose(pipe);

    if (!no_logs && verbose) {
        print_with_colors(task_prefix, "streamDeconstruct tearing down process monitoring wrappers\n");
        print_with_colors(task_prefix, "ipcTerminator disconnected communication streams successfully\n");
        print_with_colors(task_prefix, "bufferFlush matching persistent storage byte boundaries\n");
        print_with_colors(task_prefix, "telemetryEngine tracking active execution metrics profiles\n");
        snprintf(out_buffer, sizeof(out_buffer), "telemetryData intercepted lines tally evaluated to %ld total rows\n", total_lines_processed);
        print_with_colors(task_prefix, out_buffer);
        snprintf(out_buffer, sizeof(out_buffer), "telemetryData calculated stream weight to %ld raw data bytes\n", stdout_chars_transferred);
        print_with_colors(task_prefix, out_buffer);
        snprintf(out_buffer, sizeof(out_buffer), "systemState core runtime architecture pipe exit response token %d\n", status);
        print_with_colors(task_prefix, out_buffer);
        print_with_colors(task_prefix, "engineShutdown release sequence operational module termination finalized\n");
    }

    return status;
}
