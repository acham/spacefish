#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#define FILETMP "file_tmp_z"
int main() {
    int fd = open(FILETMP, O_CREAT|O_TRUNC|O_RDWR, 0644);
    close(fd);
    struct stat s;
    stat(FILETMP, &s);
    unlink(FILETMP);
    stat(FILETMP, &s);

    return 0;
}
