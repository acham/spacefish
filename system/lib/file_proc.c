#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#define _FCNTL_H
#include <bits/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* ufs */
#include <glibccall.h>
#include <utility.h>
#include <fd.h>
#include <dlog.h>
#include <file.h>
#include <process.h>

extern int calling_shm_open;
extern uid_t uid;
extern pid_t pid;
extern fd_info *backup_fds;

pid_t fork(void)
{
    dlog(1, "[ufs: fork]\n");
    return real_fork();
}

pid_t vfork(void)
{
    dlog(1, "[ufs: vfork]\n");
    int fpid = real_fork();

#ifdef VFORK_SIGNALING
    /** parent first tells child that it was forked, so
     ** child can know to signal parent in case of exec() of exit().
     ** this allows the emulation of true vfork:
     ** child completes before parent can start.
     ** acknowledgement from child is in form of SIGUSR1.
     **/
    if (fpid) {
        int child_ind = get_new_pid_meta_ind();
        dlog(1, "[ufs: vfork] setting slot %d to pid %d\n", child_ind, fpid);
        pid_meta[child_ind].pid = fpid;
        pid_meta[child_ind].first_parent_signaled = 0;
        pid_meta[child_ind].vforked = 1;
        kill(fpid, SIGUSR2);
        dlog(1, "[ufs: vfork] parent added child pid to array and signaled "
                "chiled\n");
        pause();
    }

    /** child must pause so parent can tell it whether it was
     ** created through vfork
     **/
    else {
        dlog(1, "[ufs: vfork] child before pause\n");
        while (!this_signaled_by_parent)
            pause();
        dlog(1, "[ufs: vfork] child after pause\n");
    }
#endif

    return fpid;
}

static void exec_fd_backup(void)
{
    pid = getpid();
    uid = getuid();

    int fdtable_size = MAX_NUM_FD * sizeof(fd_info);
    if (!backup_fds) {
        get_pid_meta_ind();
        /* backup file descriptors in new shm */
        char fd_backup_shm_name[50];
        sprintf(fd_backup_shm_name, "ufs_fdbackup_%d_%d", (int)uid, (int)pid);
        calling_shm_open = 1;
        int fd_backup_fd = shm_open(
            fd_backup_shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
        calling_shm_open = 0;
        EHANDLE("shm_open", fd_backup_fd, -1);
        dlog(1, "[ufs: exec_fd_backup] shm_open successful for file: %s\n",
             fd_backup_shm_name);
        EHANDLE("ftruncate", ftruncate(fd_backup_fd, fdtable_size), -1);
        void *mapret = mmap(NULL, fdtable_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd_backup_fd, 0);
        EHANDLE("mmap", mapret, MAP_FAILED);
        backup_fds = (fd_info *)mapret;
        real_close(fd_backup_fd);
    }
    memcpy(backup_fds, fdTable, fdtable_size);
    EHANDLE("munmap", munmap(backup_fds, fdtable_size), -1);
    dlog(1, "[ufs: exec**] backed up file descriptors OK\n");
    pid_meta[pid_meta_ind].fds_backed_up = 1;
}

static inline void exec_file_in_shm(const char *path) {
    meta_bucket *mb;
    const char *fpath;
    BUILD_ABS_PATH_1(fpath, path);
    if ((mb = get_mbucket(fpath))) {
        write_to_disk(mb);
    }
    FREE_ABS_PATH_1(fpath, path);
}

/* make, gcc use execvp and execv */
int execvp(const char *file, char *const args[])
{
    dlog(1, "[ufs: execvp] new program: %s\n", file);
    exec_fd_backup();
#ifdef VFORK_SIGNALING
    maybe_signal_parent();
#endif
    exec_file_in_shm(file);
    int ret = real_execvp(file, args);
    EHANDLE("real_execvp", ret, -1);
    return ret;
}

int execv(const char *path, char *const args[])
{
    dlog(1, "[ufs: execv] new program: %s\n", path);
    exec_fd_backup();
#ifdef VFORK_SIGNALING
    maybe_signal_parent();
#endif
    exec_file_in_shm(path);
    int ret = real_execv(path, args);
    EHANDLE("real_execv", ret, -1);
    return ret;
}

int execve(const char *filename, char *const args[], char *const _envp[])
{
    dlog(1, "[ufs: execve] filename: %s\n", filename);
    dlog(10, "[ufs: execve] LIBPATH: %s\n", LIBPATH);
    exec_fd_backup();

    int i = 0;
    while (_envp[i]) {
        dlog(10, "[ufs: execve] _envp[%d]: %s\n", i, _envp[i]);
        i++;
    }

    int num_old_env = i;
    char *new_envp[num_old_env + 2];
    for (i = 0; i < num_old_env; i++) {
        new_envp[i] = _envp[i];
    }
    char lib_env_var[PATH_MAX + 10];
    sprintf(lib_env_var, "LD_PRELOAD=%s/libufs.so", LIBPATH);
    dlog(10, "[ufs: execve] lib_env_var set to: %s\n", lib_env_var);
    new_envp[num_old_env] = lib_env_var;
    new_envp[num_old_env + 1] = 0;

    /* check new env */
    i = 0;
    while (new_envp[i]) {
        dlog(10, "[ufs: execve] new_envp[%d]: %s\n", i, new_envp[i]);
        i++;
    }
#ifdef VFORK_SIGNALING
    maybe_signal_parent();
#endif
    exec_file_in_shm(filename);

    int ret = real_execve(filename, args, new_envp);
    EHANDLE("real_execve", ret, -1);
    return ret;
}

