#ifndef CUCKOO_HASH_H_
#define CUCKOO_HASH_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    uint8_t key[KEY_LEN];
    uint8_t value[VALUE_LEN];
} entry_t;

typedef struct bucket
{
    entry_t slot[CUCKOO_HASH_ASSOC_NUM];
} bucket_t;

typedef struct path_node_t
{
    uint64_t pre_bucket;  // pre bucket id
    uint64_t pre_slot;    // pre slot id
    uint64_t next_bucket; // next bucket id
    uint64_t next_slot;   //next slot id
} path_node_t;

typedef struct cuckoo_hash
{
    bucket_t *table;
    uint64_t capacity;
    uint64_t entry_num;
    uint64_t f_seed, s_seed;
    path_node_t *evict_path1;
    path_node_t *evict_path2;
    uint64_t evict_len;
    uint64_t find_path;
    uint64_t resize_write;
    uint64_t init_write;
} cuckoo_hash_t;

cuckoo_hash_t *cuckoo_hash_init(uint64_t capacity);
status_t cuckoo_insert(cuckoo_hash_t *cuckoo, uint8_t *key, uint8_t *value);
status_t cuckoo_update(cuckoo_hash_t *cuckoo, uint8_t *key, uint8_t *value);
status_t cuckoo_resize(cuckoo_hash_t *cuckoo);
status_t cuckoo_query(cuckoo_hash_t *cuckoo, uint8_t *key, uint8_t *value);

void print_cuckoo(bucket_t *table, uint64_t capacity);
#endif