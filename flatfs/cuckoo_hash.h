#ifndef CUCKOO_HASH_H_
#define CUCKOO_HASH_H_

//#include </usr/include/stdint.h>
//#include <stdio.h>
//#include <stdlib.h>

#define RESIZE_SAME_OFFSET

#define KEY_LEN 16
#define VALUE_LEN 16
#define INVALID ""
// #define FORK_TEST

#define CUCKOO_HASH_ASSOC_NUM 4
#define RESIZE_FACTOR 2
#define MAX_EVICTION 16 //500
#define PAGE_SIZE 4096
// #define __NR_kvremap 436

typedef enum
{
    SUCCESS,
    FAIL
} status_t;

typedef struct entry
{
    unsigned char key[KEY_LEN];
    unsigned char value[VALUE_LEN];
} entry_t;

typedef struct bucket
{
    entry_t slot[CUCKOO_HASH_ASSOC_NUM];
} bucket_t;

typedef struct path_node_t
{
    unsigned long pre_bucket;  // pre bucket id
    unsigned long pre_slot;    // pre slot id
    unsigned long next_bucket; // next bucket id
    unsigned long next_slot;   //next slot id
} path_node_t;

typedef struct cuckoo_hash
{
    bucket_t *table;
    unsigned long capacity;
    unsigned long entry_num;
    unsigned long f_seed, s_seed;
    path_node_t *evict_path1;
    path_node_t *evict_path2;
    unsigned long evict_len;
    unsigned long find_path;
    unsigned long resize_write;
    unsigned long init_write;
} cuckoo_hash_t;

cuckoo_hash_t *cuckoo_hash_init(unsigned long capacity);
status_t cuckoo_insert(cuckoo_hash_t *cuckoo, unsigned char *key, unsigned char *value);
status_t cuckoo_update(cuckoo_hash_t *cuckoo, unsigned char *key, unsigned char *value);
status_t cuckoo_resize(cuckoo_hash_t *cuckoo);
status_t cuckoo_query(cuckoo_hash_t *cuckoo, unsigned char *key, unsigned char *value);

void print_cuckoo(bucket_t *table, unsigned long capacity);
#endif