#include <stdio.h>

#define PATH "/proc/filesystems"

int main() {
    FILE * f = fopen(PATH, "r");
    char *line;
    size_t n;
    while ( __getdelim(&line, &n, '\n', f) >= 0 ) {
        printf("%s", line);
    }
    return 0;
}
