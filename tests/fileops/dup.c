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
    int fd1 = dup(fd);
    printf("newfd(dup): %d\n", fd1);
    int fd2 = dup2(fd, 5);
    printf("newfd(dup2): %d\n",fd2);
    close(fd);
	
    char *buf = malloc(st.st_size);
    ssize_t c = read(fd2, (void *)buf, st.st_size);
    printf("expected: %jd, real: %zd\n", st.st_size, c);
    printf("content: %s\n", buf);
	
    close(fd2);
    free(buf);
    buf = malloc(st.st_size);
    c = read(fd1, (void *)buf, st.st_size);
    printf("expected: %jd, real: %zd\n", st.st_size, c);
    printf("content: %s\n", buf);
    close(fd1);
}
