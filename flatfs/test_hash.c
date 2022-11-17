/* Test hash crash*/
#include <stdio.h>   
#include <sys/types.h>   
#include <sys/stat.h>   
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifndef _TEST_H_
#define _TEST_H_
#include "hash.h"
#endif

#define file_num 24000 + 10
#define MIN_FILE_BUCKET_BITS 12


struct HashTable {
    int valid_slot_count[1 << MIN_FILE_BUCKET_BITS];
}ht;

unsigned int BKDRHash(char *str)
{
    unsigned int seed = 131;
    unsigned int hash = 0;
 
    while (*str)
    {
        hash = hash * seed + (*str++);
    }
 
    return (hash & 0x7FFFFFFF);
}

int main()   
{   
    int fd;
    fd = open("./hash_text_out.txt",O_RDWR);
    for(int i = 0; i < file_num; i ++)
    {
        char filename[100];
        sprintf(filename ,"%d", i);
        unsigned int hashcode = BKDRHash(filename);
	    unsigned long bucket_id = (unsigned long)hashcode & ((1LU << (MIN_FILE_BUCKET_BITS)) - 1LU);
        ht.valid_slot_count[bucket_id] ++;
    }

    for(int i = 0; i < (1 << MIN_FILE_BUCKET_BITS); i ++)
    {
        char buf[100];
        sprintf(buf, "%d %d\n", i , ht.valid_slot_count[i]);
        write(fd, buf, strlen(buf));
    }

    close(fd);
    return 0;
}