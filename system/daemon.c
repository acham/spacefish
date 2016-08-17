#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/shm.h>
#include <mqueue.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

/* ufs */
#include <config.h>
#include <utility.h>
#include <fd.h>
#include <data_shm.h>
#include <meta_shm.h>
#include <process.h>
#include <dlog.h>
#include <arch.h>

uid_t uid;
/* metadata main shm  */
char mainm_shm_name[50];
int mainm_shm_fd;
static void *m_memm;
/* data main shm */
char maind_shm_name[50];
int maind_shm_fd;
static void *m_memd;
/* pid_meta shm */
char pid_meta_shm_name[50];
int pid_meta_fd;
pid_info *pid_meta;
int ufs_init_called = 0;
int fd_backup_fd;
int calling_shm_open;
sys_glob *sys_glob_p = NULL;
hashinfo *hash_table = NULL;
meta_bucket *mb_arr = NULL;
data_bucket **data_steps = NULL;
char cores_shm_name[50];
static int pid_meta_sz;
static int maind_shm_size;
static unsigned long long mainm_shm_size;
static int num_cores;
/* daemon-wide global index of allocation method */
int alloc_method_ind;
/* initialize array of pid_meta */
static void init_pid_meta(pid_info *pid_meta_)
{
    int j, s;
    for (j = 0; j < MAX_EXEC_HANGING; j++) {
        pid_meta_[j].pid = 0;
        pid_meta_[j].fds_backed_up = 0;
        pid_meta_[j].first_parent_signaled = 0;
        pid_meta_[j].vforked = 0;
        s = pthread_spin_init(&pid_meta_[j].lock, PTHREAD_PROCESS_SHARED);
        if (s != 0) {
            perror("pthread_mutexattr_init");
            exit(EXIT_FAILURE);
        }
    }
}

/* init single metadata bucket */
int init_mbucket(meta_bucket *mb)
{
    mb->in_use = 0;
    mb->next_ix = -1;
    if (pthread_spin_init(&mb->lock, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_spin_init");
        exit(1);
    }
    mb->first_dbucket = -1;
    mb->last_dbucket = -1;
    mb->last_db_offset = 0;
    mb->consisent = 1;
    return 0;
}

/* init single data bucket */
int init_dbucket(data_bucket *db)
{
    db->in_use = 0;
    db->next_ix = -1;
    db->bytes = 0;
    if (pthread_spin_init(&db->lock, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_spin_init");
        exit(1);
    }
    return 0;
}

#ifdef UFS_PROFILE
long* profile_mem;
char profile_shm_name[50];
#endif

void init_mount(char *root)
{
    int j;
    char default_root[] = ".";
    if (root == NULL) {
        root = default_root;
    }
    /* meta shared mem init */
    mainm_shm_size = sizeof(sys_glob) + sizeof(hashinfo) * HASH_TABLE_SIZE +
                         sizeof(meta_bucket) * META_BUCKETS;
    uid = getuid();
    sprintf(mainm_shm_name, "ufs_mainm_%d", (int)uid);
    mainm_shm_fd =
        shm_open(mainm_shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    EHANDLE("shm_open", mainm_shm_fd, -1);
    EHANDLE("ftruncate", ftruncate(mainm_shm_fd, mainm_shm_size), -1);
    m_memm = mmap(NULL, mainm_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                  mainm_shm_fd, 0);
    EHANDLE("mmap", m_memm, MAP_FAILED);
    dlog(1, "[ufs-daemon: init] mapped main metadata memory with size %llu\n",
         mainm_shm_size);
    /* init sys_glob struct */
    sys_glob *sp = (sys_glob *)m_memm;
    if (alloc_method_ind > 0) {
        sp->dbucket_steps = TOTAL_STEPS_ALLOWED;
    }
    else {
        sp->dbucket_steps = 1;
    }
    sp->ncores = num_cores;
    sp->alloc_method = alloc_method_ind;
    /* init shm for holding core information
     * if allocation type is per-core
     */
    if (alloc_method_ind > 2) {
        int cores_shm_sz = sizeof(core_st) * num_cores;
        sprintf(cores_shm_name, "ufs_cores_%d", (int)uid);
        int cores_shm_fd = shm_open(cores_shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
        EHANDLE("shm_open", cores_shm_fd, -1);
        EHANDLE("ftruncate", ftruncate(cores_shm_fd, cores_shm_sz), -1);
        void *cmap = mmap(NULL, cores_shm_sz, PROT_READ | PROT_WRITE, MAP_SHARED, cores_shm_fd, 0);
        EHANDLE("mmap", cmap, MAP_FAILED);
        core_st *cmem = (core_st *)cmap;
        int n;
        for (n = 0; n < num_cores; n++) {
            cmem[n].in_use = 0;
            cmem[n].core_n = -1;
            cmem[n].rough_ind = 0;
            pthread_spin_init(&cmem[n].lock, PTHREAD_PROCESS_SHARED);
        }
    }
    
    sp->dbucket_rough_index = 0;
    sp->dbuckets_in_use = 0;
    if (pthread_spin_init(&sp->lock, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_spin_init");
        exit(1);
    }
    /* init hash table */
    hashinfo *ht = (hashinfo *)(m_memm + sizeof(sys_glob));
    for (j = 0; j < HASH_TABLE_SIZE; j++) {
        ht[j].first_ix = -1;
		init_urwlock(&ht[j].lock);
    }
    /* init metadata buckets */
    meta_bucket *mb = (meta_bucket *)((void *)ht + sizeof(hashinfo) * HASH_TABLE_SIZE);
    for (j = 0; j < META_BUCKETS; j++) {
        init_mbucket(&mb[j]);
    }
    /* data buckets shared mem init
     * only init 1 step if using the "steps" allocation methods
     */
    data_steps = malloc(sizeof(data_bucket *) * MAX_DATA_BUCKETS_STEPS);
    maind_shm_size = sizeof(data_bucket) * DATA_BUCKETS_STEP;
    /* allocate all steps */
    if (alloc_method_ind > 0) {
        int k;
        data_bucket *db_arr;
        for (k = 0; k < TOTAL_STEPS_ALLOWED; k++) {
            sprintf(maind_shm_name, "ufs_md_%d_%d", (int)uid, k);
            maind_shm_fd =
                shm_open(maind_shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
            EHANDLE("shm_open", maind_shm_fd, -1);
            EHANDLE("ftruncate", ftruncate(maind_shm_fd, maind_shm_size), -1);
            m_memd = mmap(NULL, maind_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          maind_shm_fd, 0);
            EHANDLE("mmap", m_memd, MAP_FAILED);
            dlog(1, "[ufs-daemon: init] mapped main data memory with size %d\n",
                 maind_shm_size);
            db_arr = (data_bucket *)m_memd;
            data_steps[k] = db_arr;
            /* init data buckets */
            for (j = 0; j < DATA_BUCKETS_STEP; j++) {
                init_dbucket(&db_arr[j]);
            }
        }
        long unsigned tot_mem_sz = TOTAL_STEPS_ALLOWED * DATA_BUCKETS_STEP * sizeof(data_bucket);
        dlog(-1, "[ufs-daemon] allocated all %d steps\n"
             "[ufs-daemon] total shared mem allocated: %lu bytes\n",
             TOTAL_STEPS_ALLOWED,
             tot_mem_sz);
    }
    /* allocation method : 1 step at a time */
    else {
        sprintf(maind_shm_name, "ufs_md_%d_0", (int)uid);
        maind_shm_fd =
            shm_open(maind_shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
        EHANDLE("shm_open", maind_shm_fd, -1);
        EHANDLE("ftruncate", ftruncate(maind_shm_fd, maind_shm_size), -1);
        m_memd = mmap(NULL, maind_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      maind_shm_fd, 0);
        EHANDLE("mmap", m_memd, MAP_FAILED);
        dlog(1, "[ufs-daemon: init] mapped main data memory with size %d\n",
             maind_shm_size);
        data_bucket *db_arr;
        db_arr = (data_bucket *)m_memd;
        /* init data buckets */
        for (j = 0; j < DATA_BUCKETS_STEP; j++) {
            init_dbucket(&db_arr[j]);
        }
        data_steps[0] = db_arr;
    }
    
    /**
     ** set up list of processes with hanging fd metadata
     ** also contains info: was process vfork'ed
     ** also contains info: was first parent signaled
     **/
    pid_meta_sz = MAX_EXEC_HANGING * sizeof(pid_info);
    sprintf(pid_meta_shm_name, "ufs_pid_meta_%d", (int)uid);
    pid_meta_fd = shm_open(pid_meta_shm_name, O_CREAT | O_EXCL | O_RDWR,
                           S_IRUSR | S_IWUSR);
    EHANDLE("shm_open", pid_meta_fd, -1);
    EHANDLE("fd_truncate", ftruncate(pid_meta_fd, pid_meta_sz), -1);
    pid_meta = (pid_info *)mmap(NULL, pid_meta_sz, PROT_READ | PROT_WRITE,
                                MAP_SHARED, pid_meta_fd, 0);
    EHANDLE("mmap", pid_meta, MAP_FAILED);
    dlog(1, "[ufs-daemon init] mapped pid_meta shm with size %d\n",
         pid_meta_sz);
    init_pid_meta(pid_meta);

    sys_glob_p = sp;
    hash_table = ht;
    mb_arr = mb;
    //data_steps = malloc(sizeof(data_bucket *) * MAX_DATA_BUCKETS_STEPS);
    
    upload_dir(root);
}

void cleanup(int g)
{
    int j, steps;
    steps = sys_glob_p->dbucket_steps;
    for (j = 1; j < steps; j++) {
        open_map_datastep(j);
    }
    /* might cause serious crash */
    clock_t begin, end;
    double time_spent;

    begin = clock();
    flush_write();
    end = clock();
    time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("write to disk: %f\n", time_spent);

    EHANDLE("shm_unlink", shm_unlink(mainm_shm_name), -1);
    for (j = 0; j < steps; j++) {
        sprintf(maind_shm_name, "ufs_md_%d_%d", (int)uid, j);
        EHANDLE("shm_unlink", shm_unlink(maind_shm_name), -1);
    }
    EHANDLE("shm_unlink", shm_unlink(pid_meta_shm_name), -1);
    if (alloc_method_ind == 3 || alloc_method_ind == 4) {
        EHANDLE("shm_unlink", shm_unlink(cores_shm_name), -1);
    }
    // add fd_backup shm removal, after
    exit(0);
}

static void handle_sigaction(struct sigaction *sa)
{
    int i;
    sa->sa_handler = cleanup;
    sigemptyset(&(sa->sa_mask));
    sa->sa_flags = 0;

    for (i = 1; i < NSIG; i++) {
        switch (i) {
        case SIGKILL:
        case SIGTSTP:
        case SIGCONT:
            break;
        default:
            sigaction(i, sa, NULL);
            break;
        }
    }
}

static int parse_options(int argc, char *argv[], struct cmd_opt *opt)
{
    int arg_cnt;
    static struct option options[] = {{"path", required_argument, 0, 'p'},
                                      {"enabledaemon", no_argument, 0, 'd'},
                                      {0, 0, 0, 0}};

    opt->dir_name = NULL;
    opt->daemon_mode = 0;

    for (arg_cnt = 0; 1; ++arg_cnt) {
        int c, idx = 0;
        c = getopt_long(argc, argv, "p:d:", options, &idx);
        if (c == -1)
            break;

        switch (c) {
        case 'p':
            opt->dir_name = optarg;
            break;
        case 'd':
            opt->daemon_mode = 1;
            break;
        default:
            return -EINVAL;
        }
    }
    return arg_cnt;
}

static void usage(FILE *out, const char *dname)
{
    fprintf(out, "usage: %s\n", dname);
    fprintf(out, "  --path          = path for mounting / loading data\n");
    fprintf(out, "  --enabledaemon  = enable daemon mode\n");
}

int main(int argc, char **argv)
{
    int count_alloc_algos = 0;
#ifdef ALLOC_STEPS
    count_alloc_algos += 1;
    alloc_method_ind = 0;
#endif
#ifdef ALLOC_ALL_RANDOM
    count_alloc_algos += 1;
    alloc_method_ind = 1;
#endif
#ifdef ALLOC_ALL_ROUGH_INDEX
    count_alloc_algos += 1;
    alloc_method_ind = 2;
#endif
#ifdef ALLOC_ALL_PER_CORE_RANDOM
    count_alloc_algos += 1;
    alloc_method_ind = 3;
#endif
#ifdef ALLOC_ALL_PER_CORE_ROUGH_IND
    count_alloc_algos += 1;
    alloc_method_ind = 4;
#endif
    if (count_alloc_algos != 1) {
        fprintf(stderr, "Exactly one bucket allocation method"
                " must be selected. Please see README.md\n");
        exit(1);
    }

#if 0
    if (alloc_method_ind == 4) {
        fprintf(stderr, "per-core-rough-index method not implemented\n");
        exit(1);
    }
#endif
    struct sigaction sa;
    struct cmd_opt opt = {NULL, 0};

    if (parse_options(argc, argv, &opt) < 0) {
        usage(stderr, argv[0]);
        exit(1);
    }

    num_cores = 0;
    if (alloc_method_ind == 3 || alloc_method_ind == 4) {
        if (argc != 2) {
            fprintf(stderr, "PER_CORE_ALLOC usage: daemon [num-cores]\n");
            exit(1);
        }
        num_cores = atoi(argv[1]);
    }
    
    init_mount(opt.dir_name);
    dlog(-1, "[daemon] Finish mount.\n");
    
    handle_sigaction(&sa);

    if (opt.daemon_mode) {
        int pid;
        int fl;

        if ((pid = fork()) == 0) {
            int num_fds = getdtablesize();

            fflush(stdout);
            for (fl = 0; fl <= num_fds; ++fl) {
                if (fl != 2) /* leave stderr open */
                    close(fl);
            }
            setsid();
        }
        else {
            /*
             * parent: get pid and print,
             * to send to wrapping script, if it exists
             */
            printf("%i\n", pid);
            exit(0);
        }
    }

    while (1) {
        nop_pause();
    }

    /* not reached */
    cleanup(-1);
    return 1;
}
