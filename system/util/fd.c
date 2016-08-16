#include "fd.h"

#include <config.h>
#include <dlog.h>

void init_fdTable()
{
    int i, j;

    /** do init **/
    for (i = 0; i < 3; i++) {
        fdTable[i].mindex = -1;
        fdTable[i].exclusive = 1;
        fdTable[i].empty = 0;
        fdTable[i].stamp_type = 0;
        for (j = 0; j < OFFSET_PROCESS_CACHE_SIZE; j++) {
        fdTable[i].off_db_mapping[j].offset = -1;
        }

    }
    for (; i < FIRST_USER_FD; i++) {
        fdTable[i].mindex = -1;
        fdTable[i].exclusive = 1;
        fdTable[i].empty = 1;
        fdTable[i].stamp_type = 0;
        for (j = 0; j < OFFSET_PROCESS_CACHE_SIZE; j++) {
        fdTable[i].off_db_mapping[j].offset = -1;
        }

    }
    for (i = FIRST_USER_FD; i < MAX_NUM_FD; i++) {
        fdTable[i].mindex = -1;
        fdTable[i].flags = 0;
        fdTable[i].exclusive = 0;
        fdTable[i].empty = 1;
        fdTable[i].stamp_type = 0;
        for (j = 0; j < OFFSET_PROCESS_CACHE_SIZE; j++) {
        fdTable[i].off_db_mapping[j].offset = -1;
        }

    }
}

int get_empty_fd(int from, int to)
{
    int i;
    for (i = from; i <= to; i++) {
        if (fdTable[i].empty) {
            fdTable[i].empty = 0;
            return i;
        }
    }
    return -1;
}

void reset_fd(int fd) {
    int i;
    fdTable[fd].mindex = -1;
    fdTable[fd].flags = 0;
    fdTable[fd].stamp_type = 0;
    fdTable[fd].exclusive = 0;
    fdTable[fd].empty = 1;
    for (i = 0; i < OFFSET_PROCESS_CACHE_SIZE; i++) {
        fdTable[fd].off_db_mapping[i].offset = -1;
    }
}
