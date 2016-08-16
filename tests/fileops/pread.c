#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PATH "./file_b"

int main()
{
    int fd = open(PATH, O_RDONLY);
    char *buf = malloc(20);
    int i;
    for (i = 0; i < 9; i++) {
        ssize_t c = pread(fd, (void *)buf, 5, i);
        buf[c] = '\0';
        printf("%s\n", buf);
    }
    free(buf);
    close(fd);
}
