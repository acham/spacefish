#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#include <dlog.h>

extern int __xstat(int, const char *, struct stat *);
extern int __fxstat(int, int, struct stat *);
extern int open(const char *, int, ...);
extern int creat(const char *, mode_t);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern int close(int);
extern off_t lseek(int, off_t, int);
extern int dup(int);
extern int dup2(int, int);
extern int dup3(int, int, int);
extern int fcntl(int, int, ...);
extern int access(const char *, int);
extern pid_t fork(void);
extern pid_t vfork(void);
extern int execvp(const char *file, char *const args[]);
extern int execve(const char *fname, char *const args[], char *const _envp[]);
extern int execv(const char *path, char *const argv[]);
extern int chmod(const char *pathname, mode_t mode);
extern int fchmod(int fd, mode_t mode);
extern int __chmod(const char *pathname, mode_t mode);

extern FILE *fopen(const char *, const char *);
extern FILE *fopen64(const char *, const char *);
extern FILE *fdopen(int, const char *);
extern int fclose(FILE *);
extern size_t fread(void *, size_t, size_t, FILE *);
extern int fgetc(FILE *);
extern int _IO_getc(FILE *);
extern char *fgets(char *, int, FILE *);
extern int ungetc(int, FILE *);
extern size_t fwrite(const void *, size_t, size_t, FILE *);
extern int fputc(int, FILE *);
// extern int fputc_unlocked(int, FILE *);
extern int putc(int c, FILE *);
extern int _IO_putc(int c, FILE *);
//#undef _IO_putc_unlocked
// extern int _IO_putc_unlocked(int, FILE *);
extern int fputs(const char *, FILE *);
extern int fseek(FILE *, long int, int);
extern int fseeko64(FILE *, off64_t, int);
extern int fflush(FILE *);
extern int ferror(FILE *);
extern int fileno(FILE *);
extern void *mmap(void *, size_t, int, int, int, off_t);
extern int munmap(void *, size_t);
extern int vfprintf(FILE *, const char *, va_list);
extern int __vfprintf_chk(FILE *, int, const char *, va_list);
extern int __fprintf_chk(FILE *, int, const char *, ...);
extern int fprintf(FILE *, const char *, ...);
extern int printf(const char *, ...);
extern int __overflow(FILE *, int);
extern size_t fwrite_unlocked(const void *, size_t, size_t, FILE *);
extern int feof(FILE *);
extern int mkstemps(char *, int);
extern long ftell(FILE *);
extern off_t ftello(FILE *);

/*-----------------------------------------------*/
#include <config.h>
#include <glibccall.h>
#include <fd.h>
#include <file.h>
#include <process.h>
#include <meta_shm.h>
#include <data_shm.h>

#include <time.h>
#include <sys/mman.h>
#define _FCNTL_H
#include <bits/fcntl.h>

/** global vars **/

/* pointer to struct
 * holding system-wide global vars
 */
sys_glob *sys_glob_p = NULL;

/* pointer to the hash table
 */
hashinfo *hash_table = NULL;

/* pointer to first metadata bucket in
 * global array of mbuckets
 */
meta_bucket *mb_arr = NULL;

/* array of pointers to data bucket arrays
 * requires multiple pointers because we expand memory
 * by adding creating new arrays of data buckets
 * with fixed "step" size
 */
data_bucket **data_steps;

/* pointer to the array of structs
 * containing info re: process backup info + vfork signaling
 */
pid_info *pid_meta = NULL;
fd_info *backup_fds = NULL;
fd_info fdTable[MAX_NUM_FD];
core_st *core_sts = NULL;
int pid_meta_ind = 0;
int pid_meta_ind_set = 0;
int this_vforked = 0;
int this_signaled_by_parent = 0;
mode_t glob_umask;
int ufs_init_called = 0;
int calling_shm_open = 0;
int calling_shm_unlink = 0;
uid_t uid = 0;
pid_t pid = 0;
int alloc_method_ind;

void __attribute__((constructor)) ufs_init(void)
{
    if (ufs_init_called) {
        return;
    }
    ufs_init_called = 1;
    init_fdTable();
    dlog(1, "[ufs: ufs_init]\n");
    /* set default umask */
    // glob_umask = S_IWGRP | S_IWOTH;
    glob_umask = 02;
    /* some more init */
    pid_meta_ind = 0;
    pid_meta_ind_set = 0;
    uid = getuid();
    pid = getpid();
    /* below is needed by mkstemp function family
     * and metadata bucket allocation
     */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int seed = (tv.tv_usec*314) % pid;
    srand(seed);
    dlog(0, "[ufs: init] pid: %d, seed: %d\n", pid, seed);
    /* register exit intercept function */
    atexit(exit_intercept);
#ifdef VFORK_SIGNALING
    init_vfork_signaling();
#endif
    /* init data_steps array */
    int k;
    data_steps = malloc(sizeof(data_bucket *) * MAX_DATA_BUCKETS_STEPS);
    for (k = 0; k < MAX_DATA_BUCKETS_STEPS; k++) {
        data_steps[k] = NULL;
    }
    /* open and map main shm */
    dlog(1, "[ufs: ufs_init] start loading shared memory\n");
    open_map_global_memm();
    open_map_global_memd();
    open_map_pid_meta_mem();
    dlog(1, "[ufs: ufs_init] finish loading shared memory\n");
    pid_meta_ind = get_pid_meta_ind();
    /* store allocation method from shared memory into global variable 
     * for convenience
     */
    alloc_method_ind = sys_glob_p->alloc_method;
    /* check if this is an exec'ed process */
    if (pid_meta[pid_meta_ind].fds_backed_up) {
        dlog(5, "[ufs: init] found fd backup table (post-exec)"
                "at position %d\n",
             pid_meta_ind);
        restore_fd_backup();
        close_cloexec();
        dlog(10, "[ufs: ufs_init] file descriptors recovered after exec\n");
        pid_meta[pid_meta_ind].fds_backed_up = 0;
    }
    dlog(1, "[ufs: ufs_init] returning\n");
}

#define internal_shm_unlink(path) \
        calling_shm_unlink = 1; \
        shm_unlink(path); \
        calling_shm_unlink = 0

void __attribute__((destructor)) ufs_fin(void)
{
    char fd_backup_shm_name[50];
    sprintf(fd_backup_shm_name, "ufs_fdbackup_%d_%d", (int)uid, (int)pid);
    internal_shm_unlink(fd_backup_shm_name);
}

