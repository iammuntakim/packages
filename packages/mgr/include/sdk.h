#ifndef SDK_H
#define SDK_H

#include <curl/curl.h>

struct progress_data {
    const char *filename;
    const char *action;
    double start_time;
    double last_time;
    double last_update_time;
    curl_off_t last_bytes;
    double current_speed;
};

double get_time_seconds(void);
void format_size_mb(curl_off_t bytes, char *out, size_t max_len);
void format_speed_mb(double bytes_per_sec, char *out, size_t max_len);
void format_time_s(long seconds, char *out, size_t max_len);
int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream);
curl_off_t get_remote_file_size(const char *url);
int download_file(const char *url, const char *output_path, const char *display_name);
void set_executable(const char *dir_path);
void extract_with_real_apt_logs(const char *zip_file, const char *dest_dir);
int handle_sdk_installation(void);

#endif
