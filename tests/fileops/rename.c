#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#define PATH "file_tmp_rename"
#define PATH2 "file_tmp_rename2"
int main(){
    int fd = open(PATH, O_CREAT | O_RDWR | O_EXCL, 0644);
    if (fd < 0) {
        printf("File already exists.\n");
        return -1;
    }
    close(fd);
    rename(PATH, PATH2);
    return 0;
}
