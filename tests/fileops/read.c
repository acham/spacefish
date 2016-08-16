#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define PATH "./file_a"

int main()
{
    struct stat st;
    if ( stat(PATH, &st) ) {
        printf("stat fails.\n");
        return 0;
    }
	
    int fd = open(PATH, O_RDONLY);
    printf("fd: %d\n", fd);
    char *buf = malloc(st.st_size);
    ssize_t c = read(fd, (void *)buf, st.st_size);
    printf("excepted: %jd, real: %zd\n", st.st_size, c);
    printf("content: %s\n", buf);
	
    close(fd);
}
