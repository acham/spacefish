#ifndef DATA_SHM_H
#define DATA_SHM_H

/* system */
#include <pthread.h>

/* shmfs */
#include <config.h>
//#include <meta_shm.h>

typedef struct data_bucket_t {
    int in_use;
    unsigned int next_ix;
    char buf[DBUCKET_BUF_SIZE];
    size_t bytes;
    pthread_spinlock_t lock;
} data_bucket;

/* data structure holding core information:
 * each core holds one, allocation protected by lock
 */
typedef struct core_st_t {
    int in_use;
    int core_n;
    /* note: this is a relative index */
    unsigned int rough_ind;
    pthread_spinlock_t lock;
} core_st;

extern data_bucket **data_steps;
extern int mycore;
extern int alloc_method_ind;


void open_map_datastep(int index);
int open_map_global_memd(void);


/* Given the logical index of an existing
 * data_bucket, returns a pointer to it.
 * Kills process if index is to a bucket
 * that does not exist.
 */
data_bucket *get_dbucket_ptr(unsigned int ix);

/* Release a data bucket for reuse.
 * On success, return 0.
 * On fail, when db is not used, return -1.
 */
int release_dbucket(data_bucket *db);

/* macros */
#define RI (sys_glob_p->dbucket_rough_index)
#define DBIU sys_glob_p->dbuckets_in_use


/*******************************************
 * bucket allocation: higher-level functions 
 * should only use "get_free_dbucket_ix" and 
 * "get_free_dbucket_ptr"
 *******************************************/
unsigned int bucket_ix_alloc_steps(void);
unsigned int bucket_ix_alloc_all_random(void);
unsigned int bucket_ix_alloc_all_rough_ind(void);
unsigned int bucket_ix_alloc_all_per_core_random(void);
unsigned int bucket_ix_alloc_all_per_core_rough_ind(void);
data_bucket *bucket_ptr_alloc_steps(void);
data_bucket *bucket_ptr_alloc_all_random(void);
data_bucket *bucket_ptr_alloc_all_rough_ind(void);
data_bucket *bucket_ptr_alloc_all_per_core_random(void);
data_bucket *bucket_ptr_alloc_all_per_core_rough_ind(void);

/* generic bucket allocation function returning index */
typedef unsigned int (*bucket_ix_alloc_func)(void);

/* generic bucket allocation function returning pointer */
typedef data_bucket * (*bucket_ptr_alloc_func)(void);

extern bucket_ix_alloc_func ix_alloc_arr[];
extern bucket_ptr_alloc_func ptr_alloc_arr[];

/* Gets a free data bucket,
 * marks it in use, and returns
 * its logical index.
 */
#define get_free_dbucket_ix ix_alloc_arr[alloc_method_ind]

/* Same as above, but returns a pointer
 * to the bucket instead. Use this
 * function instead of
 * "get_free_dbucket_ix" + "get_dbucket_ptr"
 * for performance.
 */
#define get_free_dbucket_ptr ptr_alloc_arr[alloc_method_ind]

#endif
