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
    int fd = open(PATH, O_RDWR);
    printf("fd: %d\n", fd);
    write(fd, "123", 3);
    char c;
    lseek(fd, 2, SEEK_SET);
    read(fd, &c, 1);
    printf("%c\n",c);
    lseek(fd, 1, SEEK_SET);
    read(fd, &c, 1);
    printf("%c\n", c);
    lseek(fd, 0, SEEK_SET);
    read(fd, &c, 1);
    printf("%c\n", c);
    close(fd);
}
