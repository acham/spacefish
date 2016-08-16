/* system */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

/* shmfs */
#include <glibccall.h>
#include <data_shm.h>
#include <meta_shm.h>
#include <config.h>
#include <utility.h>
#include <dlog.h>
#include <execinfo.h>
#include <process.h>

extern uid_t uid;
extern int calling_shm_open;

/* variables related to per-core allocation schemes */
/* the number of the core last used for bucket allocation */
static int last_core_num;

static int last_core_ix;
/* shared memory holding core information */
static core_st *cmem;
#define CRI (cmem[core_ix].rough_ind)

/* static declarations */
static int try_create_step(int steps_read);
static int create_map_dbucket_step(int index);
static int get_set_core_index(void);

void open_map_datastep(int index)
{
    char name[50];
    int fd;
    int step_size = sizeof(data_bucket) * DATA_BUCKETS_STEP;
    sprintf(name, "ufs_md_%d_%d", (int)uid, index);
    calling_shm_open = 1;
    fd = shm_open(name, O_RDWR, 0);
    calling_shm_open = 0;
    EHANDLE("shm_open", fd, -1);
    data_steps[index] =
        mmap(NULL, step_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    real_close(fd);
}

/** open and map main data shm
 **/
int open_map_global_memd(void)
{
    int num_db_steps;

    /* only per-core allocation requires this */
    if (sys_glob_p->alloc_method == 3 || sys_glob_p->alloc_method == 4) {
        last_core_num = -1;
        last_core_ix = -1;
    cmem = NULL;
    }

    num_db_steps = sys_glob_p->dbucket_steps;
    int i;
    for (i = 0; i < num_db_steps; i++) {
        open_map_datastep(i);
    }
    dlog(5, "[ufs: open_map_global_memd] mapped main data shared memory\n");
    return 0;
}

unsigned int bucket_ix_alloc_steps(void)
{
    if (sys_glob_p->dbuckets_in_use ==
        DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS) {
        UEHANDLE("data bucket over-use");
    }
    if (sys_glob_p->dbucket_rough_index >
        (DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS)) {
        if (pthread_spin_trylock(&sys_glob_p->lock) == 0) {
            sys_glob_p->dbucket_rough_index =
                (sys_glob_p->dbucket_rough_index) %
                (DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS);
            pthread_spin_unlock(&sys_glob_p->lock);
        }
    }
    unsigned int steps_read = sys_glob_p->dbucket_steps;
    if (sys_glob_p->dbuckets_in_use == DATA_BUCKETS_STEP * steps_read) {
        try_create_step(steps_read);
        /* step created by me or other process */
    }
    unsigned int i, j;
traverse_current_buckets:
    j = sys_glob_p->dbucket_rough_index;
    steps_read = sys_glob_p->dbucket_steps;
    unsigned int current_num_buckets = DATA_BUCKETS_STEP * steps_read;
    unsigned local_ix;
    data_bucket *db;
    for (i = 0; i < current_num_buckets; i++) {
        local_ix = i + j;
        db = get_dbucket_ptr(local_ix % current_num_buckets);
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) { /* lock error */
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) {
            pthread_spin_unlock(&db->lock);
            continue;
        }
        sys_glob_p->dbucket_rough_index = local_ix;
        db->in_use = 1;
        if (pthread_spin_unlock(&db->lock) != 0) {
            UEHANDLE("data bucket unlock error");
        }
        __sync_fetch_and_add(&(sys_glob_p->dbuckets_in_use), 1);
        db->next_ix = -1;
        db->bytes = 0;
        return local_ix;
    }
    /* traversed all data buckets in current step size */
    /* if possible, try to create a new step */
    if (current_num_buckets == DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS) {
        UEHANDLE("data bucket over-use");
    }
    else {
        try_create_step(steps_read);
        goto traverse_current_buckets;
    }
    /* not reached */
    return -1;
}
 
unsigned int bucket_ix_alloc_all_random(void)
{
    //unsigned int tot_buckets = DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS;
    unsigned int tot_buckets = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    if (sys_glob_p->dbuckets_in_use == tot_buckets) {
        UEHANDLE("data bucket over-use");
    }
    unsigned int dindex = rand() % (tot_buckets);
    unsigned int i;
    unsigned int current_ix;
    for (i = 0; i < tot_buckets; i++) {
        current_ix = (dindex + i) % tot_buckets;
        int step_num = (current_ix / DATA_BUCKETS_STEP);
        unsigned int mod = current_ix % DATA_BUCKETS_STEP;
        data_bucket *db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) { /* just to be sure */
            pthread_spin_unlock(&db->lock);
            continue;
        }
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        //__sync_fetch_and_add(&DBIU, 1);
        //my_db_used++;
        return current_ix;
    }
    UEHANDLE("data bucket over-use");
    /* all buckets traversed */
    return -1;
}


unsigned int bucket_ix_alloc_all_rough_ind(void)
{
    unsigned int tot_steps = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    if (RI >= tot_steps) {
        RI = RI % tot_steps;
    }
    unsigned int i, j, mod;
    int step_num;
    data_bucket *db;
    for (i = 0; i < tot_steps; i++) {
        j = (RI + i) % tot_steps;
        step_num = j / DATA_BUCKETS_STEP;
        mod = j % DATA_BUCKETS_STEP;
        db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        if (db->in_use) {
            pthread_spin_unlock(&db->lock);
            continue;
        }
        /* use bucket for sure */
        RI = j;
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        return j;
    }
    UEHANDLE("data bucket over-use");
    /* not reached */
    return -1;
}
    
unsigned int bucket_ix_alloc_all_per_core_random(void)
{
    int core_ix = get_set_core_index();

    unsigned int tot_bkts = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    unsigned int bkts_per_core = tot_bkts / (sys_glob_p->ncores);
    unsigned int my_start = bkts_per_core * core_ix;

    unsigned int dindex = rand() % (bkts_per_core);
    unsigned int i, j;
    for (i = 0; i < bkts_per_core; i++) {
        j = ((dindex + i) % bkts_per_core) + my_start;
        int step_num = (j / DATA_BUCKETS_STEP);
        unsigned int mod = j % DATA_BUCKETS_STEP;
        data_bucket *db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) { /* just to be sure */
            pthread_spin_unlock(&db->lock);
            continue;
        }
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        //__sync_fetch_and_add(&DBIU, 1);
        //my_db_used++;
        //dlog(0, "[ufs: get_free_dbucket_ix] got bucket index %d, mycore index: %d\n", 
        //     j, core_ix);
        return j;
    }
    UEHANDLE("data bucket over-use");
    /* all buckets traversed */
    return -1;
}

unsigned int bucket_ix_alloc_all_per_core_rough_ind(void)
{
    int core_ix = get_set_core_index();
    
    unsigned int tot_bkts = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    unsigned int bkts_per_core = tot_bkts / (sys_glob_p->ncores);
    unsigned int my_start = bkts_per_core * core_ix;

    /* check core rough index out of range */
    if (CRI >= (bkts_per_core)) {
        CRI = CRI % bkts_per_core;
    }
    unsigned int i, j;
    for (i = 0; i < bkts_per_core; i++) {
        j = ((CRI + i) % bkts_per_core) + my_start;
        int step_num = (j / DATA_BUCKETS_STEP);
        unsigned int mod = j % DATA_BUCKETS_STEP;
        data_bucket *db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) { /* just to be sure */
            pthread_spin_unlock(&db->lock);
            continue;
        }
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        unsigned int new_cri = j - my_start;
        CRI = new_cri;
        return j;
    }
    UEHANDLE("data bucket over-use");
    /* all buckets traversed */
    return -1;
}

data_bucket *bucket_ptr_alloc_steps(void)
{
    if (sys_glob_p->dbuckets_in_use ==
        DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS) {
        UEHANDLE("data bucket over-use");
    }
    if (sys_glob_p->dbucket_rough_index >
        (DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS)) {
        if (pthread_spin_trylock(&sys_glob_p->lock) == 0) {
            sys_glob_p->dbucket_rough_index =
                (sys_glob_p->dbucket_rough_index) %
                (DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS);
            pthread_spin_unlock(&sys_glob_p->lock);
        }
    }
    unsigned int steps_read = sys_glob_p->dbucket_steps;
    if (sys_glob_p->dbuckets_in_use == DATA_BUCKETS_STEP * steps_read) {
        try_create_step(steps_read);
        /* step created by me or other process */
    }
    unsigned int i, j;
traverse_current_buckets_p:
    j = sys_glob_p->dbucket_rough_index;
    steps_read = sys_glob_p->dbucket_steps;
    unsigned int current_num_buckets = DATA_BUCKETS_STEP * steps_read;
    unsigned local_ix;
    data_bucket *db;
    for (i = 0; i < current_num_buckets; i++) {
        local_ix = i + j;
        db = get_dbucket_ptr(local_ix % current_num_buckets);
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) { /* lock error */
            UEHANDLE("data bucket lock error");
        }
        if (db->in_use) {
            pthread_spin_unlock(&db->lock);
            continue;
        }
        /* bucket not in use and lock acquired */
        sys_glob_p->dbucket_rough_index = local_ix;
        db->in_use = 1;
        if (pthread_spin_unlock(&db->lock) != 0) {
            UEHANDLE("data bucket unlock error");
        }
        __sync_fetch_and_add(&(sys_glob_p->dbuckets_in_use), 1);
        db->next_ix = -1;
        db->bytes = 0;
        return db;
    }
    /* traversed all data buckets in current step size */
    /* if possible, try to create a new step */
    if (current_num_buckets == DATA_BUCKETS_STEP * MAX_DATA_BUCKETS_STEPS) {
        UEHANDLE("data bucket over-use");
    }
    else {
        try_create_step(steps_read);
        goto traverse_current_buckets_p;
    }
    /* not reached */
    return NULL;
}

data_bucket *bucket_ptr_alloc_all_random(void)
{
    dlog(1, "[ufs: get_free_dbucket_ptr] (ALLOC_ALL_STEPS version)\n");
    unsigned int tot_buckets = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    if (sys_glob_p->dbuckets_in_use == tot_buckets) {
        UEHANDLE("data bucket over-use");
    }
    unsigned int dindex = rand() % (tot_buckets);
    unsigned int i;
    unsigned int current_ix;
    for (i = 0; i < tot_buckets; i++) {
        current_ix = (dindex + i) % tot_buckets;
        int step_num = (current_ix / DATA_BUCKETS_STEP);
        unsigned int mod = current_ix % DATA_BUCKETS_STEP;
        data_bucket *db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) { /* just to be sure */
            pthread_spin_unlock(&db->lock);
            continue;
        }
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        //__sync_fetch_and_add(&DBIU, 1);
        //my_db_used++;
        return db;
    }
    UEHANDLE("data bucket over-use");
    /* not reached */
    return NULL;
}

data_bucket *bucket_ptr_alloc_all_rough_ind(void)
{
    unsigned int tot_steps = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    if (RI >= tot_steps) {
        RI = RI % tot_steps;
    }
    unsigned int i, j, mod;
    int step_num;
    data_bucket *db;
    for (i = 0; i < tot_steps; i++) {
        j = (RI + i) % tot_steps;
        step_num = j / DATA_BUCKETS_STEP;
        mod = j % DATA_BUCKETS_STEP;
        db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        if (db->in_use) {
            pthread_spin_unlock(&db->lock);
            continue;
        }
        /* use bucket for sure */
        RI = j;
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        return db;
    }
    UEHANDLE("data bucket over-use");
    /* not reached */
    return NULL;
}


data_bucket *bucket_ptr_alloc_all_per_core_random(void)
{
    int core_ix = get_set_core_index();

    unsigned int tot_bkts = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    unsigned int bkts_per_core = tot_bkts / (sys_glob_p->ncores);
    unsigned int my_start = bkts_per_core * core_ix;

    unsigned int dindex = rand() % (bkts_per_core);
    unsigned int i, j;
    for (i = 0; i < bkts_per_core; i++) {
        j = ((dindex + i) % bkts_per_core) + my_start;
        int step_num = (j / DATA_BUCKETS_STEP);
        unsigned int mod = j % DATA_BUCKETS_STEP;
        data_bucket *db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) { /* just to be sure */
            pthread_spin_unlock(&db->lock);
            continue;
        }
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        /* fxmark profiling */
        //my_db_used++;
        //dlog(0, "[ufs: get_free_dbucket_ix] got bucket index %d, mycore index: %d\n", 
        //     j, core_ix);
        return db;
    }
    UEHANDLE("data bucket over-use");
    /* all buckets traversed */
    return NULL;
}

data_bucket *bucket_ptr_alloc_all_per_core_rough_ind(void)
{
    int core_ix = get_set_core_index();
    
    unsigned int tot_bkts = DATA_BUCKETS_STEP * TOTAL_STEPS_ALLOWED;
    unsigned int bkts_per_core = tot_bkts / (sys_glob_p->ncores);
    unsigned int my_start = bkts_per_core * core_ix;

    /* check core rough index out of range */
    if (CRI >= (bkts_per_core)) {
        CRI = CRI % bkts_per_core;
    }
    unsigned int i, j;
    for (i = 0; i < bkts_per_core; i++) {
        j = ((CRI + i) % bkts_per_core) + my_start;
        int step_num = (j / DATA_BUCKETS_STEP);
        unsigned int mod = j % DATA_BUCKETS_STEP;
        data_bucket *db = &data_steps[step_num][mod];
        if (db->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&db->lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        else if (trylock_ret != 0) {
            UEHANDLE("data bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (db->in_use) { /* just to be sure */
            pthread_spin_unlock(&db->lock);
            continue;
        }
        db->in_use = 1;
        pthread_spin_unlock(&db->lock);
        unsigned int new_cri = j - my_start;
        CRI = new_cri;
        return db;
    }
    UEHANDLE("data bucket over-use");
    /* all buckets traversed */
    return NULL;
}


/* algorithm selection arrays */
bucket_ix_alloc_func ix_alloc_arr[] = {
    bucket_ix_alloc_steps,
    bucket_ix_alloc_all_random,
    bucket_ix_alloc_all_rough_ind,
    bucket_ix_alloc_all_per_core_random,
    bucket_ix_alloc_all_per_core_rough_ind
};


bucket_ptr_alloc_func ptr_alloc_arr[] = {
    bucket_ptr_alloc_steps,
    bucket_ptr_alloc_all_random,
    bucket_ptr_alloc_all_rough_ind,
    bucket_ptr_alloc_all_per_core_random,
    bucket_ptr_alloc_all_per_core_rough_ind
};


/* try_create_step should only return once a step has actually be created */
static int try_create_step(int steps_read)
{
    if (steps_read == MAX_DATA_BUCKETS_STEPS) {
        UEHANDLE("max steps created");
    }
    int local_steps_read = sys_glob_p->dbucket_steps;
    int j;
    if (local_steps_read > steps_read) { /* step already created */
        for (j = steps_read; j < local_steps_read; j++) {
            open_map_datastep(j);
            return 0;
        }
    }
    int trylock_ret = pthread_spin_trylock(&sys_glob_p->lock);
    if (trylock_ret == EBUSY) { /* other process creating step */
        /* wait for step creation */
        while (sys_glob_p->dbucket_steps == steps_read)
            ;
    }
    else if (trylock_ret != 0) {
        UEHANDLE("pthread_spin_trylock error");
    }
    else { /* lock acquired: create step */
        create_map_dbucket_step(steps_read);
        pthread_spin_unlock(&sys_glob_p->lock);
        return 0;
    }
    /* other proc created step: map it */
    local_steps_read = sys_glob_p->dbucket_steps;
    for (j = steps_read; j < local_steps_read; j++) {
        open_map_datastep(j);
        return 0;
    }
    /* hopefully not reached */
    UEHANDLE("try_create_step error");
    return -1;
}

static int init_dbucket(data_bucket *db)
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

/* actually create a shm segment for a new step
 * of data buckets
 */
static int create_map_dbucket_step(int index)
{
    char name[50];
    int fd, j;
    int step_size = sizeof(data_bucket) * DATA_BUCKETS_STEP;
    sprintf(name, "ufs_md_%d_%d", (int)uid, index);
    calling_shm_open = 1;
    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    EHANDLE("shm_open", fd, -1);
    calling_shm_open = 0;
    EHANDLE("ftruncate", ftruncate(fd, step_size), -1);
    data_steps[index] = (data_bucket *)mmap(
        NULL, step_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    real_close(fd);
    for (j = 0; j < DATA_BUCKETS_STEP; j++) {
        init_dbucket(&data_steps[index][j]);
    }
    sys_glob_p->dbucket_steps++;
    return 0;
}

/* given index of bucket, try to retrieve pointer.
 * possily need to map new data step
 */
data_bucket *get_dbucket_ptr(unsigned int ix)
{
    int step_num = (ix / DATA_BUCKETS_STEP);
    int steps_read = sys_glob_p->dbucket_steps;
    unsigned int mod;
    data_bucket *db;
    if (step_num > (steps_read - 1)) {
        void *btbuf[5];
        if (backtrace(btbuf, 5) < 0) {
            perror("backtrace");
            exit(1);
        }
        char **syms = backtrace_symbols(btbuf, 5);
        int k;
        for (k = 0; k < 5; k++) {
            dlog(0,"backtrace symbol: %s\n", syms[k]);
        }
        UEHANDLE("reference to data bucket in step not yet created. steps_read: %d, step_num: %d\n", steps_read, step_num);
    }
    if (data_steps[step_num] != NULL) { /* step mapped */
        mod = ix % DATA_BUCKETS_STEP;
        db = &data_steps[step_num][mod];
        return db;
    }
    /* find current last step currently mapped */
    int i;
    for (i = 0; i < MAX_DATA_BUCKETS_STEPS; i++) {
        if (data_steps[i] == NULL)
            break;
    }
    /* map unmapped step(s) */
    for (i = i - 1; i < steps_read; i++) {
        open_map_datastep(i);
    }
    mod = ix % DATA_BUCKETS_STEP;
    db = &data_steps[step_num][mod];
    return db;
}

int release_dbucket(data_bucket *db)
{
    if (!db->in_use) {
        return -1;
    }
    else {
        db->in_use = 0;
        __sync_fetch_and_add(&(sys_glob_p->dbuckets_in_use), -1);
        return 0;
    }
}

int get_set_core_index(void)
{
    if (!cmem) { /* core-related shared memory not mapped yet */        
        char name[50];
        int fd;
        int cores_shm_sz = sizeof(core_st) * (sys_glob_p->ncores);
        sprintf(name, "ufs_cores_%d", (int)uid);
        calling_shm_open = 1;
        fd = shm_open(name, O_RDWR, 0);
        calling_shm_open = 0;
        EHANDLE("shm_open", fd, -1);
        void *cmap = mmap(NULL, cores_shm_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        EHANDLE("mmap", cmap, MAP_FAILED);
        cmem = (core_st *)cmap;
        real_close(fd);
    }
    int cnum = sched_getcpu();
    if (cnum == last_core_num) {
        return last_core_ix;
    }
    int i;
    int set = 0;
    for (i = 0; i < sys_glob_p->ncores; i++) {
        if (cmem[i].in_use) {
            if (cmem[i].core_n == cnum) {
                set = 1;
                break;
            }
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&cmem[i].lock);
        if (trylock_ret == EBUSY) {
            continue;
        }
        if (cmem[i].in_use) {
            pthread_spin_unlock(&cmem[i].lock);
            continue;
        }
        cmem[i].in_use = 1;
        cmem[i].core_n = cnum;
        pthread_spin_unlock(&cmem[i].lock);
        set = 1;
        break;
    }
    if (!set) {
        UEHANDLE("[ufs: open_map_register_core_mem] per-core allocation selected but core mismatch (core %d)\n",
            cnum);
    }
    last_core_ix = i;
    last_core_num = cnum;
    return i;
}
