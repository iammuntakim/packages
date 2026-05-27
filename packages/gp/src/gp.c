#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

void get_val(const char *data, const char *key, char *val) {
    val[0] = '\0';
    char *p = strstr(data, key);
    if (!p) return;
    p = strchr(p, '=');
    if (!p) return;
    p++;
    int i = 0;
    while (*p && *p != '\n' && *p != '\r') {
        val[i++] = *p++;
    }
    val[i] = '\0';
}

int main(int argc, char *argv[]) {
    char config_dir[512];
    char config_file[512];
    char *prefix = getenv("PREFIX");
    if (!prefix) return 1;
    
    snprintf(config_dir, sizeof(config_dir), "%s/share", prefix);
    snprintf(config_file, sizeof(config_file), "%s/share/.push", prefix);
    
    struct stat st = {0};
    if (stat(config_dir, &st) == -1) {
        mkdir(config_dir, 0755);
    }
    
    char raw_decoded[4096] = {0};
    
    FILE *fp = fopen(config_file, "r");
    if (fp) {
        size_t bytes = fread(raw_decoded, 1, sizeof(raw_decoded) - 1, fp);
        raw_decoded[bytes] = '\0';
        fclose(fp);
    }
    
    char latest_repo[256] = {0};
    char latest_branch[256] = {0};
    char force_push[16] = "false";
    char pull_before[16] = "false";
    char generate_log[16] = "false";
    char dry_run[16] = "false";
    char add_all[16] = "true";
    char auto_tag[16] = "false";
    char skip_ci[16] = "false";
    char amend[16] = "false";
    char squash_val[16] = "0";
    char rebase[16] = "false";
    char stash_work[16] = "false";
    char tag_name[256] = {0};
    char should_push[16] = "false";
    
    get_val(raw_decoded, "REPO", latest_repo);
    get_val(raw_decoded, "BRANCH", latest_branch);
    get_val(raw_decoded, "FORCE_PUSH", force_push);
    get_val(raw_decoded, "PULL_BEFORE", pull_before);
    get_val(raw_decoded, "GENERATE_LOG", generate_log);
    get_val(raw_decoded, "DRY_RUN", dry_run);
    get_val(raw_decoded, "ADD_ALL", add_all);
    get_val(raw_decoded, "AUTO_TAG", auto_tag);
    get_val(raw_decoded, "SKIP_CI", skip_ci);
    get_val(raw_decoded, "AMEND", amend);
    get_val(raw_decoded, "SQUASH_VAL", squash_val);
    get_val(raw_decoded, "REBASE", rebase);
    get_val(raw_decoded, "STASH_WORK", stash_work);
    get_val(raw_decoded, "TAG_NAME", tag_name);
    get_val(raw_decoded, "SHOULD_PUSH", should_push);
    
    char selected_repo[256] = {0};
    char selected_branch[256] = {0};
    char commit_msg[512] = {0};
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(commit_msg, sizeof(commit_msg), "build %d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    
    int arg_idx = 1;
    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "-p") == 0 || strcmp(argv[arg_idx], "--push") == 0) {
            strcpy(should_push, "true");
        } else if (strcmp(argv[arg_idx], "+p") == 0) {
            strcpy(should_push, "false");
        } else if (strcmp(argv[arg_idx], "-r") == 0 && arg_idx + 1 < argc) {
            strcpy(selected_repo, argv[arg_idx + 1]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-b") == 0 && arg_idx + 1 < argc) {
            strcpy(selected_branch, argv[arg_idx + 1]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-m") == 0 && arg_idx + 1 < argc) {
            strcpy(commit_msg, argv[arg_idx + 1]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "-f") == 0) {
            strcpy(force_push, "true");
        } else if (strcmp(argv[arg_idx], "+f") == 0) {
            strcpy(force_push, "false");
        } else if (strcmp(argv[arg_idx], "--pull") == 0) {
            strcpy(pull_before, "true");
        } else if (strcmp(argv[arg_idx], "+pull") == 0) {
            strcpy(pull_before, "false");
        } else if (strcmp(argv[arg_idx], "-l") == 0) {
            strcpy(generate_log, "true");
        } else if (strcmp(argv[arg_idx], "+l") == 0) {
            strcpy(generate_log, "false");
        } else if (strcmp(argv[arg_idx], "-d") == 0) {
            strcpy(dry_run, "true");
        } else if (strcmp(argv[arg_idx], "+d") == 0) {
            strcpy(dry_run, "false");
        } else if (strcmp(argv[arg_idx], "-s") == 0) {
            strcpy(add_all, "false");
        } else if (strcmp(argv[arg_idx], "+s") == 0) {
            strcpy(add_all, "true");
        } else if (strcmp(argv[arg_idx], "-t") == 0 && arg_idx + 1 < argc) {
            strcpy(auto_tag, "true");
            strcpy(tag_name, argv[arg_idx + 1]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "+t") == 0) {
            strcpy(auto_tag, "false");
            strcpy(tag_name, "");
        } else if (strcmp(argv[arg_idx], "--skip-ci") == 0) {
            strcpy(skip_ci, "true");
        } else if (strcmp(argv[arg_idx], "+skip-ci") == 0) {
            strcpy(skip_ci, "false");
        } else if (strcmp(argv[arg_idx], "--amend") == 0) {
            strcpy(amend, "true");
        } else if (strcmp(argv[arg_idx], "+amend") == 0) {
            strcpy(amend, "false");
        } else if (strcmp(argv[arg_idx], "--squash") == 0 && arg_idx + 1 < argc) {
            strcpy(squash_val, argv[arg_idx + 1]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--rebase") == 0) {
            strcpy(rebase, "true");
        } else if (strcmp(argv[arg_idx], "+rebase") == 0) {
            strcpy(rebase, "false");
        } else if (strcmp(argv[arg_idx], "--stash") == 0) {
            strcpy(stash_work, "true");
        } else if (strcmp(argv[arg_idx], "+stash") == 0) {
            strcpy(stash_work, "false");
        }
        arg_idx++;
    }
    
    if (selected_repo[0] != '\0') strcpy(latest_repo, selected_repo);
    if (selected_branch[0] != '\0') strcpy(latest_branch, selected_branch);
    if (latest_repo[0] == '\0') return 1;
    if (latest_branch[0] == '\0') strcpy(latest_branch, "main");
    
    char remote_url[512];
    snprintf(remote_url, sizeof(remote_url), "https://github.com/iammuntakim/%s.git", latest_repo);
    
    char encode_buf[4096];
    snprintf(encode_buf, sizeof(encode_buf),
             "REPO=%s\nBRANCH=%s\nREMOTE=%s\nFORCE_PUSH=%s\nPULL_BEFORE=%s\nGENERATE_LOG=%s\nDRY_RUN=%s\nADD_ALL=%s\nAUTO_TAG=%s\nSKIP_CI=%s\nAMEND=%s\nSQUASH_VAL=%s\nREBASE=%s\nSTASH_WORK=%s\nTAG_NAME=%s\nSHOULD_PUSH=%s\n",
             latest_repo, latest_branch, remote_url, force_push, pull_before, generate_log, dry_run, add_all, auto_tag, skip_ci, amend, squash_val, rebase, stash_work, tag_name, should_push);
    
    fp = fopen(config_file, "w");
    if (fp) {
        fputs(encode_buf, fp);
        fclose(fp);
    }
    
    if (strcmp(should_push, "false") == 0) return 0;
    if (strcmp(dry_run, "true") == 0) return 0;
    
    if (access(".git", F_OK) == -1) {
        system("git init");
    }
    
    if (system("git remote | grep -q \"^origin$\"") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git remote set-url origin %s", remote_url);
        system(cmd);
    } else {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git remote add origin %s", remote_url);
        system(cmd);
    }
    
    if (strcmp(stash_work, "true") == 0) system("git stash");
    
    char fetch_cmd[512];
    snprintf(fetch_cmd, sizeof(fetch_cmd), "git fetch origin %s 2>/dev/null", latest_branch);
    system(fetch_cmd);
    
    char check_branch_cmd[1024];
    snprintf(check_branch_cmd, sizeof(check_branch_cmd),
             "CURRENT_LOCAL_BRANCH=\$(git rev-parse --abbrev-ref HEAD 2>/dev/null); "
             "if [ \"\$CURRENT_LOCAL_BRANCH\" != \"%s\" ]; then "
             "git checkout -b %s origin/%s 2>/dev/null || "
             "git checkout -b %s 2>/dev/null || "
             "git checkout %s; "
             "fi", latest_branch, latest_branch, latest_branch, latest_branch, latest_branch);
    system(check_branch_cmd);
    
    if (strcmp(rebase, "true") == 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "git pull --rebase origin %s", latest_branch);
        system(cmd);
    } else if (strcmp(pull_before, "true") == 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "git pull origin %s", latest_branch);
        system(cmd);
    }
    
    if (strcmp(stash_work, "true") == 0) system("git stash pop");
    if (strcmp(add_all, "true") == 0) system("git add .");
    if (strcmp(skip_ci, "true") == 0) strcat(commit_msg, " [skip ci]");
    
    int s_val = atoi(squash_val);
    if (s_val > 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git reset --soft HEAD~%d && git commit -m \"%s\"", s_val, commit_msg);
        system(cmd);
    } else if (strcmp(amend, "true") == 0) {
        system("git commit --amend --no-edit");
    } else {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git diff-index --quiet HEAD -- 2>/dev/null || git commit -m \"%s\"", commit_msg);
        system(cmd);
    }
    
    if (strcmp(auto_tag, "true") == 0 && tag_name[0] != '\0') {
        char cmd1[512], cmd2[512];
        snprintf(cmd1, sizeof(cmd1), "git tag -a \"%s\" -m \"Release %s\"", tag_name, tag_name);
        snprintf(cmd2, sizeof(cmd2), "git push origin \"%s\"", tag_name);
        system(cmd1);
        system(cmd2);
    }
    
    char push_flags[32] = "";
    if (strcmp(force_push, "true") == 0) strcpy(push_flags, "--force");
    
    char final_cmd[2048];
    if (strcmp(generate_log, "true") == 0) {
        char log_dir[512];
        snprintf(log_dir, sizeof(log_dir), "%s/share/logs", prefix);
        char mkdir_cmd[512];
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", log_dir);
        system(mkdir_cmd);
        snprintf(final_cmd, sizeof(final_cmd), "git push origin %s %s 2>&1 | tee %s/%ld.log", latest_branch, push_flags, log_dir, (long)time(NULL));
    } else {
        snprintf(final_cmd, sizeof(final_cmd), "git push origin %s %s", latest_branch, push_flags);
    }
    system(final_cmd);
    
    return 0;
}
