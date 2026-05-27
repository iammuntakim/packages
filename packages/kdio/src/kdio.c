#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <unistd.h>
#endif

#define MIN_MATCH_LEN 3
#define MAX_MATCH_LEN 258
#define WINDOW_SIZE 65536

void lzss_compress(const unsigned char *src, int src_len, unsigned char *dest, int *dest_len) {
    int src_idx = 0, dest_idx = 0;
    while (src_idx < src_len) {
        int best_len = 0;
        int best_dist = 0;
        int start = (src_idx > WINDOW_SIZE) ? src_idx - WINDOW_SIZE : 0;
        for (int i = start; i < src_idx; i++) {
            int len = 0;
            while (len < MAX_MATCH_LEN && (src_idx + len) < src_len && src[i + len] == src[src_idx + len]) {
                len++;
            }
            if (len >= MIN_MATCH_LEN && len > best_len) {
                best_len = len;
                best_dist = src_idx - i;
            }
        }
        if (best_len >= MIN_MATCH_LEN) {
            dest[dest_idx++] = 1;
            dest[dest_idx++] = (best_dist >> 8) & 0xFF;
            dest[dest_idx++] = best_dist & 0xFF;
            dest[dest_idx++] = best_len & 0xFF;
            src_idx += best_len;
        } else {
            dest[dest_idx++] = 0;
            dest[dest_idx++] = src[src_idx++];
        }
    }
    *dest_len = dest_idx;
}

void lzss_decompress(const unsigned char *src, int src_len, unsigned char *dest, int *dest_len) {
    int src_idx = 0, dest_idx = 0;
    while (src_idx < src_len) {
        unsigned char flag = src[src_idx++];
        if (flag == 1) {
            int dist = (src[src_idx] << 8) | src[src_idx + 1];
            int len = src[src_idx + 2];
            src_idx += 3;
            int start = dest_idx - dist;
            for (int i = 0; i < len; i++) {
                dest[dest_idx++] = dest[start + i];
            }
        } else {
            dest[dest_idx++] = src[src_idx++];
        }
    }
    *dest_len = dest_idx;
}

void xor_buffer(char *buffer, int len) {
    for (int i = 0; i < len; i++) {
        buffer[i] = buffer[i] ^ 0x5A;
    }
}

void obfuscate_buffer(char *buffer, int len) {
    for (int i = 0; i < len; i++) {
        buffer[i] = (buffer[i] ^ 0xA5) + 0x07;
    }
}

void deobfuscate_buffer(char *buffer, int len) {
    for (int i = 0; i < len; i++) {
        buffer[i] = (buffer[i] - 0x07) ^ 0xA5;
    }
}

void optimize_content(const char *src, char *dest, int *len) {
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    *len = i;
}

void generate_random_name(char *name, int len) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < len; i++) {
        name[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    name[len] = '\0';
}

void write_string(FILE *f, const char *str) {
    int len = strlen(str);
    fwrite(&len, sizeof(int), 1, f);
    fwrite(str, 1, len, f);
}

void read_string(FILE *f, char *str) {
    int len = 0;
    if (fread(&len, sizeof(int), 1, f) != 1) return;
    fread(str, 1, len, f);
    str[len] = '\0';
}

void process_file(const char *file_path, const char *file_name, FILE *manifest) {
    FILE *src = fopen(file_path, "rb");
    if (!src) return;

    fseek(src, 0, SEEK_END);
    long size = ftell(src);
    fseek(src, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, src);
    buffer[size] = '\0';
    fclose(src);

    char *processed = malloc(size + 1);
    int proc_len = 0;
    optimize_content(buffer, processed, &proc_len);
    free(buffer);

    xor_buffer(processed, proc_len);

    char rand_name[17];
    generate_random_name(rand_name, 16);

    char out_path[512];
    sprintf(out_path, "tmp/annoation/%s", rand_name);

    FILE *dest = fopen(out_path, "wb");
    if (dest) {
        fwrite(processed, 1, proc_len, dest);
        fclose(dest);

        unsigned long long timestamp = (unsigned long long)time(NULL) * 1000 + (rand() % 1000);
        int is_raw = 0;
        
        int orig_len = strlen(file_name);
        fwrite(&orig_len, sizeof(int), 1, manifest);
        fwrite(file_name, 1, orig_len, manifest);
        
        int map_len = strlen(rand_name);
        fwrite(&map_len, sizeof(int), 1, manifest);
        fwrite(rand_name, 1, map_len, manifest);
        
        fwrite(&timestamp, sizeof(unsigned long long), 1, manifest);
        fwrite(&is_raw, sizeof(int), 1, manifest);
    }

    free(processed);
}

void create_kd_archive(const char *archive_name, const char *target_dir) {
    FILE *archive = fopen(archive_name, "wb");
    if (!archive) return;

    unsigned char so_header[32] = {
        0x7F, 0x4B, 0x44, 0x41, 0x02, 0x01, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x3E, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    fwrite(so_header, 1, 32, archive);

    char main_file[256] = "";
    char *prop_buf = NULL;
    long prop_size = 0;

    if (target_dir) {
        char prop_path[512];
        sprintf(prop_path, "%s/META-INF/KNFO.PROP", target_dir);
        FILE *prop_f = fopen(prop_path, "rb");
        if (prop_f) {
            fseek(prop_f, 0, SEEK_END);
            prop_size = ftell(prop_f);
            fseek(prop_f, 0, SEEK_SET);
            prop_buf = malloc(prop_size + 1);
            fread(prop_buf, 1, prop_size, prop_f);
            prop_buf[prop_size] = '\0';
            fclose(prop_f);

            char *main_class_ptr = strstr(prop_buf, "main=");
            if (main_class_ptr) {
                main_class_ptr += 5;
                int i = 0;
                while (main_class_ptr[i] != '\n' && main_class_ptr[i] != '\r' && main_class_ptr[i] != '\0' && i < 255) {
                    main_file[i] = main_class_ptr[i];
                    i++;
                }
                main_file[i] = '\0';
            }
            write_string(archive, main_file);
            write_string(archive, prop_buf);
            free(prop_buf);
        } else {
            write_string(archive, "");
            write_string(archive, "orientation=unspecified\nkeyboard=unspecified\n");
        }
    } else {
        write_string(archive, "");
        write_string(archive, "orientation=unspecified\nkeyboard=unspecified\n");
    }

    FILE *manifest = fopen("tmp/manifest.pb", "rb");
    if (!manifest) {
        fclose(archive);
        return;
    }
    fseek(manifest, 0, SEEK_END);
    long manifest_size = ftell(manifest);
    fseek(manifest, 0, SEEK_SET);
    char *manifest_buf = malloc(manifest_size);
    fread(manifest_buf, 1, manifest_size, manifest);
    fclose(manifest);

    obfuscate_buffer(manifest_buf, manifest_size);

    unsigned char *comp_manifest = malloc(manifest_size * 2);
    int comp_manifest_len = 0;
    lzss_compress((unsigned char *)manifest_buf, manifest_size, comp_manifest, &comp_manifest_len);

    fwrite(&manifest_size, sizeof(long), 1, archive);
    fwrite(&comp_manifest_len, sizeof(int), 1, archive);
    fwrite(comp_manifest, 1, comp_manifest_len, archive);

    free(manifest_buf);
    free(comp_manifest);

    DIR *dir = opendir("tmp/annoation");
    struct dirent *entry;
    int file_count = 0;
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') file_count++;
        }
        rewinddir(dir);
        fwrite(&file_count, sizeof(int), 1, archive);

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            char full_path[512];
            sprintf(full_path, "tmp/annoation/%s", entry->d_name);
            FILE *f = fopen(full_path, "rb");
            if (!f) continue;

            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *file_buf = malloc(file_size);
            fread(file_buf, 1, file_size, f);
            fclose(f);

            unsigned char *comp_file = malloc(file_size * 2);
            int comp_file_len = 0;
            lzss_compress((unsigned char *)file_buf, file_size, comp_file, &comp_file_len);

            write_string(archive, entry->d_name);
            fwrite(&file_size, sizeof(long), 1, archive);
            fwrite(&comp_file_len, sizeof(int), 1, archive);
            fwrite(comp_file, 1, comp_file_len, archive);

            free(file_buf);
            free(comp_file);
        }
        closedir(dir);
    }
    fclose(archive);
}

void extract_kd_archive(const char *archive_path, const char *output_dir, char *main_file_out, char *prop_out) {
    FILE *archive = fopen(archive_path, "rb");
    if (!archive) return;

    fseek(archive, 32, SEEK_SET);

    read_string(archive, main_file_out);
    read_string(archive, prop_out);

    mkdir(output_dir, 0777);

    long manifest_size = 0;
    int comp_manifest_len = 0;
    fread(&manifest_size, sizeof(long), 1, archive);
    fread(&comp_manifest_len, sizeof(int), 1, archive);

    unsigned char *comp_manifest = malloc(comp_manifest_len);
    fread(comp_manifest, 1, comp_manifest_len, archive);
    char *manifest_buf = malloc(manifest_size);
    int decomp_manifest_len = 0;
    lzss_decompress(comp_manifest, comp_manifest_len, (unsigned char *)manifest_buf, &decomp_manifest_len);
    free(comp_manifest);

    deobfuscate_buffer(manifest_buf, manifest_size);

    char manifest_dir[512];
    sprintf(manifest_dir, "%s/files", output_dir);
    mkdir(manifest_dir, 0777);

    char manifest_path[512];
    sprintf(manifest_path, "%s/manifest.pb", output_dir);
    FILE *manifest = fopen(manifest_path, "wb");
    if (manifest) {
        fwrite(manifest_buf, 1, manifest_size, manifest);
        fclose(manifest);
    }
    free(manifest_buf);

    int file_count = 0;
    fread(&file_count, sizeof(int), 1, archive);

    for (int i = 0; i < file_count; i++) {
        char file_name[256];
        long file_size = 0;
        int comp_file_len = 0;

        read_string(archive, file_name);
        fread(&file_size, sizeof(long), 1, archive);
        fread(&comp_file_len, sizeof(int), 1, archive);

        unsigned char *comp_file = malloc(comp_file_len);
        fread(comp_file, 1, comp_file_len, archive);
        char *file_buf = malloc(file_size);
        int decomp_file_len = 0;
        lzss_decompress(comp_file, comp_file_len, (unsigned char *)file_buf, &decomp_file_len);
        free(comp_file);

        char out_path[512];
        sprintf(out_path, "%s/files/%s", output_dir, file_name);
        FILE *dest = fopen(out_path, "wb");
        if (dest) {
            fwrite(file_buf, 1, file_size, dest);
            fclose(dest);
        }
        free(file_buf);
    }
    fclose(archive);
}

void extract_and_restore_raw(const char *archive_path, const char *output_dir) {
    char main_file[256];
    char prop_content[4096];
    char temp_dir[512];
    sprintf(temp_dir, "%s_compiled_tmp", output_dir);
    
    extract_kd_archive(archive_path, temp_dir, main_file, prop_content);
    
    char manifest_path[512];
    sprintf(manifest_path, "%s/manifest.pb", temp_dir);
    FILE *manifest = fopen(manifest_path, "rb");
    if (!manifest) return;

    mkdir(output_dir, 0777);
    
    char inf_dir[512];
    sprintf(inf_dir, "%s/META-INF", output_dir);
    mkdir(inf_dir, 0777);
    char prop_out_path[512];
    sprintf(prop_out_path, "%s/KNFO.PROP", inf_dir);
    FILE *prop_dest = fopen(prop_out_path, "wb");
    if (prop_dest) {
        fwrite(prop_content, 1, strlen(prop_content), prop_dest);
        fclose(prop_dest);
    }

    fseek(manifest, 0, SEEK_END);
    long manifest_len = ftell(manifest);
    fseek(manifest, 0, SEEK_SET);

    while (ftell(manifest) < manifest_len) {
        int orig_len = 0, map_len = 0, is_raw = 0;
        char original[256];
        char mapped[256];
        unsigned long long timestamp = 0;

        if (fread(&orig_len, sizeof(int), 1, manifest) != 1) break;
        fread(original, 1, orig_len, manifest);
        original[orig_len] = '\0';

        if (fread(&map_len, sizeof(int), 1, manifest) != 1) break;
        fread(mapped, 1, map_len, manifest);
        mapped[map_len] = '\0';

        if (fread(&timestamp, sizeof(unsigned long long), 1, manifest) != 1) break;
        if (fread(&is_raw, sizeof(int), 1, manifest) != 1) break;

        char in_path[512];
        char out_path[512];
        sprintf(in_path, "%s/files/%s", temp_dir, mapped);
        sprintf(out_path, "%s/%s", output_dir, original);

        FILE *src = fopen(in_path, "rb");
        if (!src) continue;

        fseek(src, 0, SEEK_END);
        long size = ftell(src);
        fseek(src, 0, SEEK_SET);

        char *buffer = malloc(size);
        if (buffer) {
            fread(buffer, 1, size, src);
            fclose(src);

            xor_buffer(buffer, size);

            FILE *dest = fopen(out_path, "wb");
            if (dest) {
                fwrite(buffer, 1, size, dest);
                fclose(dest);
            }
            free(buffer);
        } else {
            fclose(src);
        }
    }
    fclose(manifest);
    
    DIR *dir = opendir(temp_dir);
    struct dirent *entry;
    if (dir) {
        char files_sub_dir[512];
        sprintf(files_sub_dir, "%s/files", temp_dir);
        DIR *subdir = opendir(files_sub_dir);
        if (subdir) {
            while ((entry = readdir(subdir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                char rem_path[512];
                sprintf(rem_path, "%s/%s", files_sub_dir, entry->d_name);
                remove(rem_path);
            }
            closedir(subdir);
            rmdir(files_sub_dir);
        }
        rewinddir(dir);
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            char rem_path[512];
            sprintf(rem_path, "%s/%s", temp_dir, entry->d_name);
            remove(rem_path);
        }
        closedir(dir);
    }
    rmdir(temp_dir);
}

void clean_dir(const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char full[512];
        sprintf(full, "%s/%s", path, entry->d_name);
        remove(full);
    }
    closedir(dir);
    rmdir(path);
}

void inject_file_to_archive(const char *archive_path, const char *inject_src_path, const char *internal_name) {
    char main_file[256];
    char prop_content[4096];
    
    mkdir("tmp", 0777);
    mkdir("tmp/annoation", 0777);

    extract_kd_archive(archive_path, "tmp_extracted", main_file, prop_content);

    FILE *old_manifest = fopen("tmp_extracted/manifest.pb", "rb");
    FILE *new_manifest = fopen("tmp/manifest.pb", "wb");

    if (!new_manifest) {
        clean_dir("tmp_extracted/files");
        clean_dir("tmp_extracted");
        return;
    }

    char target_rand_name[17] = "";
    int injection_done = 0;

    if (old_manifest) {
    fseek(old_manifest, 0, SEEK_END);
    long manifest_len = ftell(old_manifest);
    fseek(old_manifest, 0, SEEK_SET);

    while (ftell(old_manifest) < manifest_len) {
        int orig_len = 0, map_len = 0, is_raw = 0;
        char original[256];
        char mapped[256];
        unsigned long long timestamp = 0;

        // Read original path length
        if (fread(&orig_len, sizeof(int), 1, old_manifest) != 1) break;
        // Bound check to prevent buffer overflow (leaving room for '\0')
        if (orig_len < 0 || orig_len >= sizeof(original)) break; 
        
        // Read original string data and apply null terminator using 'orig_len'
        if (fread(original, 1, orig_len, old_manifest) != orig_len) break;
        original[orig_len] = '\0'; // <-- Fixed typo here

        // Read mapped path length
        if (fread(&map_len, sizeof(int), 1, old_manifest) != 1) break;
        if (map_len < 0 || map_len >= sizeof(mapped)) break;

        // Read mapped string data
        if (fread(mapped, 1, map_len, old_manifest) != map_len) break;
        mapped[map_len] = '\0';

        // Read remaining metadata block
        if (fread(&timestamp, sizeof(unsigned long long), 1, old_manifest) != 1) break;
        if (fread(&is_raw, sizeof(int), 1, old_manifest) != 1) break;

        if (strcmp(original, internal_name) == 0) {
            strcpy(target_rand_name, mapped);
            injection_done = 1;
        }

        // Write entry out to the new manifest file
        fwrite(&orig_len, sizeof(int), 1, new_manifest);
        fwrite(original, 1, orig_len, new_manifest);
        fwrite(&map_len, sizeof(int), 1, new_manifest);
        fwrite(mapped, 1, map_len, new_manifest);
        fwrite(&timestamp, sizeof(unsigned long long), 1, new_manifest);
        fwrite(&is_raw, sizeof(int), 1, new_manifest);
    }
    fclose(old_manifest);
}


    if (!injection_done) {
        generate_random_name(target_rand_name, 16);
        unsigned long long timestamp = (unsigned long long)time(NULL) * 1000;
        int is_raw = 0;

        int orig_len = strlen(internal_name);
        fwrite(&orig_len, sizeof(int), 1, new_manifest);
        fwrite(internal_name, 1, orig_len, new_manifest);

        int map_len = strlen(target_rand_name);
        fwrite(&map_len, sizeof(int), 1, new_manifest);
        fwrite(target_rand_name, 1, map_len, new_manifest);

        fwrite(&timestamp, sizeof(unsigned long long), 1, new_manifest);
        fwrite(&is_raw, sizeof(int), 1, new_manifest);
    }
    fclose(new_manifest);

    DIR *dir = opendir("tmp_extracted/files");
    struct dirent *entry;
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, target_rand_name) == 0) continue;

            char src_path[512], dest_path[512];
            sprintf(src_path, "tmp_extracted/files/%s", entry->d_name);
            sprintf(dest_path, "tmp/annoation/%s", entry->d_name);

            FILE *s = fopen(src_path, "rb");
            FILE *d = fopen(dest_path, "wb");
            if (s && d) {
                char b[4096];
                size_t r;
                while ((r = fread(b, 1, sizeof(b), s)) > 0) fwrite(b, 1, r, d);
            }
            if (s) fclose(s);
            if (d) fclose(d);
        }
        closedir(dir);
    }

    FILE *inj_src = fopen(inject_src_path, "rb");
    if (inj_src) {
        fseek(inj_src, 0, SEEK_END);
        long size = ftell(inj_src);
        fseek(inj_src, 0, SEEK_SET);

        char *buffer = malloc(size + 1);
        fread(buffer, 1, size, inj_src);
        buffer[size] = '\0';
        fclose(inj_src);

        char *processed = malloc(size + 1);
        int proc_len = 0;
        optimize_content(buffer, processed, &proc_len);
        free(buffer);

        xor_buffer(processed, proc_len);

        char out_path[512];
        sprintf(out_path, "tmp/annoation/%s", target_rand_name);
        FILE *dest = fopen(out_path, "wb");
        if (dest) {
            fwrite(processed, 1, proc_len, dest);
            fclose(dest);
        }
        free(processed);
    }

    create_kd_archive(archive_path, NULL);

    clean_dir("tmp/annoation");
    remove("tmp/manifest.pb");
    rmdir("tmp");

    clean_dir("tmp_extracted/files");
    remove("tmp_extracted/manifest.pb");
    rmdir("tmp_extracted");
}

void get_main_class_from_archive(const char *archive_path, char *out_buffer) {
    FILE *archive = fopen(archive_path, "rb");
    if (!archive) {
        out_buffer[0] = '\0';
        return;
    }

    fseek(archive, 32, SEEK_SET);

    char main_file[256];
    read_string(archive, main_file);
    fclose(archive);

    if (strlen(main_file) > 0) {
        sprintf(out_buffer, "%s", main_file);
    } else {
        out_buffer[0] = '\0';
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return 1;
    }

    if (strcmp(argv[1], "-m") == 0) {
        if (argc < 3) return 1;
        char main_class[256];
        get_main_class_from_archive(argv[2], main_class);
        if (strlen(main_class) > 0) {
            sprintf(argv[0], "%s", main_class);
        }
        return 0;
    }

    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 4) return 1;
        extract_and_restore_raw(argv[2], argv[3]);
        return 0;
    }

    if (strcmp(argv[1], "-f") == 0) {
        if (argc < 4) return 1;
        char main_file[256];
        char prop_content[4096];
        extract_kd_archive(argv[2], argv[3], main_file, prop_content);
        return 0;
    }

    if (strcmp(argv[1], "-x") == 0) {
        if (argc < 5) {
            return 1;
        }
        srand((unsigned int)time(NULL));
        inject_file_to_archive(argv[2], argv[3], argv[4]);
        return 0;
    }

    int is_so_mode = 0;
    char *target_dir = NULL;
    char archive_name[512];

    if (strcmp(argv[1], "-l") == 0) {
        if (argc < 3) return 1;
        is_so_mode = 1;
        target_dir = argv[2];
        
        char *dir_name = strrchr(target_dir, '/');
        if (!dir_name) dir_name = strrchr(target_dir, '\\');
        if (!dir_name) dir_name = target_dir;
        else dir_name++;
        sprintf(archive_name, "lib%s.so", dir_name);
    } else {
        target_dir = argv[1];
        
        char *dir_name = strrchr(target_dir, '/');
        if (!dir_name) dir_name = strrchr(target_dir, '\\');
        if (!dir_name) dir_name = target_dir;
        else dir_name++;
        sprintf(archive_name, "%s.kd", dir_name);
    }

    srand((unsigned int)time(NULL));
    
    mkdir("tmp", 0777);
    mkdir("tmp/annoation", 0777);

    FILE *manifest = fopen("tmp/manifest.pb", "wb");
    if (!manifest) return 1;

    DIR *dir = opendir(target_dir);
    struct dirent *entry;

    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, "META-INF") == 0) continue;
            
            char full_path[512];
            sprintf(full_path, "%s/%s", target_dir, entry->d_name);
            
            struct stat statbuf;
            if (stat(full_path, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
                process_file(full_path, entry->d_name, manifest);
            }
        }
        closedir(dir);
    } else {
        fclose(manifest);
        remove("tmp/manifest.pb");
        rmdir("tmp/annoation");
        rmdir("tmp");
        return 1;
    }

    fclose(manifest);

    create_kd_archive(archive_name, target_dir);

    clean_dir("tmp/annoation");
    remove("tmp/manifest.pb");
    rmdir("tmp");

    return 0;
}