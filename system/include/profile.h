#ifndef UFS_PROFILE_H
#define UFS_PROFILE_H

#ifdef UFS_PROFILE

#include <time.h>
#include <stdio.h>

typedef enum {CLOSE = 0, READ, WRITE, OPEN, STAT,
LSTAT, FSTAT, DUP, DUP2, DUP3, LSEEK, MMAP,
ACCESS, UNLINK, EXECVP, EXECV, EXECVE, PIPE,
RENAME, CHMOD, REALPATH, NUM_OF_CALLS} profile_calls;

static char* profile_name[] = {"close", "read",
"write", "open", "stat", "lstat", "fstat", "dup",
"dup2", "dup3", "lseek", "mmap", "access",
"unlink", "execvp", "execv", "execve", "pipe",
"rename", "chmod", "realpath", 0};

extern long* profile_mem;

static inline void init_profile(void) {
    int i;
    for (i = 0; i < NUM_OF_CALLS; i++) {
        profile_mem[i] = 0;
    }
}

static inline void output_profile(FILE* out) {
    int i;
    long total = 0;
    for (i = 0; i < NUM_OF_CALLS; i++) {
        total += profile_mem[i];
        fprintf(out, "%s: %ld %f\n", profile_name[i], profile_mem[i], (double) profile_mem[i] / CLOCKS_PER_SEC);
    }
    fprintf(out, "Total: %ld %f\n", total, (double) total / CLOCKS_PER_SEC);
}

#define profile_begin() \
        clock_t profile_b, profile_e; \
        profile_b = clock()

#define profile_end(name) \
        profile_e = clock(); \
        __sync_fetch_and_add(&profile_mem[name], (long) (profile_e - profile_b))

#else

#define init_profile()
#define output_profile()
#define profile_begin()
#define profile_end(name)

#endif
#endif
