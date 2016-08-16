#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <dlfcn.h>

#define dlfun(func)                                                            \
    NULL;                                                                      \
    real_##func##_f = dlsym(RTLD_NEXT, #func)

static inline int real_fprintf(FILE *stream, const char *format, ...)
{
    va_list ap;
    char *strp;

    va_start(ap, format);
    vasprintf(&strp, format, ap);
    va_end(ap);

    static int (*real_fprintf_f)(FILE *, const char *, ...) = dlfun(fprintf);
    //    static int (*real_fprintf_f)(FILE *, const char *, ...) = NULL;
    //    real_fprintf_f = dlsym(RTLD_NEXT, fprintf);
    assert(real_fprintf_f);
    return real_fprintf_f(stream, strp);
}

static inline int real_stat(const char *path, struct stat *buf)
{
    static int (*real___xstat_f)(int, const char *, struct stat *) =
        dlfun(__xstat);
    assert(real___xstat_f);
    return real___xstat_f(1, path, buf);
}

static inline int real_lstat(const char *path, struct stat *buf)
{
    static int (*real___lxstat_f)(int, const char *, struct stat *) =
        dlfun(__lxstat);
    assert(real___lxstat_f);
    return real___lxstat_f(1, path, buf);
}

static inline int real_fstat(int fd, struct stat *buf)
{
    static int (*real___fxstat_f)(int, int, struct stat *) = dlfun(__fxstat);
    assert(real___fxstat_f);
    return real___fxstat_f(1, fd, buf);
}

static inline int real_open(const char *path, int flags, ...)
{
    static int (*real_open_f)(const char *, int, mode_t) = dlfun(open);
    assert(real_open_f);

    va_list ap;
    va_start(ap, flags);
    mode_t arg = va_arg(ap, mode_t);
    va_end(ap);

    return real_open_f(path, flags, arg);
}

static inline FILE *real_fopen(const char *path, const char *mode)
{
    static FILE *(*real_fopen_f)(const char *, const char *) = dlfun(fopen);
    assert(real_fopen_f);
    return real_fopen_f(path, mode);
}

static inline int real_creat(const char *path, mode_t mode)
{
    static int (*real_creat_f)(const char *, mode_t) = dlfun(creat);
    assert(real_creat_f);
    return real_creat_f(path, mode);
}

static inline ssize_t real_read(int fd, void *buf, size_t count)
{
    static ssize_t (*real_read_f)(int, void *, size_t) = dlfun(read);
    assert(real_read_f);
    return real_read_f(fd, buf, count);
}

static inline ssize_t real_write(int fd, const void *buf, size_t count)
{
    static ssize_t (*real_write_f)(int, const void *, size_t) = dlfun(write);
    assert(real_write_f);
    return real_write_f(fd, buf, count);
}

static inline int real_close(int fd)
{
    static int (*real_close_f)(int) = dlfun(close);
    assert(real_close_f);
    return real_close_f(fd);
}

static inline off_t real_lseek(int fd, off_t offset, int whence)
{
    static off_t (*real_lseek_f)(int, off_t, int) = dlfun(lseek);
    assert(real_lseek_f);
    return real_lseek_f(fd, offset, whence);
}

static inline void *real_mmap(void *addr, size_t length, int prot, int flags,
                              int fd, off_t offset)
{
    static void *(*real_mmap_f)(void *, size_t, int, int, int, off_t) =
        dlfun(mmap);
    assert(real_mmap_f);
    return real_mmap_f(addr, length, prot, flags, fd, offset);
}

static inline int real_fileno(FILE *stream)
{
    static int (*real_fileno_f)(FILE *) = dlfun(fileno);
    assert(real_fileno_f);
    return real_fileno_f(stream);
}

static inline int real_fflush(FILE *stream)
{
    static int (*real_fflush_f)(FILE *) = dlfun(fflush);
    assert(real_fflush_f);
    return real_fflush_f(stream);
}

static inline int real_access(const char *pathname, int mode)
{
    static int (*real_access_f)(const char *, int) = dlfun(access);
    assert(real_access_f);
    return real_access_f(pathname, mode);
}

static inline int real_fclose(FILE *stream)
{
    static int (*real_fclose_f)(FILE *stream) = dlfun(fclose);
    assert(real_fclose_f);
    return real_fclose_f(stream);
}

static inline int real_unlink(const char *pathname)
{
    static int (*real_unlink_f)(const char *) = dlfun(unlink);
    assert(real_unlink_f);
    return real_unlink_f(pathname);
}

static inline int real_isatty(int fd)
{
    static int (*real_isatty_f)(int) = dlfun(isatty);
    assert(real_isatty_f);
    return real_isatty_f(fd);
}

static inline int real_fcntl(int fd, int cmd, ...)
{
    static int (*real_fcntl_f)(int, int, long) = dlfun(fcntl);
    assert(real_fcntl_f);

    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);

    return real_fcntl_f(fd, cmd, arg);
}

static inline char *real_realpath(const char *path, char *resolved_path)
{
    static char *(*real_realpath_f)(const char *, char *) = dlfun(realpath);
    assert(real_realpath_f);
    return real_realpath_f(path, resolved_path);
}

static inline int real_mkstemps(char *template, int suffixlen)
{
    static int (*real_mkstemps_f)(char *, int) = dlfun(mkstemps);
    assert(real_mkstemps_f);
    return real_mkstemps_f(template, suffixlen);
}

static inline int real_munmap(void *addr, size_t length)
{
    static int (*real_munmap_f)(void *, size_t) = dlfun(munmap);
    assert(real_munmap_f);
    return real_munmap_f(addr, length);
}

static inline pid_t real_fork(void)
{
    static pid_t (*real_fork_f)(void) = dlfun(fork);
    assert(real_fork_f);
    return real_fork_f();
}

static inline pid_t real_vfork(void)
{
    static pid_t (*real_vfork_f)(void) = dlfun(vfork);
    assert(real_vfork_f);
    return real_vfork_f();
}

static inline int real_execvp(const char *file, char *const args[])
{
    static int (*real_execvp_f)(const char *file, char *const args[]) =
        dlfun(execvp);
    assert(real_execvp_f);
    return real_execvp_f(file, args);
}

static inline int real_execv(const char *path, char *const args[])
{
    static int (*real_execv_f)(const char *path, char *const args[]) =
        dlfun(execv);
    assert(real_execv_f);
    return real_execv_f(path, args);
}

static inline int real_execve(const char *filename, char *const args[],
                              char *const _envp[])
{
    static int (*real_execve_f)(const char *filename, char *const args[],
                                char *const _envp[]) = dlfun(execve);
    assert(real_execve_f);
    return real_execve_f(filename, args, _envp);
}

static inline int real_chmod(const char *pathname, mode_t mode)
{
    static int (*real_chmod_f)(const char *filename, mode_t mode) =
        dlfun(chmod);
    assert(real_chmod_f);
    return real_chmod_f(pathname, mode);
}

static inline int real_fchmod(int fd, mode_t mode)
{
    static int (*real_fchmod_f)(int fd, mode_t mode) = dlfun(fchmod);
    assert(real_fchmod_f);
    return real_fchmod_f(fd, mode);
}

static inline int real___chmod(const char *pathname, mode_t mode)
{
    static int (*real___chmod_f)(const char *filename, mode_t mode) =
        dlfun(__chmod);
    assert(real___chmod_f);
    return real___chmod_f(pathname, mode);
}

static inline int real_dup(int oldfd)
{
    static int (*real_dup_f)(int) = dlfun(dup);
    assert(real_dup_f);
    return real_dup_f(oldfd);
}

static inline int real_dup2(int oldfd, int newfd)
{
    static int (*real_dup2_f)(int, int) = dlfun(dup2);
    assert(real_dup2_f);
    return real_dup2_f(oldfd, newfd);
}

static inline int real_dup3(int oldfd, int newfd, int flags)
{
    static int (*real_dup3_f)(int, int, int) = dlfun(dup3);
    assert(real_dup3_f);
    return real_dup3_f(oldfd, newfd, flags);
}

static inline int real_pipe(int pipefd[2])
{
    static int (*real_pipe_f)(int pipefd[2]) = dlfun(pipe);
    assert(real_pipe_f);
    return real_pipe_f(pipefd);
}

static inline int real___overflow(FILE *stream, int c)
{
    static int (*real___overflow_f)(FILE *, int) = dlfun(__overflow);
    assert(real___overflow_f);
    return real___overflow_f(stream, c);
}

static inline long real_ftell(FILE *stream)
{
    static long (*real_ftell_f)(FILE *) = dlfun(ftell);
    assert(real_ftell_f);
    return real_ftell_f(stream);
}

static inline size_t real_fread(void *ptr, size_t size, size_t nmemb,
                                FILE *stream)
{
    static size_t (*real_fread_f)(void *, size_t, size_t, FILE *) =
        dlfun(fread);
    assert(real_fread_f);
    return real_fread_f(ptr, size, nmemb, stream);
}

static inline size_t real_fwrite(const void *ptr, size_t size, size_t nmemb,
                                 FILE *stream)
{
    static size_t (*real_fwrite_f)(const void *, size_t, size_t, FILE *) =
        dlfun(fwrite);
    assert(real_fwrite_f);
    return real_fwrite_f(ptr, size, nmemb, stream);
}

static inline int real_ferror(FILE *stream)
{
    static int (*real_ferror_f)(FILE *) = dlfun(ferror);
    assert(real_ferror_f);
    return real_ferror_f(stream);
}

static inline int real___uflow(FILE *stream)
{
    static size_t (*real___uflow_f)(FILE *) = dlfun(__uflow);
    assert(real___uflow_f);
    return real___uflow_f(stream);
}

static inline int real_rename(const char *oldpath, const char *newpath)
{
    static int (*real_rename_f)(const char *, const char *) = dlfun(rename);
    assert(real_rename_f);
    return real_rename_f(oldpath, newpath);
}

static inline int real_feof(FILE *stream)
{
    static int (*real_feof_f)(FILE *) = dlfun(feof);
    assert(real_feof_f);
    return real_feof_f(stream);
}

static inline void real_clearerr(FILE *stream)
{
    static void (*real_clearerr_f)(FILE *) = dlfun(clearerr);
    assert(real_clearerr_f);
    return real_clearerr_f(stream);
}

static inline ssize_t real_pread(int fd, void *buf, size_t count, off_t offset)
{
    static ssize_t (*real_pread_f)(int, void *, size_t, off_t) = dlfun(pread);
    assert(real_pread_f);
    return real_pread_f(fd, buf, count, offset);
}

static inline ssize_t real_pwrite(int fd, const void *buf, size_t count,
                                  off_t offset)
{
    static ssize_t (*real_pwrite_f)(int, const void *, size_t, off_t) =
        dlfun(pwrite);
    assert(real_pwrite_f);
    return real_pwrite_f(fd, buf, count, offset);
}

static inline int real_ftruncate(int fd, off_t length)
{
    static int (*real_ftruncate_f)(int, off_t) = dlfun(ftruncate);
    assert(real_ftruncate_f);
    return real_ftruncate_f(fd, length);
}

static inline int real_truncate(const char *path, off_t length)
{
    static int (*real_truncate_f)(const char *, off_t) = dlfun(truncate);
    assert(real_truncate_f);
    return real_truncate_f(path, length);
}

static inline int real_printf(const char *format, ...)
{
    va_list ap;
    char *strp;
    int ret;

    va_start(ap, format);
    vasprintf(&strp, format, ap);
    va_end(ap);

    static int (*real_printf_f)(const char *, ...) = dlfun(printf);
    assert(real_printf_f);
    ret = real_printf_f(strp);
    return ret;
}

// more to be added here
