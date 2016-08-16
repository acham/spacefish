#ifndef UFS_FDLOG_H
#define UFS_FDLOG_H

#if UFS_DEBUG > 0

#include <stdio.h>
#include <glibccall.h>
#include <unistd.h>

extern FILE* ufs_fdlog_out;

#define UFS_FDLOG_OUT ufs_fdlog_out
/* #define UFS_FDLOG_OUT stderr */

#define fdlog(level, ...) \
        if (UFS_DEBUG > level) { \
            if ( UFS_FDLOG_OUT ) { \
                real_fprintf(UFS_FDLOG_OUT, "(%ld)", (long) getpid()); \
                real_fprintf(UFS_FDLOG_OUT, __VA_ARGS__); \
                real_fflush(UFS_FDLOG_OUT); \
            } \
            else { \
                real_fprintf(stderr, "Warning!!! FILE pointer invalid, use stderr instead\n!"); \
                real_fprintf(stderr, "(%ld)", (long) getpid()); \
                real_fprintf(stderr, __VA_ARGS__); \
            } \
        }
#else
#define fdlog(level, ...)
#endif

#endif
