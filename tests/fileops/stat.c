#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(){
    struct stat st;
    if (!stat("./file_a", &st)){
        printf ("st_dev    : 0x%x\n",  st.st_dev);
        printf ("st_ino    : %lu\n",  st.st_ino);
        printf ("st_mode   : 0%o\n",  st.st_mode);
        printf ("st_nlink  : %d\n",  st.st_nlink);
        printf ("st_uid    : %d\n",  st.st_uid);
        printf ("st_gid    : %d\n",  st.st_gid);
        printf ("st_rdev   : 0x%x\n",  st.st_rdev);
        printf ("st_size   : %ld\n",  st.st_size);
        printf ("st_blksize: %ld\n",  st.st_blksize);
        printf ("st_blocks : %ld\n",  st.st_blocks);
        printf ("st_ctime : %ld\n",  st.st_ctime);
        printf ("st_mtime : %ld\n",  st.st_mtime);
        printf ("st_atime : %ld\n",  st.st_atime);
    }
    else{
        printf("stat fails.\n");
    }
		
    return 0;
}
