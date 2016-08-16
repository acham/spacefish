#ifndef UFSFD_H
#define UFSFD_H

#include <config.h>
#include <linux/limits.h>
#include <sys/stat.h>

typedef struct off_db_mapping_item_t {
    off_t offset;
    unsigned int dbindex;
} off_db_mapping_item;

typedef struct fd_info_t {
    unsigned int mindex;
    /** close on exec flag **/
    int flags;
    /* defer the stamp tp close */
    struct stat priv_s;
    int stamp_type;
    /* cache for pread and pwrite */
#ifdef OFFSET_PROCESS_CACHE
    off_db_mapping_item off_db_mapping[OFFSET_PROCESS_CACHE_SIZE];
    int off_db_ptr;
#endif
    /** internal fd such as stdin, stderr, stdout **/
    int exclusive;
    int empty;
} fd_info;

/* vars */
extern fd_info fdTable[MAX_NUM_FD];
extern fd_info *backup_fds;

/* functions */
extern void init_fdTable();
extern int get_empty_fd(int from, int to);
extern void reset_fd(int fd);
#define FDDES(fd) fdTable[fd]

#endif
