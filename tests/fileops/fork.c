#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

int main(void)
{
    int fd;

    fd = open("file_tmp", O_CREAT|O_TRUNC|O_WRONLY, 0666);

    if(!fork()) {
        /* child */
        write(fd, "hello ", 6);
        close(fd);
        _exit(0);
    } else {
        /* parent */
        int status;

        wait(&status);
        write(fd, "world\n", 6);
        close(fd);
    }

   
    return 0;
}
