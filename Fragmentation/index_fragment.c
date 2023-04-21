#include <stdio.h>   
#include <sys/types.h>   
#include <sys/stat.h>   
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
 
#define file_num 4
#define append_size 4096
const int append_times = 800 * 1024;
const char path[32] = "/mnt/bbssd/";
int fd[file_num];
char buf[append_size];
 
void create_file(int ino)
{
    char filename[100];

    strcpy(filename, path);
    sprintf(filename + strlen(path), "f%d", ino);
    printf("%s\n", filename);
    fd[ino] = open(filename, O_WRONLY | O_CREAT | O_APPEND | O_TRUNC, 0777);
}
 
int main()   
{  
    for (int i = 0; i < append_size; i++) 
    { 
        buf[i] = 'a' + i % 26; 
    }
    for (int i = 0; i < file_num; i ++) {
        create_file(i);
    }
    for (int i = 0; i < append_times; i ++) { 
        for (int j = 0; j < file_num; j ++) {
            write(fd[j], buf, append_size);
        }       
        
    }
    
    for (int i = 0; i < file_num; i ++) {
        close(fd[i]);
    }
    return 0;
}