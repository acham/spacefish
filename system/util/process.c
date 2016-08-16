#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* OS */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sched.h>

/* UFS */
#include <process.h>
#include <config.h>
#include <glibccall.h>
#include <dlog.h>
#include <utility.h>
#include <fd.h>
/* DWAL fxmark needed */
#include <data_shm.h>

extern uid_t uid;
extern pid_t pid;
extern int calling_shm_open;

/** open and map shared memory
 ** containing fd_back and vfork info
 **/
int open_map_pid_meta_mem(void)
{
    int pid_meta_sz = MAX_EXEC_HANGING * sizeof(pid_info);
    char pid_meta_shm_name[50];
    sprintf(pid_meta_shm_name, "ufs_pid_meta_%d", (int)uid);
    int pid_meta_fd;
    calling_shm_open = 1;
    pid_meta_fd = shm_open(pid_meta_shm_name, O_RDWR, 0);
    calling_shm_open = 0;
    EHANDLE("shm_open", pid_meta_fd, -1);
    pid_meta = mmap(NULL, pid_meta_sz, PROT_READ | PROT_WRITE, MAP_SHARED,
                    pid_meta_fd, 0);
    real_close(pid_meta_fd);
    dlog(5, "[ufs: ufs_init] pid_meta shared memory mapped\n");
    return 0;
}

/* init handlers and vars related to
 * vfork signaling
 */
int init_vfork_signaling(void)
{
    this_vforked = 0;
    this_signaled_by_parent = 0;
    /* install signal handler used for waking parent after vfork */
    struct sigaction sa;
    sa.sa_handler = wake1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    /* install signal handler used for telling child its properties ok */
    struct sigaction sa2;
    sa2.sa_handler = wake2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, NULL);
    dlog(4, "[ufs: ufs_init] signal handlers installed\n");
    return 0;
}

/** restore fd's from shared memory backup
 ** Keep fd_backup memory mapped and
 ** in shared memory for easy access
 ** by downstream exec() calls
 **/
int restore_fd_backup(void)
{
    char fd_backup_shm_name[50];
    sprintf(fd_backup_shm_name, "ufs_fdbackup_%d_%d", (int)uid, (int)pid);
    calling_shm_open = 1;
    int fd_backup_fd = shm_open(fd_backup_shm_name, O_RDWR, 0);
    calling_shm_open = 0;
    EHANDLE("shm_open", fd_backup_fd, -1);
    int fdtable_size = MAX_NUM_FD * sizeof(fd_info);
    void *mapret =
        mmap(NULL, fdtable_size, PROT_READ, MAP_SHARED, fd_backup_fd, 0);
    EHANDLE("mmap", mapret, MAP_FAILED);
    close(fd_backup_fd);
    fd_info *backup_fds = (fd_info *)mapret;
    memcpy(fdTable, backup_fds, fdtable_size);
    dlog(1, "[ufs: ufs_init] memcpy of fdtable ok\n");
    return 0;
}

/** inline function **/
void close_cloexec(void)
{
    int i;
    for (i = 0; i < MAX_NUM_FD; i++) {
        if (!FDDES(i).empty && (FDDES(i).flags & FD_CLOEXEC)) {
            dlog(1, "[ufs: close_cloexec] (post_exec) closed close-on-exec fd %d\n", i);
            close(i);
        }
    }
}

int get_pid_meta_ind(void)
{
    pid_t pid = getpid();
    if (pid_meta_ind_set) {
        /* maybe left over from parent context */
        if (pid_meta[pid_meta_ind].pid == pid) {
            return pid_meta_ind;
        }
    }
    int j;
    for (j = 0; j < (MAX_EXEC_HANGING); j++) {
        /* no need to lock: only seeing if my pid in table */
        if (pid_meta[j].pid == pid) {
            pid_meta_ind = j;
            pid_meta_ind_set = 1;
            dlog(1, "[ufs: get_pid_meta_ind] found pid %d at position %d\n",
                 pid, pid_meta_ind);
            return j;
        }
    }
    /* pid not in array yet */
    int k = get_new_pid_meta_ind();
    pid_meta[k].pid = pid;
    dlog(1, "[ufs: get_pid_meta_ind] added pid %d to position %d\n", pid,
         pid_meta_ind);
    return k;
}

int get_new_pid_meta_ind(void)
{
    int i;
    for (i = 0; i < (MAX_EXEC_HANGING); i++) {
        if (pid_meta[i].pid == 0) {
            int trylock_ret = trylock_pid_info(&pid_meta[i]);
            if (trylock_ret == -1) { /* another process got here first */
                continue;
            }
            pid_meta_ind = i;
            pid_meta_ind_set = 1;
            unlock_pid_info(&pid_meta[i]);
            return i;
        }
    }
    UEHANDLE("pid_meta array full!\n");
    /* not reached */
    return -1;
}

int maybe_signal_parent(void)
{
    pid_t ppid = getppid();
    pid_meta_ind = get_pid_meta_ind();
    dlog(5, "[ufs: ufs_init] get_pid_meta_ind() returned OK\n");
    this_vforked = pid_meta[pid_meta_ind].vforked;
    dlog(1, "[ufs: maybe_signal_parent]: \n\tthis_vforked: %d\n"
            "\tpid_meta_ind: %d\n"
            "\tfirst_parent_signaled: %d\n",
         this_vforked, pid_meta_ind,
         pid_meta[pid_meta_ind].first_parent_signaled);
    if (this_vforked && !(pid_meta[pid_meta_ind].first_parent_signaled)) {
        kill(ppid, SIGUSR1);
        pid_meta[pid_meta_ind].first_parent_signaled = 1;
        dlog(1, "[ufs: maybe_signal_parent] signal sent to parent\n");
    }
    return 0;
}

int remove_pid_slot()
{
    get_pid_meta_ind();
    pid_info *pp = &pid_meta[pid_meta_ind];
    pp->pid = 0;
    pp->fds_backed_up = 0;
    pp->first_parent_signaled = 0;
    pp->vforked = 0;
    return 0;
}

/* child wake parent */
void wake1(int a)
{
    dlog(1, "[ufs: wake1]\n");
    return;
}

/* parent wake child */
void wake2(int a)
{
    dlog(1, "[ufs: wake2]\n");
    this_signaled_by_parent = 1;
    return;
}

void exit_intercept(void)
{
    //dlog(0, "[ufs: exit_intercept] pid: %d, mybucketsused; %d\n",
    //     pid, my_db_used);
#ifdef VFORK_SIGNALING
    maybe_signal_parent();
#endif
    remove_pid_slot();
    return;
}

int trylock_pid_info(pid_info *pi)
{
    int ret = pthread_spin_trylock(&pi->lock);
    if (ret != 0) {
        if (ret == EBUSY) {
            return -1;
        }
        else {
            perror("pthread_spin_trylock");
            exit(EXIT_FAILURE);
        }
    }
    return ret;
}

int unlock_pid_info(pid_info *pi)
{
    int ret = pthread_spin_unlock(&pi->lock);
    if (ret != 0) {
        perror("pthread_spin_unlock");
        exit(EXIT_FAILURE);
    }
    return 0;
}


