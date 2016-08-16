#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlog.h>

#include <glibccall.h>
#include <utility.h>
#include <fd.h>
#include <config.h>
#include <meta_shm.h>

#include <string.h>
#include <errno.h>

extern int errno;
/* This file currently only contains three stat function, __xstat, __lxstat, __fxstat */

/* __xstat is a wrapper for stat.
 * It first checks whether path existes in ufs, if not it then checks whether path existes on disk.
 * real_stat is used to check if file existes on disk.
 * */
int __xstat(int ver, const char *path, struct stat *buf)
{
    const char *fpath;
    meta_bucket *mb;
    int ret = -1;
    int err = 0;

    if (strlen(path) > PATH_MAX) {
        err = ENAMETOOLONG;
        ret = -1;
        goto xstat_return;
    }
    BUILD_ABS_PATH_1(fpath, path);
    mb = get_mbucket(fpath);
    if (mb) {
        memcpy(buf, &mb->s, sizeof(struct stat));
        ret = 0;
    }
    else {
        ret = real_stat(path, buf);
        err = errno;
    }
    FREE_ABS_PATH_1(fpath, path);

xstat_return:
    dlog(1, "[ufs: __xstat] path: %s, ret: %d\n", path, ret);
    errno = err;
    return ret;
}

/*  __lxstat is a wrapper for lstat.
 * ufs currently not supports symbolic link, so it works totally same as __xstat.
 * Except that it uses real_lstat to check if path does not exist in ufs.
 * */
int __lxstat(int ver, const char *path, struct stat *buf)
{
    const char *fpath;
    meta_bucket *mb;
    int ret = -1;
    int err = 0;

    if (strlen(path) > PATH_MAX) {
        err = ENAMETOOLONG;
        ret = -1;
        goto lstat_return;
    }
    BUILD_ABS_PATH_1(fpath, path);
    mb = get_mbucket(fpath);
    if (mb) {
        memcpy(buf, &mb->s, sizeof(struct stat));
        ret = 0;
    }
    else {
        ret = real_lstat(path, buf);
        err = errno;
    }
    FREE_ABS_PATH_1(fpath, path);

lstat_return:
    dlog(0, "[ufs: __lxstat] path: %s, ret: %d\n", path, ret);
    errno = err;
    return ret;
}

/* __fxstat is a wrapper for fstat.
 * For exclusive fds, it calls real_fstat to handle.
 * For internal fd, it looks for meta bucket and copy the stat to buf.
 * */
int __fxstat(int ver, int fd, struct stat *buf)
{
    meta_bucket *mb;
    int ret = -1;
    int err = 0;

    if (FDDES(fd).exclusive) {
        ret = real_fstat(fd, buf);
        err = errno;
    }
    else if (FDDES(fd).empty) {
        ret = -1;
        err = EBADF;
    }
    else {
        mb = MB_PTR(FDDES(fd).mindex);
        if (!mb->in_use) {
            ret = -1;
            err = EFAULT;
        }
        else {
            memcpy(buf, &mb->s, sizeof(struct stat));
            ret = 0;
        }
    }

    dlog(0, "[ufs: __fxstat] fd: %d, ret: %d\n", fd, ret);
    errno = err;
    return ret;
}
