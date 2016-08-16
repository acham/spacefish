#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


int main()
{
    FILE *f;
    char *str;
    f = fopen("./file_a","r");
    printf("fopen returned\n");
    str = malloc(4);
    fread(str, sizeof(char), 3, f);
    fclose(f);
    str[3] = '\0';
    printf("content: %s\n", str);
    return 0;
}
