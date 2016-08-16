#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define PATH "file_tmp2"

int main()
{
    struct stat st;
    int fd = open(PATH, O_RDWR|O_CREAT, 0666);
    if (fd < 0) {
        fprintf(stderr, "open failed, exiting\n");
        return -1;
    }
    printf("(open-ret) returned fd: %d\n", fd);
    close(fd);
    fd = open(PATH, O_RDWR, 0666);
    if (fd < 0) {
        fprintf(stderr, "open failed, exiting\n");
        return -1;
    }
    printf("(open-ret #2) returned fd: %d\n", fd);
    //close(fd);
    //return 0;
    
    char* buf = "222";
    ssize_t c = write(fd, (void *)buf, strlen(buf));
    printf("(write-ret) wrote: %s, expected: %zu, real: %zd\n",
           buf, strlen(buf), c);
    lseek(fd, 0, SEEK_SET);
    void* dest = malloc(10);
    c = read(fd, dest, 10);
    printf("(read-ret) content read: %s\n", (char *)dest);
    close(fd);
    if (stat(PATH, &st) == -1) printf("stat failed\n");
    return 0;
    
}
