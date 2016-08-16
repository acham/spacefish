#include <config.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include <meta_shm.h>

typedef int bool;

/* used by stamp() */
#define ST_ACS 1
#define ST_MOD 2
#define ST_CHG 4

struct cmd_opt {
    char *dir_name;
    bool daemon_mode;
};

void upload_dir(const char *name);
meta_bucket *upload_file(const char *name);
meta_bucket *traverse_next_bucket(meta_bucket *mb);
void write_to_disk(meta_bucket *mb);
void flush_write(void);
int build_abs_path(char **abs_path_t, const char *path_in);
int stamp(struct stat *s, int type);

#define round_up_division(a, b) (a + (b - 1)) / b

#define BUILD_ABS_PATH_1(NAME, INAME); \
    char *SNAME; \
    int BRET = build_abs_path(&SNAME, INAME); \
    switch (BRET) { \
        case 0: \
            NAME = SNAME; \
            break; \
        case 1: \
            NAME = INAME; \
            break; \
        default: \
            NAME = INAME; \
    }

#define FREE_ABS_PATH_1(NAME, INAME); \
    if ((NAME != INAME) && SNAME) { \
        free(SNAME); \
    }

unsigned long hash(const char *str);
