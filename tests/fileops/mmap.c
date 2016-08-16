#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

int main() {
    int fd = open("file_a", O_RDONLY);
    void *ret = mmap(NULL, 3, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    munmap(ret, 3);
    return 0;
}
