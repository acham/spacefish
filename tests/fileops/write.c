#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define PATH "./file_a"
#define LOOP 2000

int main()
{
    struct stat st;

    int fd = open(PATH, O_CREAT|O_RDWR|O_TRUNC, 0644);
    char* buf = "4567";
    int i;
    ssize_t c;

    for (i = 0; i < LOOP; i++) {
        c = write(fd, (void *)buf, strlen(buf));
        if (c != strlen(buf)) {
            printf("warning: stop early!\n");
            break;
        }
    }

    close(fd);
}
