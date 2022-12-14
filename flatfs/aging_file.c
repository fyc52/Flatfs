#include <stdio.h>   
#include <sys/types.h>   
#include <sys/stat.h>   
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
 
#define file_num 10000000
const char path[16] = "/mnt/bbssd/";
int inode[file_num + 10];

int rand_ino()
{
    
    return rand() % file_num;
}

void my_random_create_small_file()
{
    int fd;
    int ino = rand_ino();
    while(inode[ino]) 
    {
        ino ++;
        ino = ino % file_num;
    }
    char filename[100];
    strcpy(filename, path);
    sprintf(filename + strlen(path), "%09d", ino);
   // printf("%s\n", filename);
    fd = creat(filename, 0x777);
    if(fd < 0)
    {
        return ;
    }
  
    inode[ino] = 1; 
    int i = 0;       
    char buf[100000] = "fycnb!\n";
    //printf("filenme:%s, ino:%d %d\n", filename, ino, inode[ino]);
    while(i ++ < 16000)  
    {
        write(fd, buf, strlen(buf));
    }
    close(fd);
}

void my_random_create_big_file()
{
    int fd;
    int ino = rand_ino();
    while(inode[ino]) 
    {
        ino ++;
        ino = ino % file_num;
    }
    char filename[100];
    strcpy(filename, path);
    sprintf(filename + strlen(path), "%09d", ino);
   // printf("%s\n", filename);
    fd = creat(filename, 0x777);
    if(fd < 0)
    {
        return ;
    }
  
    inode[ino] = 1; 
    int i = 0;       
    char buf[100000] = "hbtqlnb!";
    //printf("filenme:%s, ino:%d %d\n", filename, ino, inode[ino]);
    while(i ++ < 100000000)  
    {
        write(fd, buf, strlen(buf));
    }
    close(fd);
}

void my_random_remove_file()
{
    int ino = rand_ino();
    int fd;
    while(!inode[ino]) 
    {
        ino ++;
        ino %= file_num;
       // printf("%d\n", ino);
    }
    char filename[100];
    strcpy(filename, path);
    sprintf(filename + strlen(path), "%09d", ino);
    //printf("%s\n", filename);
    fd = remove(filename);
    if(fd < 0) return;
    inode[ino] = 0; 
    close(fd);
}
 
int main()   
{   
    srand(time(0));
    int fd;  
    for(int i = 0; i < 100000; i ++)
    {
        my_random_create_small_file();
    }
    for(int i = 0; i < 50000; i ++)
    {
        my_random_remove_file();
    }

    for(int i = 0; i < 450000; i ++)
    {
        my_random_create_small_file();
    }
    for(int i = 0; i < 500000; i ++)
    {
        my_random_remove_file();
    }

    return 0;
}