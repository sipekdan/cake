#ifndef CAKE_H
#define CAKE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <glob.h>

static int cake_argc;
static char **cake_argv;
static char **cake_envp;

#define CAKE_MAIN() \
    int cake_main(); \
    int main(int c, char **v, char **e) { cake_init(c, v, e, __FILE__); return cake_main(); } \
    int cake_main()

#define CAKE_BUILD(output, sources) cake_build(output, sources)

void cake_init(int argc, char **argv, char **envp, const char *source_file);
void cake_run(const char *cmd);
int cake_needs_rebuild(const char *binary, const char *source);
void cake_build(const char *output, const char *sources);

#endif // CAKE_H

#ifdef CAKE_IMPLEMENTATION

void cake_init(int argc, char **argv, char **envp, const char *source_file)
{
    cake_argc = argc;
    cake_argv = argv;
    cake_envp = envp;

    if (getenv("CAKE_ALREADY_RAN")) {
        return;
    }
    
    const char *bin = argv[0];
    char bin_old[PATH_MAX];
    strncpy(bin_old, bin, sizeof(bin_old) - 1);
    bin_old[sizeof(bin_old) - 1] = '\0';
    strncat(bin_old, ".old", sizeof(bin_old) - strlen(bin_old) - 1);

    printf("Binary: %s\nSource: %s\n", argv[0], source_file);

    if (cake_needs_rebuild(bin, source_file)) {
        printf("Bin: %s, bin_old: %s, source_file: %s\n", bin, bin_old, source_file);

        remove(bin_old); // remove the old backup

        // renames current bin to backup
        if (rename(bin, bin_old) != 0) {
            perror("[cake] Failed to rename old binary, trying to remove instead...");
            if (remove(bin) != 0) {
                perror("[cake] Failed to remove current binary");
                exit(1);
            }
        }

        char buff[PATH_MAX];
        snprintf(buff, sizeof(buff), "gcc -o %s %s", bin, source_file);
        cake_run(buff);

        setenv("CAKE_ALREADY_RAN", "1", 1);

        // run the new binary (use ./ prefix for Unix)
        char run_cmd[PATH_MAX];
        snprintf(run_cmd, sizeof(run_cmd), "%s", bin);
        cake_run(run_cmd);

        // remove the .old file
        remove(bin_old);

        exit(0);
    }
}

void cake_run(const char *cmd)
{
    if (system(cmd) != 0) {
        fprintf(stderr, "[cake] Command failed: %s\n", cmd);
    }
}

int cake_needs_rebuild(const char *binary, const char *source) {
    struct stat bin_stat, src_stat;

    if (stat(binary, &bin_stat) != 0) {
        // Binary missing → needs rebuild
        return 1;
    }

    if (stat(source, &src_stat) != 0) {
        printf("[cake] %s is missing\n", source);
        // Source missing → assume no rebuild needed
        return 0;
    }

    return src_stat.st_mtime > bin_stat.st_mtime;
}

void cake_build(const char *output, const char *sources) {
    const char *bin = output;  // use output as binary name
    char bin_old[PATH_MAX];
    snprintf(bin_old, sizeof(bin_old), "%s.old", bin);

    // Expand wildcards with glob for each token separated by space
    glob_t globbuf;
    memset(&globbuf, 0, sizeof(globbuf));

    // We'll need to expand multiple patterns separated by space
    // So tokenize sources string, then glob each and accumulate results
    glob_t accum = {0};
    char sources_copy[4096];
    strncpy(sources_copy, sources, sizeof(sources_copy)-1);
    sources_copy[sizeof(sources_copy)-1] = '\0';

    char *token = strtok(sources_copy, " ");
    while (token != NULL) {
        glob_t temp = {0};
        if (glob(token, GLOB_TILDE, NULL, &temp) == 0) {
            // Append temp results to accum
            for (size_t i = 0; i < temp.gl_pathc; ++i) {
                globbuf.gl_pathv = realloc(globbuf.gl_pathv, sizeof(char*) * (globbuf.gl_pathc + 1));
                globbuf.gl_pathv[globbuf.gl_pathc] = strdup(temp.gl_pathv[i]);
                globbuf.gl_pathc++;
            }
            globfree(&temp);
        } else {
            // If no matches, just add token literally
            globbuf.gl_pathv = realloc(globbuf.gl_pathv, sizeof(char*) * (globbuf.gl_pathc + 1));
            globbuf.gl_pathv[globbuf.gl_pathc] = strdup(token);
            globbuf.gl_pathc++;
        }
        token = strtok(NULL, " ");
    }

    if (globbuf.gl_pathc == 0) {
        fprintf(stderr, "[cake] No source files matched: %s\n", sources);
        exit(1);
    }

    // Check if rebuild is needed by comparing mod time of any source vs binary
    int needs_rebuild = 0;
    for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
        if (cake_needs_rebuild(bin, globbuf.gl_pathv[i])) {
            needs_rebuild = 1;
            break;
        }
    }

    if (needs_rebuild) {
        // Backup current binary
        remove(bin_old);
        if (rename(bin, bin_old) != 0) {
            if (remove(bin) != 0) {
                perror("[cake] Failed to rename or remove current binary");
                exit(1);
            }
        }

        // Build gcc command string
        char cmd[4096] = {0};
        snprintf(cmd, sizeof(cmd), "gcc -o %s", bin);
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
            strncat(cmd, globbuf.gl_pathv[i], sizeof(cmd) - strlen(cmd) - 1);
        }

        printf("[cake] Rebuilding: %s\n", cmd);
        cake_run(cmd);

        // Cleanup backup
        remove(bin_old);

        printf("[cake] Relaunching: %s\n", bin);
        cake_run(bin);
        exit(0);
    }

    // Free allocated glob strings
    for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
        free(globbuf.gl_pathv[i]);
    }
    free(globbuf.gl_pathv);
}


#endif // CAKE_IMPLEMENTATION
