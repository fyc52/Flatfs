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

#define file_num 4000000 + 10
#define MIN_FILE_BUCKET_BITS 21


struct HashTable {
    int valid_slot_count[1 << MIN_FILE_BUCKET_BITS];
}ht;

int slot_num[8];

unsigned int BKDRHash(char *str)
{
    unsigned int seed = 4397;
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
    int over_3slot_bucket_num = 0;
    int full_bucket_num = 0;

    fd = open("./hash_text_out.txt",O_RDWR);
    for(int i = 0; i < file_num; i ++)
    {
        char filename[100];
        sprintf(filename ,"%09d", i);
        unsigned int hashcode = BKDRHash(filename);
	    unsigned long bucket_id = (unsigned long)hashcode & ((1LU << (MIN_FILE_BUCKET_BITS)) - 1LU);
        ht.valid_slot_count[bucket_id] ++;
        if(ht.valid_slot_count[bucket_id] == 8)
            printf("%s\n", filename);
    }
    return 0;
    for(int i = 0; i < (1 << MIN_FILE_BUCKET_BITS); i ++)
    {
        char buf[100];
        //if(ht.valid_slot_count[i] == 0) continue;
        slot_num[ht.valid_slot_count[i]] ++;
        if(ht.valid_slot_count[i] > 3) over_3slot_bucket_num ++;
        if(ht.valid_slot_count[i] > 7) full_bucket_num ++;
        sprintf(buf, "%d %d\n", i , ht.valid_slot_count[i]);
        write(fd, buf, strlen(buf));
    }
    for(int i = 0; i < 8; i ++)
    {
        printf("slot num %d, total num %d\n", i, slot_num[i]);
    }
    printf("over_3slot_bucket: %d, full_bucket_num: %d\n", over_3slot_bucket_num, full_bucket_num);
    close(fd);
    return 0;
}