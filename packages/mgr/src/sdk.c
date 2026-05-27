#include "sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

void format_size_mb(curl_off_t bytes, char *out, size_t max_len) {
    snprintf(out, max_len, "%.1f MB", (double)bytes / 1048576.0);
}

void format_speed_mb(double bytes_per_sec, char *out, size_t max_len) {
    snprintf(out, max_len, "%.1f MB/s", bytes_per_sec / 1048576.0);
}

void format_time_s(long seconds, char *out, size_t max_len) {
    if (seconds < 0) seconds = 0;
    snprintf(out, max_len, "%lds", seconds);
}

int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    struct progress_data *data = (struct progress_data *)clientp;
    double current_time = get_time_seconds();
    
    if (data->start_time == 0.0) {
        data->start_time = current_time;
        data->last_time = current_time;
        data->last_bytes = 0;
        data->current_speed = 0.0;
        data->last_update_time = 0.0;
    }

    double time_diff = current_time - data->last_time;
    if (time_diff >= 0.5) {
        curl_off_t bytes_diff = dlnow - data->last_bytes;
        if (bytes_diff >= 0) {
            data->current_speed = (double)bytes_diff / time_diff;
        }
        data->last_time = current_time;
        data->last_bytes = dlnow;
    }

    if (dltotal > 0 && (current_time - data->last_update_time >= 1.0)) {
        data->last_update_time = current_time;
        long percentage = (long)(((double)dlnow / (double)dltotal) * 100.0);
        if (percentage > 100) percentage = 100;
        
        char dlnow_str[32];
        char dltotal_str[32];
        char speed_str[32];
        char eta_str[32];

        format_size_mb(dlnow, dlnow_str, sizeof(dlnow_str));
        format_size_mb(dltotal, dltotal_str, sizeof(dltotal_str));

        if (data->current_speed > 0.0) {
            format_speed_mb(data->current_speed, speed_str, sizeof(speed_str));
            curl_off_t remaining_bytes = dltotal - dlnow;
            long eta_seconds = (long)((double)remaining_bytes / data->current_speed);
            format_time_s(eta_seconds, eta_str, sizeof(eta_str));
        } else {
            snprintf(speed_str, sizeof(speed_str), "0.0 MB/s");
            snprintf(eta_str, sizeof(eta_str), "0s");
        }

        printf("\033[2K\r\033[33m%ld%% [1 %s %s/%s %ld%%]                     %s %s\033[0m", 
               percentage, data->filename, dlnow_str, dltotal_str, percentage, speed_str, eta_str);
        fflush(stdout);
    }
    return 0;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

curl_off_t get_remote_file_size(const char *url) {
    CURL *curl = curl_easy_init();
    curl_off_t download_size = 0;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (curl_easy_perform(curl) == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &download_size);
        }
        curl_easy_cleanup(curl);
    }
    return (download_size > 0) ? download_size : 0;
}

int download_file(const char *url, const char *output_path, const char *display_name) {
    CURL *curl;
    CURLcode res = CURLE_FAILED_INIT;
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        return -1;
    }
    curl = curl_easy_init();
    if (curl) {
        struct progress_data pdata;
        pdata.filename = display_name;
        pdata.action = "Downloading";
        pdata.start_time = 0.0;
        pdata.last_time = 0.0;
        pdata.last_bytes = 0;
        pdata.current_speed = 0.0;
        pdata.last_update_time = 0.0;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pdata);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    fclose(fp);
    printf("\n");
    return (res == CURLE_OK) ? 0 : -1;
}

void set_executable(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    char path[1024];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            chmod(path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
        }
    }
    closedir(dir);
}

void extract_with_real_apt_logs(const char *zip_file, const char *dest_dir) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "unzip -o %s -d %s 2>&1", zip_file, dest_dir);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    char buf[4096];
    char current_folder[256] = {0};
    char last_printed_folder[256] = {0};
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        char *path_start = strstr(buf, "inflating:");
        if (!path_start) {
            path_start = strstr(buf, "creating:");
        }
        if (path_start) {
            char *token = strstr(path_start, " ");
            if (token) {
                while (*token == ' ' || *token == '\t') token++;
                char path[2048];
                size_t idx = 0;
                while (*token && *token != '\r' && *token != '\n' && idx < sizeof(path) - 1) {
                    path[idx++] = *token++;
                }
                path[idx] = '\0';
                char *clean_path = path;
                if (strncmp(clean_path, "android-sdk/", 12) == 0) {
                    clean_path += 12;
                }
                while (*clean_path == '/') {
                    clean_path++;
                }
                if (strlen(clean_path) > 0) {
                    char path_copy[2048];
                    strncpy(path_copy, clean_path, sizeof(path_copy) - 1);
                    path_copy[sizeof(path_copy) - 1] = '\0';
                    char *first_slash = strchr(path_copy, '/');
                    if (first_slash) {
                        size_t len = first_slash - path_copy;
                        if (len > 0 && len < sizeof(current_folder)) {
                            strncpy(current_folder, path_copy, len);
                            current_folder[len] = '\0';
                        }
                    } else {
                        strncpy(current_folder, path_copy, sizeof(current_folder) - 1);
                        current_folder[sizeof(current_folder) - 1] = '\0';
                    }
                    if (strlen(current_folder) > 0 && strcmp(current_folder, last_printed_folder) != 0) {
                        if (strlen(last_printed_folder) > 0) {
                            printf("Setting up %s ...\n", last_printed_folder);
                        }
                        printf("Selecting previously unselected package %s.\n", current_folder);
                        printf("Preparing to unpack ...\n");
                        printf("Unpacking android-sdk ...\n");
                        fflush(stdout);
                        strncpy(last_printed_folder, current_folder, sizeof(last_printed_folder) - 1);
                        last_printed_folder[sizeof(last_printed_folder) - 1] = '\0';
                    }
                }
            }
        }
    }
    pclose(fp);
    if (strlen(last_printed_folder) > 0) {
        printf("Setting up android-sdk ...\n");
    }
}

int handle_sdk_installation(void) {
    printf("[*] https://github.com/iammuntakim/android-sdk: ok\n");
    printf("Reading package lists... Done\n");
    printf("Building dependency tree... Done\n");
    printf("Reading state information... Done\n");

    const char *prefix = getenv("PREFIX");
    if (!prefix) prefix = "/data/data/com.termux/files/usr";
    const char *home = getenv("HOME");
    if (!home) home = "/data/data/com.termux/files/home";

    char java_path[1024];
    snprintf(java_path, sizeof(java_path), "%s/lib/jvm/java-21-openjdk", prefix);
    char android_home[1024];
    snprintf(android_home, sizeof(android_home), "%s/android-sdk", home);
    char init_file[1024];
    snprintf(init_file, sizeof(init_file), "%s/.init", android_home);
    char sdk_zip_file[1024];
    snprintf(sdk_zip_file, sizeof(sdk_zip_file), "%s/android-sdk.zip", home);
    char tmp_dir[1024];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/.android_sdk_tmp", home);
    char cmdline_bin[1024];
    snprintf(cmdline_bin, sizeof(cmdline_bin), "%s/cmdline-tools/latest/bin", android_home);
    char platform_tools[1024];
    snprintf(platform_tools, sizeof(platform_tools), "%s/platform-tools", android_home);
    char build_tools[1024];
    snprintf(build_tools, sizeof(build_tools), "%s/build-tools/35.0.0", android_home);
    char aapt2_path[1024];
    snprintf(aapt2_path, sizeof(aapt2_path), "%s/aapt2", build_tools);
    char gradle_dir[1024];
    snprintf(gradle_dir, sizeof(gradle_dir), "%s/.gradle", home);
    char gradle_file[1024];
    snprintf(gradle_file, sizeof(gradle_file), "%s/gradle.properties", gradle_dir);
    char licenses_dir[1024];
    snprintf(licenses_dir, sizeof(licenses_dir), "%s/licenses", android_home);
    char license_file[1024];
    snprintf(license_file, sizeof(license_file), "%s/android-sdk-license", licenses_dir);
    char cmd[4096];
    struct stat st = {0};

    int needs_install = 0;
    if (stat(init_file, &st) == -1) {
        needs_install = 1;
    } else {
        char check_path[1024];
        snprintf(check_path, sizeof(check_path), "%s/cmdline-tools/latest/bin/sdkmanager", android_home);
        if (stat(check_path, &st) == -1) {
            needs_install = 1;
        }
        snprintf(check_path, sizeof(check_path), "%s/build-tools/35.0.0/aapt2", android_home);
        if (stat(check_path, &st) == -1) {
            needs_install = 1;
        }
        snprintf(check_path, sizeof(check_path), "%s/platform-tools/adb", android_home);
        if (stat(check_path, &st) == -1) {
            needs_install = 1;
        }
        if (stat(license_file, &st) == -1) {
            needs_install = 1;
        }
        const char *env_jh = getenv("JAVA_HOME");
        const char *env_ah = getenv("ANDROID_HOME");
        const char *env_path = getenv("PATH");
        if (!env_jh || strcmp(env_jh, java_path) != 0 || !env_ah || strcmp(env_ah, android_home) != 0) {
            needs_install = 1;
        }
        if (!env_path || !strstr(env_path, "cmdline-tools") || !strstr(env_path, "platform-tools")) {
            needs_install = 1;
        }
    }

    if (needs_install) {
        const char *url = "https://github.com/iammuntakim/android-sdk/releases/download/android-sdk/android-sdk.zip";
        curl_off_t archive_bytes = get_remote_file_size(url);
        double archive_mb = (archive_bytes > 0) ? (double)archive_bytes / 1048576.0 : 648.0;
        double unpacked_mb = (archive_bytes > 0) ? (archive_mb * 2.84) : 1843.2;

        char archive_str[64];
        if (archive_mb >= 1024.0) {
            snprintf(archive_str, sizeof(archive_str), "%.1f GB", archive_mb / 1024.0);
        } else {
            snprintf(archive_str, sizeof(archive_str), "%.1f MB", archive_mb);
        }

        char unpacked_str[64];
        if (unpacked_mb >= 1024.0) {
            snprintf(unpacked_str, sizeof(unpacked_str), "%.1f GB", unpacked_mb / 1024.0);
        } else {
            snprintf(unpacked_str, sizeof(unpacked_str), "%.1f MB", unpacked_mb);
        }

        printf("The following NEW packages will be installed:\n");
        printf("  android-sdk build-tools-35.0.0 platform-tools cmdline-tools               0 upgraded, 4 newly installed, 0 to remove and 0 not upgraded.\n");
        printf("Need to get %s of archives.                                             After this operation, %s of additional disk space will be used.\n", archive_str, unpacked_str);
        printf("Get:1 https://github.com/iammuntakim/android-sdk stable/main android-sdk.zip [%s]\n", archive_str);

        snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", tmp_dir, tmp_dir);
        system(cmd);
        if (download_file(url, sdk_zip_file, "android-sdk") != 0) {
            return 1;
        }

        extract_with_real_apt_logs(sdk_zip_file, tmp_dir);

        DIR *dir = opendir(tmp_dir);
        if (dir) {
            struct dirent *entry;
            char extracted_dir[1024] = {0};
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] != '.') {
                    char full_check_path[2048];
                    snprintf(full_check_path, sizeof(full_check_path), "%s/%s", tmp_dir, entry->d_name);
                    struct stat est;
                    if (stat(full_check_path, &est) == 0 && S_ISDIR(est.st_mode)) {
                        snprintf(extracted_dir, sizeof(extracted_dir), "%s", full_check_path);
                        if (strcmp(entry->d_name, "android-sdk") == 0) {
                            break;
                        }
                    }
                }
            }
            closedir(dir);
            if (extracted_dir[0] != '\0') {
                snprintf(cmd, sizeof(cmd), "rm -rf %s && mv %s %s", android_home, extracted_dir, android_home);
                system(cmd);
            } else {
                snprintf(cmd, sizeof(cmd), "mkdir -p %s && mv %s/* %s/ 2>/dev/null", android_home, tmp_dir, android_home);
                system(cmd);
            }
        }
        snprintf(cmd, sizeof(cmd), "rm -rf %s %s", tmp_dir, sdk_zip_file);
        system(cmd);

        snprintf(cmd, sizeof(cmd), "mkdir -p %s", android_home);
        system(cmd);
        FILE *init_fp = fopen(init_file, "wb");
        if (init_fp) {
            fclose(init_fp);
        }
    } else {
        printf("android-sdk is already the newest version.\n");
        printf("0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n");
    }

    setenv("JAVA_HOME", java_path, 1);
    setenv("ANDROID_HOME", android_home, 1);
    setenv("ANDROID_SDK_ROOT", android_home, 1);
    char new_path[4096];
    snprintf(new_path, sizeof(new_path), "%s/bin:%s/cmdline-tools/latest/bin:%s:%s", java_path, android_home, platform_tools, getenv("PATH"));
    setenv("PATH", new_path, 1);
    set_executable(cmdline_bin);

    if (stat(license_file, &st) == -1) {
        if (system("command -v sdkmanager >/dev/null 2>&1") == 0) {
            system("yes | sdkmanager --licenses > /dev/null 2>&1");
        }
    }

    char check_platform[1024];
    snprintf(check_platform, sizeof(check_platform), "%s/platforms/android-34", android_home);
    if (stat(check_platform, &st) == -1) {
        if (system("command -v sdkmanager >/dev/null 2>&1") == 0) {
            snprintf(cmd, sizeof(cmd), "sdkmanager --sdk_root=%s \"platforms;android-34\" > /dev/null 2>&1", android_home);
            system(cmd);
        }
    }

    if (system("command -v sdkmanager >/dev/null 2>&1") == 0) {
        char check_bt[1024];
        snprintf(check_bt, sizeof(check_bt), "%s/build-tools/35.0.0", android_home);
        if (stat(check_bt, &st) == -1) {
            snprintf(cmd, sizeof(cmd), "sdkmanager --sdk_root=%s \"build-tools;35.0.0\" > /dev/null 2>&1", android_home);
            system(cmd);
        }
        char check_pt[1024];
        snprintf(check_pt, sizeof(check_pt), "%s/platform-tools", android_home);
        if (stat(check_pt, &st) == -1) {
            snprintf(cmd, sizeof(cmd), "sdkmanager --sdk_root=%s \"platform-tools\" > /dev/null 2>&1", android_home);
            system(cmd);
        }
        set_executable(platform_tools);
        set_executable(build_tools);
    }

    if (stat(aapt2_path, &st) == 0) {
        chmod(aapt2_path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", gradle_dir);
        system(cmd);
        FILE *f = fopen(gradle_file, "r");
        int found = 0;
        if (f) {
            char line[1024];
            while (fgets(line, sizeof(line), f) != NULL) {
                if (strstr(line, "android.aapt2FromMavenOverride")) {
                    found = 1;
                    break;
                }
            }
            fclose(f);
        }
        if (!found) {
            f = fopen(gradle_file, "a");
            if (f) {
                fprintf(f, "android.aapt2FromMavenOverride=%s\n", aapt2_path);
                fclose(f);
            }
        }
    }
    return 0;
}