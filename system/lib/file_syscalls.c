#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* do not change the order of include*/
#include <stdlib.h>
#include <string.h>
#define _FCNTL_H
#include <bits/fcntl.h>
#include <errno.h>
#include <time.h>

/* ufs */
#include <glibccall.h>
#include <utility.h>
#include <fd.h>
#include <meta_shm.h>
#include <data_shm.h>

#include <dlog.h>

extern int calling_shm_open;
extern int calling_shm_unlink;
extern int errno;

#define O_TRUNC_CHECK(flags) ((flags & O_TRUNC) && ((flags & O_RDWR) || (flags & O_WRONLY)))

static inline meta_bucket *create_new_file(const char *fpath, int flags, mode_t mode)
{
    unsigned int dbindex;
    meta_bucket *mb;

    mb = strict_create_mbucket(fpath);
    if (!mb) {
        errno = ENOSPC;
        return NULL;
    }
    dbindex = get_free_dbucket_ix();
    mb->first_dbucket = dbindex;
    mb->s.st_mode = mode;
    mb->s.st_size = 0;
    mb->s.st_blocks = 1;
    mb->s.st_blksize = DBUCKET_BUF_SIZE;
    strcpy(mb->path, fpath);
    stamp(&mb->s, ST_ACS | ST_MOD | ST_CHG);
    MB_MODIFY(mb);

    return mb;
}

static inline int exclusive_path(const char *path)
{
    int i;
#define EXPATHNUM 16
    char *exlist[EXPATHNUM] = {"/dev/", "/proc/", 0};
    for (i = 0; i < EXPATHNUM; i++) {
        if (exlist[i] == 0) {
            return 0;
        }
        if (strstr(path, exlist[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* For ufs setup, calling_shm_open is set to 1 and directly return real_open.
 * Flags without O_CREAT, open first finds the file in ufs memory then on disk.
 * Flags with O_TRUNC, open skips the disk look up, for file on disk will be overwritten later.
 * Flags without O_TRUNC, open needs to check both ufs memory and disk. 
 * If not define O_TRUNC_FREE option, open will keep the data bucket and set the in-used bytes to 0 instead of releasing the data bucket.
 */
int open(const char *pathname, int flags, ...)
{
    va_list ap;
    mode_t mode;
    va_start(ap, flags);
    mode = va_arg(ap, mode_t);
    va_end(ap);

    if (calling_shm_open) {
        return real_open(pathname, flags, mode);
    }

    int ret = -1;
    int err = 0;

    if (exclusive_path(pathname)) {
        ret = real_open(pathname, flags, mode);
        err = errno;
        if (ret > 0) {
            FDDES(ret).empty = 0;
            FDDES(ret).exclusive = 1;
        }
        goto open_return;
    }
    else {
        unsigned int dbindex;
        meta_bucket *mb;
        data_bucket *db;
        int fd;
        const char *fpath;

        if (strlen(pathname) > PATH_MAX) {
            ret = -1;
            err = ENAMETOOLONG;
            goto open_return;
        }
        BUILD_ABS_PATH_1(fpath, pathname);
        if (!(flags & O_CREAT)) {
            if (!(mb = get_mbucket(fpath))) {
                mb = upload_file(fpath);
                if (!mb) {
                    ret = -1;
                    err = errno;
                    goto open_cleanup_return;
                }
            }
        }
        else {
            if (O_TRUNC_CHECK(flags)) {
                if ((mb = get_mbucket(fpath))) {
                    dbindex = mb->first_dbucket;
                    db = get_dbucket_ptr(dbindex);
                    db->bytes = 0;
                    dbindex = db->next_ix;
                    while (dbindex != -1) {
                        db = get_dbucket_ptr(dbindex);
                        dbindex = db->next_ix;
#ifndef O_TRUNC_FREE
                        db->bytes = 0;
#else
                        release_dbucket(db);
#endif
                    }
#ifdef O_TRUNC_FREE
                    dbindex = mb->first_dbucket;
                    db = get_dbucket_ptr(dbindex);
                    db->next_ix = -1;
#endif
                    mb->s.st_size = 0;
                    mb->s.st_blocks = 1;
                    stamp(&mb->s, ST_MOD | ST_CHG);
                    MB_MODIFY(mb);
                }
                else {
                    mb = create_new_file(fpath, flags, mode);
                    if (!mb) {
                        ret = -1;
                        err = errno;
                        goto open_cleanup_return;
                    }
                }
            }
            else if ((mb = get_mbucket(fpath))) {
                if (flags & O_EXCL) {
                    ret = -1;
                    err = EEXIST;
                    goto open_cleanup_return;
                }
            }
            else {
                struct stat s;
                if (!real_stat(pathname, &s)) {
                    if (flags & O_EXCL){
                        ret = -1;
                        err = EEXIST;
                        goto open_cleanup_return;
                    }
                    else {
                        mb = upload_file(fpath);
                        if (!mb) {
                            ret = -1;
                            err = errno;
                            goto open_cleanup_return;
                        }
                    }
                }
                else {
                    mb = create_new_file(fpath, flags, mode);
                    if (!mb) {
                        ret = -1;
                        err = errno;
                        goto open_cleanup_return;
                    }
                }
            }
        }
        if (flags & O_APPEND){
            dbindex = mb->first_dbucket;
            while (1) {
                db = get_dbucket_ptr(dbindex);
                if (db->next_ix >= 0) {
                    dbindex = db->next_ix;
                }
                else {
                    mb->last_dbucket = dbindex;
                    mb->last_db_offset = db->bytes;
                }
            }
        }
        else {
            mb->last_dbucket = mb->first_dbucket;
            mb->last_db_offset = 0;
        }
        fd = get_empty_fd(FIRST_USER_FD, MAX_NUM_FD);
        if (fd < 0) {
            ret = -1;
            err = EMFILE;
            goto open_cleanup_return;
        }
        FDDES(fd).mindex = MB_IX(mb);
        ret = fd;

open_cleanup_return:
        FREE_ABS_PATH_1(fpath, pathname);
    }


open_return:
    dlog(1, "[ufs: open] path: %s, flags: %d, mode: %3o, ret: %d\n", pathname, flags, mode & 0777, ret);
    errno = err;
    return ret;
}

static inline int timespec_comp(struct timespec a, struct timespec b)
{
    if (a.tv_sec == b.tv_sec)
        return a.tv_nsec > b.tv_nsec;
    else
        return a.tv_sec > b.tv_sec;
}

#define STAMP_CHECK(t1, t2, a, b) ((t1 & t2) && timespec_comp(a, b))

/* For exclusive fds, real_close is used.
 * For both exclusive and internal fds, fdDescriptor will be reset for reuse. 
 * */
int close(int fd)
{
    int ret = -1;
    int err = 0;

    if (FDDES(fd).exclusive) {
        ret = real_close(fd);
        err = errno;
        if (!ret) {
            reset_fd(fd);
        }
    }
    else if (FDDES(fd).empty) {
        ret = -1;
        err = EBADF;
    }
    else {
#ifdef DEFER_TIMESTAMP_CLOSE
        meta_bucket *mb;
        mb = MB_PTR(FDDES(fd).mindex);
        if (STAMP_CHECK(FDDES(fd).stamp_type, ST_ACS, FDDES(fd).priv_s.st_atim, mb->s.st_atim)) {
            mb->s.st_atim = FDDES(fd).priv_s.st_atim;
        }
        if (STAMP_CHECK(FDDES(fd).stamp_type, ST_MOD, FDDES(fd).priv_s.st_mtim, mb->s.st_mtim)) {
            mb->s.st_mtim = FDDES(fd).priv_s.st_mtim;
        }
        if (STAMP_CHECK(FDDES(fd).stamp_type, ST_CHG, FDDES(fd).priv_s.st_ctim, mb->s.st_ctim)) {
            mb->s.st_ctim = FDDES(fd).priv_s.st_ctim;
        }
#endif
        reset_fd(fd);
        ret = 0;
    }

    dlog(1, "[ufs: close] fd: %d, ret: %d\n", fd, ret);
    errno = err;
    return ret;
}

/* For external fd, real_read is used.
 * read uses mb's last_dbucket and last_db_offset to skip the offset translation overhead.
 * read has option to defer timestamp update to close to avoid cache invalid overhead.
 */
ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t ret = -1;
    int err = 0;

    if (FDDES(fd).exclusive) {
        ret = real_read(fd, buf, count);
        err = errno;
    }
    else if (FDDES(fd).empty) {
        ret = -1;
        err = EBADF;
    }
    else {
        meta_bucket *mb;
        data_bucket *db;
        ssize_t n;
        size_t c, off;
        unsigned int dbindex;
        mb = MB_PTR(FDDES(fd).mindex);
        dbindex = mb->last_dbucket;
        off = mb->last_db_offset;
        c = count;
        n = 0;
        while (1) {
            db = get_dbucket_ptr(dbindex);
            if (c <= db->bytes - off) {
                memcpy(buf + n, db->buf + off, c);
                n += c;
                off += c;
                break;
            }
            else {
                memcpy(buf + n, db->buf + off, db->bytes - off);
                n += db->bytes - off;
                c -= db->bytes - off;
                off = db->bytes;
                if (db->next_ix != -1) {
                    dbindex = db->next_ix;
                    off = 0;
                }
                else {
                    break;
                }
            }
        }
        if (n > 0) {
            mb->last_dbucket = dbindex;
            mb->last_db_offset = off;
#ifndef DEFER_TIMESTAMP_CLOSE
            stamp(&mb->s, ST_ACS);
#else
            stamp(&FDDES(fd).priv_s, ST_ACS);
            FDDES(fd).stamp_type |= ST_ACS;
#endif
        }
        ret = n;
    }

    dlog(0, "[ufs: read] fd: %d, count: %zd, ret: %zu\n", fd, count, ret);
    errno = err;
    return ret;

}

/* For external fd, real_write is used.
 * write uses meta bucket's last_dbucket and last_db_offset to skip the offset translation overhead.
 * write has option to defer timestamp update to close.
 */
ssize_t write(int fd, const void *buf, size_t count)
{
    ssize_t ret = -1;
    int err = 0;

    if (FDDES(fd).exclusive) {
        ret = real_write(fd, buf, count);
        err = errno;
    }
    else if (FDDES(fd).empty) {
        ret = -1;
        err = EBADF;
    }
    else {
        meta_bucket *mb;
        data_bucket *db;
        ssize_t n;
        size_t c, off, size_ind;
        unsigned int dbindex;
        mb = MB_PTR(FDDES(fd).mindex);
        c = count;
        dbindex = mb->last_dbucket;
        off = mb->last_db_offset;
        n = 0;
        size_ind = 0;
        while (1) {
            db = get_dbucket_ptr(dbindex);
            if (c <= DBUCKET_BUF_SIZE - off) {
                memcpy(db->buf + off, buf + n, c);
                if (off + c > db->bytes){
                    size_ind += off + c - db->bytes;
                    db->bytes = off + c;
                }
                n += c;
                off += c;
                break;
            }
            else {
                memcpy(db->buf + off, buf + n, DBUCKET_BUF_SIZE - off);
                if (DBUCKET_BUF_SIZE > db->bytes) {
                    size_ind += DBUCKET_BUF_SIZE - db->bytes;
                    db->bytes = DBUCKET_BUF_SIZE;
                }
                n += DBUCKET_BUF_SIZE - off;
                c -= DBUCKET_BUF_SIZE - off;
                dbindex = db->next_ix;
                if (dbindex == -1) {
                    dbindex = get_free_dbucket_ix();
                    db->next_ix = dbindex;
                }
                off = 0;
            }
        }
        if (n > 0) {
            mb->last_dbucket = dbindex;
            mb->last_db_offset = off;
#ifndef DEFER_TIMESTAMP_CLOSE
            stamp(&mb->s, ST_MOD | ST_CHG);
#else
            stamp(&FDDES(fd).priv_s, ST_MOD | ST_CHG);
            FDDES(fd).stamp_type |= ST_MOD | ST_CHG;
#endif
            MB_MODIFY(mb);
        }
        if (size_ind > 0) {
            size_ind += mb->s.st_size;
            mb->s.st_size = size_ind;
            mb->s.st_blocks = round_up_division(size_ind, DBUCKET_BUF_SIZE);
        }
        ret = n;
    }

    dlog(1, "[ufs: write] fd: %d, count: %zd, ret: %zu\n", fd, count, ret);
    errno = err;
    return ret;
}

/* For exclusive fd, real_pread is used.
 * pread can update the timestamp immediately or defer the update to close to avoid the overhead.  
 */
ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    ssize_t ret = -1;
    int err = 0;

    if (FDDES(fd).exclusive) {
        ret = real_pread(fd, buf, count, offset);
        err = errno;
    }
    else if (FDDES(fd).empty) {
        ret = -1;
        err = EBADF;
    }
    else {
        meta_bucket *mb;
        data_bucket *db;
        ssize_t n;
        int rounds, i;
        unsigned int dbindex = -1;
        mb = MB_PTR(FDDES(fd).mindex);
#ifdef OFFSET_PROCESS_CACHE
        for (i = 0; i < OFFSET_PROCESS_CACHE_SIZE; i++) {
            if (FDDES(fd).off_db_mapping[i].offset == offset) {
                dbindex = FDDES(fd).off_db_mapping[i].dbindex;
            }
        }
#endif
        if (dbindex == -1) {
            dbindex = mb->first_dbucket;
            rounds = offset / DBUCKET_BUF_SIZE;
            for (i = 0; i < rounds; i++) {
                if (dbindex == -1) {
                    ret = 0;
                    goto pread_return;
                }
                db = get_dbucket_ptr(dbindex);
                dbindex = db->next_ix;
            }
#ifdef OFFSET_PROCESS_CACHE
            FDDES(fd).off_db_mapping[FDDES(fd).off_db_ptr].offset = offset;
            FDDES(fd).off_db_mapping[FDDES(fd).off_db_ptr].dbindex = dbindex;
            FDDES(fd).off_db_ptr = (FDDES(fd).off_db_ptr + 1) % OFFSET_PROCESS_CACHE_SIZE;
#endif
        }
        size_t c, off;
        off = offset % DBUCKET_BUF_SIZE;
        c = count;
        n = 0;
        while (dbindex != -1) {
            db = get_dbucket_ptr(dbindex);
            if (c <= db->bytes - off) {
                memcpy(buf + n, db->buf + off, c);
                n += c;
                break;
            }
            else {
                memcpy(buf + n, db->buf + off, db->bytes - off);
                n += db->bytes - off;
                c -= db->bytes - off;
            }
            dbindex = db->next_ix;
            off = 0;
        }
        if (n > 0) {
#ifndef DEFER_TIMESTAMP_CLOSE
            stamp(&mb->s, ST_ACS);
#else
            stamp(&FDDES(fd).priv_s, ST_ACS);
            FDDES(fd).stamp_type |= ST_ACS;
#endif
        }
        ret = n;
    }

pread_return:
    dlog(0, "[ufs: pread] fd: %d, count: %zd, offset: %zu, ret: %zu\n", fd, count, offset, ret);
    errno = err;
    return ret;
}

/* For exclusive fd, pwrite use real_pwrite.
 * pwrite can update the timestamp immediately or defer the update to close to avoid the overhead.
 */
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    ssize_t ret = -1;
    int err = 0;

    if (FDDES(fd).exclusive) {
        ret = real_pwrite(fd, buf, count, offset);
        err = errno;
    }
    else if (FDDES(fd).empty) {
        ret = -1;
        err = EBADF;
    }
    else {
        meta_bucket *mb;
        data_bucket *db;
        ssize_t n;
        int rounds, i;
        unsigned int dbindex = -1;
        mb = MB_PTR(FDDES(fd).mindex);
#ifdef OFFSET_PROCESS_CACHE
        for (i = 0; i < OFFSET_PROCESS_CACHE_SIZE; i++) {
            if (FDDES(fd).off_db_mapping[i].offset == offset) {
                dbindex = FDDES(fd).off_db_mapping[i].dbindex;
            }
        }
#endif
        if (dbindex == -1) {
            dbindex = mb->first_dbucket;
            rounds = offset / DBUCKET_BUF_SIZE;
            for (i = 0; i < rounds; i++) {
                if (dbindex == -1) {
                    ret = 0;
                    goto pwrite_return;
                }
                db = get_dbucket_ptr(dbindex);
                dbindex = db->next_ix;
            }
#ifdef OFFSET_PROCESS_CACHE
            FDDES(fd).off_db_mapping[FDDES(fd).off_db_ptr].offset = offset;
            FDDES(fd).off_db_mapping[FDDES(fd).off_db_ptr].dbindex = dbindex;
            FDDES(fd).off_db_ptr = (FDDES(fd).off_db_ptr + 1) % OFFSET_PROCESS_CACHE_SIZE;
#endif
        }
        size_t c, off, size_ind;
        off = offset % DBUCKET_BUF_SIZE;
        c = count;
        n = 0;
        size_ind = 0;
        while (dbindex != -1) {
            db = get_dbucket_ptr(dbindex);
            if (c <= DBUCKET_BUF_SIZE - off) {
                memcpy(db->buf + off, buf + n, c);
                if (off + c > db->bytes){
                    size_ind += off + c - db->bytes;
                    db->bytes = off + c;
                }
                n += c;
                break;
            }
            else {
                memcpy(db->buf + off, buf + n, DBUCKET_BUF_SIZE - off);
                if (DBUCKET_BUF_SIZE > db->bytes) {
                    size_ind += DBUCKET_BUF_SIZE - db->bytes;
                    db->bytes = DBUCKET_BUF_SIZE;
                }
                n += DBUCKET_BUF_SIZE - off;
                c -= DBUCKET_BUF_SIZE - off;
                dbindex = db->next_ix;
                if (dbindex == -1) {
                    dbindex = get_free_dbucket_ix();
                    db->next_ix = dbindex;
                }
                off = 0;
            }
        }
        if (n > 0) {
#ifndef DEFER_TIMESTAMP_CLOSE
            stamp(&mb->s, ST_MOD | ST_CHG);
#else
            stamp(&FDDES(fd).priv_s, ST_MOD | ST_CHG);
            FDDES(fd).stamp_type |= ST_MOD | ST_CHG;
#endif
            MB_MODIFY(mb);
        }
        if (size_ind > 0) {
            size_ind += mb->s.st_size;
            mb->s.st_size = size_ind;
            mb->s.st_blocks = round_up_division(size_ind, DBUCKET_BUF_SIZE);
        }
        ret = n;
    }

pwrite_return:
    dlog(0, "[ufs: pwrite] fd: %d, count: %zd, offset: %zu, ret: %zu\n", fd, count, offset, ret);
    errno = err;
    return ret;
}

/* unlink looks for the meta bucket and data bucket associated with pathname, and mark them for reuse.
 * If enable UNLINK_IGNORE_DISK, unlink will not remove the file on disk, instead only removing the file in ufs memory.
 * UNLINK_IGNORE_DISK can improve the performance when you know, you are not going to unlink anything from disk.
 * Disable UNLINK_IGNORE_DISK, only when you want to unlink your source files. Notice all middle file are only in ufs memory.
 */
int unlink(const char *pathname)
{
    if (calling_shm_unlink) {
        return real_unlink(pathname);
    }

    int ret = -1;
    int err = 0;
    const char *fpath;

    if (strlen(pathname) > PATH_MAX) {
        ret = -1;
        err = ENAMETOOLONG;
        goto unlink_return;
    }
    BUILD_ABS_PATH_1(fpath, pathname);
    if (!release_meta_bucket(fpath)) {
#ifndef UNLINK_IGNORE_DISK
        real_unlink(pathname);
#endif
        ret = 0;
        err = 0;
    }
    else {
        ret = -1;
        err = ENOENT;
#ifndef UNLINK_IGNORE_DISK
        ret = real_unlink(pathname);
        err = errno;
#endif
    }
    FREE_ABS_PATH_1(fpath, pathname);

unlink_return:
    dlog(0, "[ufs: unlink] path: %s, ret: %d\n", pathname, ret);
    errno = err;
    return ret;
}

/* rename has 16 cases, oldpath on disk or not, oldpath in ufs memory or not, newpath on disk or not, newpath in ufs memory or not.
 * rename is simplified in the following way:
 * If oldpath not in ufs memory, real_rename handle that. And if newpath exists in ufs memory, unlink that only from ufs memory (not supported yet).
 * If oldpath in ufs memory, and if newpath also in ufs memory, overwrite newpath, and release its data bucket (not supported yet).
 * If oldpath in ufs memory, and if newpath not in ufs memory, chrow will change the path in oldpath's meta bucket and move the bucket to the corrent hash linked list.   
 */
int rename(const char *oldpath, const char *newpath)
{
    int ret = -1;
    int err = 0;

    if (strcmp(oldpath, newpath) == 0) {
        ret = 0;
        goto rename_return;
    }
    if (strlen(oldpath) > PATH_MAX || strlen(newpath) > PATH_MAX) {
        ret = -1;
        err = ENAMETOOLONG;
        goto rename_return;
    }

    const char *foldpath, *fnewpath;
    char *tmp1, *tmp2;
    if (!build_abs_path(&tmp1, oldpath)) {
        foldpath = tmp1;
    }
    else {
        foldpath = oldpath;
    }
    if (!build_abs_path(&tmp2, newpath)) {
        fnewpath = tmp2;
    }
    else {
        fnewpath = newpath;
    }
    ret = mb_chpath(foldpath, fnewpath);
    if (ret < 0) {
        ret = real_rename(oldpath, newpath);
        err = errno;
        if (!ret && get_mbucket(fnewpath)) {
            UEHANDLE("unsupported rename: oldpath on disk, newpath in ufs memory.");
        }
    }
    else {
        meta_bucket *mb;
        mb = get_mbucket(fnewpath);
        MB_MODIFY(mb);
    }
    if (foldpath != oldpath) {
        free(tmp1);
    }
    if (fnewpath != newpath) {
        free(tmp2);
    }

rename_return:
    dlog(0, "[ufs: rename] oldpath: %s, newpath: %s, ret: %d\n", oldpath, newpath, ret);
    errno = err;
    return ret;
}

/* fsync is an empty function in ufs.
 * We keep everhting in memory, and only flush to disk when daemon is closed.
 */
int fsync(int fd)
{
    return 0;
}

