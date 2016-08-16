/* sys */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

/* shmfs */
#include <config.h>
#include <glibccall.h>
#include <meta_shm.h>
#include <dlog.h>
#include <utility.h>

extern uid_t uid;
extern int calling_shm_open;

/* static declarations */
static int allocate_mbucket(void);

/** open and map main metadata shm
 **/
int open_map_global_memm(void)
{
    unsigned long long mainm_shm_size = sizeof(sys_glob) + sizeof(hashinfo) * HASH_TABLE_SIZE +
        sizeof(meta_bucket) * META_BUCKETS;
    char mainm_shm_name[50];
    int mainm_shm_fd;
    sprintf(mainm_shm_name, "ufs_mainm_%d", (int)uid);
    calling_shm_open = 1;
    mainm_shm_fd = shm_open(mainm_shm_name, O_RDWR, 0);
    calling_shm_open = 0;
    EHANDLE("shm_open", mainm_shm_fd, -1);
    void *global_memm = mmap(NULL, mainm_shm_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, mainm_shm_fd, 0);
    /* assign global pointers */
    sys_glob_p = (sys_glob *)global_memm;
    hash_table = (hashinfo *)(global_memm + sizeof(sys_glob));
    mb_arr = (meta_bucket *)((void *)hash_table +
                             sizeof(hashinfo) * HASH_TABLE_SIZE);
    real_close(mainm_shm_fd);
    dlog(5, "[ufs: open_map_global_memm] mapped main metadata shared memory\n");
    return 0;
}

/* get *existing* metadata bucket for file
 * input: full path
 * not found: return NULL
 */
meta_bucket *get_mbucket(const char *str)
{
    /* lock hash table row */
    hashinfo *hi = &hash_table[hash(str)];
	down_read(&hi->lock);
    int mb_ix = hi->first_ix;
    meta_bucket *mb = NULL;
    while (mb_ix != -1) {
        mb = MB_PTR(mb_ix);
        if (!strncmp(str, mb->path, PATH_MAX)) {
            unlock_rw(&hi->lock, 1);
            return mb;
        }
        mb_ix = mb->next_ix;
    }
	up_read(&hi->lock);
    return NULL;
}


meta_bucket *get_or_create_mbucket(const char *str)
{
    /* lock hash table row */
    hashinfo *hi = &hash_table[hash(str)];
	down_write(&hi->lock);
    int mb_ix = hi->first_ix;
    int ret_mb_ix;
    meta_bucket *mb = NULL;
    if (mb_ix == -1) {
        ret_mb_ix = allocate_mbucket();
        hi->first_ix = ret_mb_ix;
        unlock_rw(&hi->lock, 2);
        return MB_PTR(ret_mb_ix);
    }
    mb = MB_PTR(mb_ix);
    /* first */
    if ((mb->next_ix == -1) && !strncmp(str, mb->path, PATH_MAX)) {
        unlock_rw(&hi->lock, 2);
        return mb;
    }
    while (mb->next_ix != -1) {
        if (!strncmp(str, mb->path, PATH_MAX)) {
			up_write(&hi->lock);
            return mb;
        }
        mb = MB_PTR(mb->next_ix);
    }
    /* mb has no next: last metadata in linked list */
    if (!strncmp(str, mb->path, PATH_MAX)) {
		up_write(&hi->lock);
        return mb;
    }
    /* end of list, metadata bucket not found */
    ret_mb_ix = allocate_mbucket();
    mb->next_ix = ret_mb_ix;
	up_write(&hi->lock);
    return MB_PTR(ret_mb_ix);
}

meta_bucket *strict_create_mbucket(const char *str)
{
    /* lock hash table row */
    hashinfo *hi = &hash_table[hash(str)];
	down_write(&hi->lock);
    int mb_ix = hi->first_ix;
    int ret_mb_ix;
    meta_bucket *mb = NULL;
    if (mb_ix == -1) {
        ret_mb_ix = allocate_mbucket();
        if (ret_mb_ix == -1) {
			up_write(&hi->lock);
            return NULL;
        }
        hi->first_ix = ret_mb_ix;
		up_write(&hi->lock);
        return MB_PTR(ret_mb_ix);
    }
    mb = MB_PTR(mb_ix);
    /* first */
    if ((mb->next_ix == -1) && !strncmp(str, mb->path, PATH_MAX)) {
		up_write(&hi->lock);
        return NULL;
    }
    while (mb->next_ix != -1) {
        if (!strncmp(str, mb->path, PATH_MAX)) {
			up_write(&hi->lock);
            /* bucket already exists, in strict_create function */
            return NULL;
        }
        mb = MB_PTR(mb->next_ix);
    }
    /* mb has no next: last metadata in linked list */
    if (!strncmp(str, mb->path, PATH_MAX)) {
		up_write(&hi->lock);
        return NULL;
    }
    ret_mb_ix = allocate_mbucket();
    if (ret_mb_ix == -1) {
		up_write(&hi->lock);
        return NULL;
    }
    mb->next_ix = ret_mb_ix;
	up_write(&hi->lock);
    return MB_PTR(ret_mb_ix);
}

/* "allocate" an empty metadata bucket.
 * that is, return index to a mbucket
 * that is not in use in shared memory array.
 * start searching random location, then traverse forward
 */
static int allocate_mbucket(void)
{
    int mindex = rand() % META_BUCKETS;
    int i;
    for (i = 0; i < META_BUCKETS; i++) {
        int current_ix = (mindex + i) % META_BUCKETS;
        meta_bucket *mb = MB_PTR(current_ix);
        if (mb->in_use) {
            continue;
        }
        int trylock_ret = pthread_spin_trylock(&mb->lock);
        if (trylock_ret == EBUSY) {
            /* other proc allocating this bucket */
            continue;
        }
        else if (trylock_ret != 0) { /* error on lock */
            UEHANDLE("metadata bucket lock error");
        }
        /* bucket not in use and lock acquired */
        if (mb->in_use) {
            pthread_spin_unlock(&mb->lock);
            continue;
        }
        mb->in_use = 1;
        if (pthread_spin_unlock(&mb->lock) != 0) {
            UEHANDLE("metadata bucket unlock error");
        }
        return current_ix;
    }
    /* traversed all metadata bucket. there might be some
     * freed in the meantime, but this is cutting it too close.
     * exit with error
     */
    //UEHANDLE("metadata bucket over-use");
    /* not reached */
    return -1;
}

/* helper function
 * given a path, 
 * get pointers to a metadata bucket and to its 
 * previous bucket.
 * Afterwards: 
 *  - *mb == NULL means no bucket for path
 *  - *mb != NULL but *mb_prev == NULL means
 *    bucket first in list.
 * - *mb == NULL but *mb_prev != NULL:
 *  bucket not found, and mb_prev points to last
 *  bucket in list for path hash.
 * Assume locks are taken care of by calling context.
 */
static int get_mb_and_prev(const char *path,       //in
                           meta_bucket **mb,       //out
                           meta_bucket **mb_prev)  //out
{
    *mb = NULL;
    *mb_prev = NULL;
    hashinfo *hi = &hash_table[hash(path)];
    if (hi->first_ix == -1) {
        return 0;
    }
    *mb = MB_PTR(hi->first_ix);
    /* only one bucket in linked list */
    if ((*mb)->next_ix == -1) {
        if (!strncmp(path, (*mb)->path, PATH_MAX)) {
            return 0;
        }
        else {
            *mb_prev = *mb;
            *mb = NULL;
            return 0;
        }
    }
    /* two or more buckets */
    else {
        if (!strncmp(path, (*mb)->path, PATH_MAX)) {
            return 0;
        }
        else {
            *mb_prev = *mb;
            int ix_iter = (*mb)->next_ix;
            while (ix_iter != -1) {
                *mb = MB_PTR(ix_iter);
                if (!strncmp(path, (*mb)->path, PATH_MAX)) {
                    return 0;
                }
                ix_iter = (*mb)->next_ix;
                *mb_prev = *mb;
            }
            /* not found */
            *mb = NULL;
            return 0;
        }
    }
    /* not reached */
    return -1;
}


int release_meta_bucket(const char *str)
{
    meta_bucket *mb = NULL;
    meta_bucket *mb_prev = NULL;
    /* lock hash table row */
    hashinfo *hi = &hash_table[hash(str)];
	down_write(&hi->lock);
    get_mb_and_prev(str, &mb, &mb_prev);
    if (!mb) {
		up_write(&hi->lock);
        return -1;
    }
    if (!mb_prev) {
        /* target first in list */
        hi->first_ix = mb->next_ix;
    }
    else {
        mb_prev->next_ix = mb->next_ix;
    }
    mb->next_ix = -1;
	up_write(&hi->lock);
    if (pthread_spin_lock(&mb->lock) != 0) {
        UEHANDLE("metadata bucket lock error");
    }
    mb->in_use = 0;
    mb->first_dbucket = -1;
    mb->last_dbucket = -1;
    mb->last_db_offset = 0;
    if (pthread_spin_unlock(&mb->lock) != 0) {
        UEHANDLE("metadata bucket unlock error");
    }
    return 0;
}


int mb_chpath(const char *oldpath, const char *newpath)
{
    /* check */
    if (!strncmp(oldpath, newpath, PATH_MAX)) {
        return 0;
    }
    /* lock both hash table rows */
    hashinfo *hi_old = &hash_table[hash(oldpath)];
    hashinfo *hi_new = &hash_table[hash(newpath)];
	down_write(&hi_old->lock);
	down_write(&hi_new->lock);
    meta_bucket *mb_old = NULL;
    meta_bucket *mb_old_prev = NULL;
    /* mb_new != NULL : overwrite */
    meta_bucket *mb_new = NULL;
    meta_bucket *mb_new_prev = NULL;
    /* fill pointers */
    get_mb_and_prev(oldpath, &mb_old, &mb_old_prev);
    get_mb_and_prev(newpath, &mb_new, &mb_new_prev);
    /* oldpath not found */
    if (!mb_old) {
        return -1;
    }
    /* get old index */
    int old_ix;
    if (!mb_old_prev) {
        old_ix = hi_old->first_ix;
    }
    else {
        old_ix = mb_old_prev->next_ix;
    }
    /* new path exists: overwrite */
    if (mb_new != NULL) {
        strncpy(mb_old->path, newpath, PATH_MAX);
        /* fix prev in old list */
        if (!mb_old_prev) {
            hi_old->first_ix = mb_old->next_ix;
        }
        else {
            mb_old_prev->next_ix = mb_old->next_ix;
        }
        mb_old->next_ix = mb_new->next_ix;
        /* fix prev in new position */
        if (!mb_new_prev) {
            hi_new->first_ix = old_ix;
        }
        else {
            mb_new_prev->next_ix = old_ix;
        }
        /* free "over-written" bucket */
        if (pthread_spin_lock(&mb_new->lock) != 0) {
            UEHANDLE("metadata bucket lock error");
        }
        mb_new->in_use = 0;
        mb_new->next_ix = -1;
        mb_new->first_dbucket = -1;
        mb_new->last_dbucket = -1;
        mb_new->last_db_offset = 0;
        if (pthread_spin_unlock(&mb_new->lock) != 0) {
            UEHANDLE("metadata bucket unlock error");
        }
		up_write(&hi_new->lock);
		up_write(&hi_old->lock);
        return 0;
    }
    /* new path does not exist */
    else {
        strncpy(mb_old->path, newpath, PATH_MAX);
        /* fix prev in old list */
        if (!mb_old_prev) {
            hi_old->first_ix = mb_old->next_ix;
        }
        else {
            mb_old_prev->next_ix = mb_old->next_ix;
        }
        mb_old->next_ix = -1;
        /* fix prev in new list */
        if (!mb_new_prev) {
            hi_new->first_ix = old_ix;
        }
        else {
            mb_new_prev->next_ix = old_ix;
        }
        /* unlock table rows */
		up_write(&hi_new->lock);
		up_write(&hi_old->lock);
        return 0;
    }
    /* not reached */
    return -1;
}
