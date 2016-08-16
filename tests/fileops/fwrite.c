#include <stdio.h>
#include <stdlib.h>

int main()
{
    FILE *f;
    char *str;
    f = fopen("./file_tmp","w+");
    printf("fopen-ret\n");
    fwrite("123", sizeof(char), 3, f);
    printf("fwrite ret\n");
    
    fseek(f, 0, SEEK_SET);
    printf("fseek ret\n");
    
    str = malloc(4);
    fread(str, sizeof(char), 3, f);
    printf("fread ret\n");
    str[3] = '\0';
    printf("content: %s\n", str);	
    fclose(f);
    return 0;    
}
