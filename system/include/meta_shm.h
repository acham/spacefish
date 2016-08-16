#ifndef META_SHM_H
#define META_SHM_H

/* system */
#include <linux/limits.h>

/* shmfs */
#include <config.h>
#include <rwlock.h>

typedef struct sys_glob_t {
    /* currently used data bucket steps */
    int dbucket_steps;
    /* rough index of current bucket usedd */
    unsigned int dbucket_rough_index;
    unsigned int dbuckets_in_use;
    pthread_spinlock_t lock;
    int ncores;
    int alloc_method;
} sys_glob;

typedef struct meta_bucket_t {
    int in_use;
    int next_ix;
    char path[PATH_MAX];
    struct stat s;
    unsigned int first_dbucket;
    unsigned int last_dbucket;
    /* below is the first non-used byte in the
     * last data bucket corresponding to this file
     */
    int last_db_offset;
    int consisent;
    pthread_spinlock_t lock;
} meta_bucket;

typedef struct hashinfo_t {
    int first_ix;
    urw_lock lock;
} hashinfo;

/** vars **/
extern sys_glob *sys_glob_p;
extern hashinfo *hash_table;
extern meta_bucket *mb_arr;

/** functions **/
int open_map_global_memm(void);
int init_mbucket(meta_bucket *mb);
unsigned long hash(const char *str);

/* Get *existing* metadata bucket for file.
 * input: full path
 * not found: return NULL
 */
meta_bucket *get_mbucket(const char *str);

/* Get a new metadata bucket for a file if it does not exist.
 * if metadata bucket exists for file, return pointer to it
 * input : full path
 */
meta_bucket *get_or_create_mbucket(const char *str);

/* Strictly get an *empty* metadata bucket given input string.
 * If a metadata bucket already exists for the file,
 * return NULL.
 */
meta_bucket *strict_create_mbucket(const char *str);

/* Release a metadata bucket given a path.
 * Remove bucket from linked list and mark
 * it as not in use.
 * return -1 if can't
 */
int release_meta_bucket(const char *str);

/* Move a metadata bucket from one htable row
 * to another, changing its path (rename).
 * Old and new paths are inputs.
 * If there already exists a bucket for the new path,
 * it is replaced by the bucket for the old path.
 * Returns 0 on success, -1 on error.
 */
int mb_chpath(const char *oldpath, const char *newpath);

/** macros **/
/* convert metadata bucket index to pointer */
#define MB_PTR(index) (&mb_arr[index])
/* convert metadata pointer to index */
#define MB_IX(ptr) (ptr - mb_arr)
/* mark meta bucket modified */
#define MB_MODIFY(ptr); \
        if (ptr->consisent) { \
            ptr->consisent = 0; \
        }

#endif
