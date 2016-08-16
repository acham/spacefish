#ifndef UFSDLOG_H
#define UFSDLOG_H

#include <config.h>
#include <glibccall.h>
#include <unistd.h>

#ifndef UFS_DEBUG_SHOW_LOCATION
#define UFS_DEBUG_SHOW_LOCATION -1
#endif

#ifndef UFS_DEBUG_SHOW_PID
#define UFS_DEBUG_SHOW_PID -1
#endif

extern int ufs_init_called;
#define NUMNUM 6

#ifndef UFS_DEBUG
#define dlog(level, ...)
#else
#include <stdio.h>

#define server_dlog(level, ...)                                                \
    if (UFS_DEBUG > level) {                                                   \
        if (UFS_DEBUG_SHOW_PID > 0) {                                          \
            fprintf(stderr, "(%ld)", (long)getpid());                          \
        }                                                                      \
        if (UFS_DEBUG_SHOW_LOCATION > 0) {                                     \
            fprintf(stderr, "(%s:%d)", __FILE__, __LINE__);                    \
        }                                                                      \
        fprintf(stderr, __VA_ARGS__);                                          \
        fflush(stderr);                                                        \
    }

#define client_dlog(level, ...)                                                \
    if (UFS_DEBUG > level) {                                                   \
        if (UFS_DEBUG_SHOW_PID > 0) {                                          \
            real_fprintf(stderr, "(%ld)", (long)getpid());                     \
        }                                                                      \
        if (UFS_DEBUG_SHOW_LOCATION > 0) {                                     \
            real_fprintf(stderr, "(%s:%d)", __FILE__, __LINE__);               \
        }                                                                      \
        real_fprintf(stderr, __VA_ARGS__);                                     \
    }

#define dlog(level, ...)                                                       \
    if (ufs_init_called == 1) {                                                \
        client_dlog(level, __VA_ARGS__);                                       \
    }                                                                          \
    else {                                                                     \
        server_dlog(level, __VA_ARGS__);                                       \
    }

#endif

/* error wrapper for non-UFS system functions */
#include <stdlib.h>
/* error wrapper for non-UFS system functions */
#define server_EHANDLE(fname, retval, errval)                                  \
    if (retval == errval) {                                                    \
        perror(fname);                                                         \
        fprintf(stderr, "(%s:%d)", __FILE__, __LINE__);                        \
        exit(EXIT_FAILURE);                                                    \
    }

#define client_EHANDLE(fname, retval, errval)                                  \
    if (retval == errval) {                                                    \
        perror(fname);                                                         \
        real_fprintf(stderr, "(%s:%d)", __FILE__, __LINE__);                   \
        exit(EXIT_FAILURE);                                                    \
    }

#define EHANDLE(fname, retval, errval)                                         \
    if (ufs_init_called) {                                                     \
        client_EHANDLE(fname, retval, errval);                                 \
    }                                                                          \
    else {                                                                     \
        server_EHANDLE(fname, retval, errval);                                 \
    }

/* error wrapper for UFS lower-level functions, util etc.
 * uses global string uemsg
 */

// char *uemsg;

#define UEHANDLE(...)                                                          \
    real_fprintf(stderr, "(%ld)", (long)getpid());                             \
    real_fprintf(stderr, "[ufs: %s] Error: ", __FUNCTION__);                   \
    real_fprintf(stderr, __VA_ARGS__);                                         \
    real_fprintf(stderr, "(%s:%d)\n", __FILE__, __LINE__);                     \
    exit(EXIT_FAILURE);

#endif
