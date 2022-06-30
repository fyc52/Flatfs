#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "cuckoo_hash.h"
#include "hash.h"

pid_t pid = 1;

// refer to https://github.com/Pfzuo/Level-Hashing/blob/master/level_hashing/level_hashing.c
/*
Function: F_HASH()
        Compute the first hash value of a key-value item
*/
uint64_t F_HASH(cuckoo_hash_t *cuckoo, const uint8_t *key)
{
    return (hash((void *)key, strlen(key), cuckoo->f_seed));
}

/*
Function: S_HASH() 
        Compute the second hash value of a key-value item
*/
uint64_t S_HASH(cuckoo_hash_t *cuckoo, const uint8_t *key)
{
    return (hash((void *)key, strlen(key), cuckoo->s_seed));
}

/*
Function: F_IDX() 
        Compute the second hash location
*/
uint64_t F_IDX(uint64_t hashKey, uint64_t capacity)
{
#ifdef RESIZE_SAME_OFFSET
    return hashKey % capacity;
#else
    return hashKey % (capacity / 2);
#endif
}

/*
Function: S_IDX() 
        Compute the second hash location
*/
uint64_t S_IDX(uint64_t hashKey, uint64_t capacity)
{
#ifdef RESIZE_SAME_OFFSET
    return hashKey % capacity;
#else
    return hashKey % (capacity / 2) + capacity / 2;
#endif
}

void generate_seeds(cuckoo_hash_t *cuckoo)
{
    srand(time(NULL));
    do
    {
        cuckoo->f_seed = rand();
        cuckoo->s_seed = rand();
        cuckoo->f_seed = cuckoo->f_seed << (rand() % 63);
        cuckoo->s_seed = cuckoo->s_seed << (rand() % 63);
    } while (cuckoo->f_seed == cuckoo->s_seed);
}

cuckoo_hash_t *cuckoo_hash_init(uint64_t capacity)
{
    cuckoo_hash_t *cuckoo = NULL;
    uint64_t table_len = sizeof(bucket_t) * capacity;

    if (table_len < PAGE_SIZE || table_len % PAGE_SIZE != 0)
    {
        printf("cuckoo table size error, not page aligned\n");
        exit(EXIT_FAILURE);
    }

    cuckoo = (cuckoo_hash_t *)kzalloc(sizeof(cuckoo_hash_t),GFP_KERNEL);
    if (!cuckoo)
    {
        printf("cuckoo alloc fails\n");
        exit(EXIT_FAILURE);
    }
    // else
    // {
    //     memset(cuckoo, 0, sizeof(cuckoo_hash_t));
    // }

    cuckoo->evict_path1 = (path_node_t *)kzalloc(MAX_EVICTION * sizeof(path_node_t),GFP_KERNEL);
    cuckoo->evict_path2 = (path_node_t *)kzalloc(MAX_EVICTION * sizeof(path_node_t),GFP_KERNEL);
    if (!cuckoo->evict_path1 || !cuckoo->evict_path2)
    {
        printf("paths alloc fails\n");
        exit(EXIT_FAILURE);
    }

    cuckoo->table = (bucket_t *)kzalloc(table_len, GFP_KERNEL);
    if (!cuckoo->table)
    {
        printf("cuckoo table alloc fails\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        memset(cuckoo->table, 0, sizeof(bucket_t) * capacity);
    }

    cuckoo->capacity = capacity;
    cuckoo->entry_num = 0;
    cuckoo->find_path = 0;
    cuckoo->evict_len = 0;
    cuckoo->resize_write = 0;
    cuckoo->init_write = capacity * CUCKOO_HASH_ASSOC_NUM;

    generate_seeds(cuckoo);

    return cuckoo;
}

uint64_t find_path(cuckoo_hash_t *cuckoo, uint64_t bucket, uint64_t slot, int path)
{
    path_node_t *paths = (path == 1 ? cuckoo->evict_path1 : cuckoo->evict_path2);
    uint64_t cur_bucket = bucket;
    uint64_t cur_slot = slot;
    uint64_t used_node_number = 0;
    int valid = 0;
    path_node_t tmpnode;
    uint64_t i;

    while (used_node_number < MAX_EVICTION)
    {
        uint8_t *key = cuckoo->table[cur_bucket].slot[cur_slot].key;
        uint64_t f_hash = F_HASH(cuckoo, key);
        uint64_t s_hash = S_HASH(cuckoo, key);
        uint64_t f_idx = F_IDX(f_hash, cuckoo->capacity);
        uint64_t s_idx = S_IDX(s_hash, cuckoo->capacity);
        uint64_t evict_bucket;

        cuckoo->find_path++;

        if (f_idx == cur_bucket)
        {
            evict_bucket = s_idx;
        }
        else if (s_idx == cur_bucket)
        {
            evict_bucket = f_idx;
        }
        else
        {
            printf("find path error\n");
            exit(EXIT_FAILURE);
        }

        tmpnode.pre_bucket = cur_bucket;
        tmpnode.next_bucket = evict_bucket;

        for (i = 0; i < CUCKOO_HASH_ASSOC_NUM; i++)
        {
            if (strcmp((const char *)cuckoo->table[evict_bucket].slot[i].key, INVALID) == 0)
            {
                tmpnode.pre_slot = cur_slot;
                tmpnode.next_slot = i;
                paths[used_node_number++] = tmpnode;
                return used_node_number;
            }
        }

        tmpnode.pre_slot = cur_slot;
        tmpnode.next_slot = CUCKOO_HASH_ASSOC_NUM - 1;
        paths[used_node_number++] = tmpnode;
        cur_bucket = evict_bucket;
        cur_slot = CUCKOO_HASH_ASSOC_NUM - 1;
    }

    return 0;
}

void execute_path(cuckoo_hash_t *cuckoo, path_node_t *paths, uint64_t path_len)
{
    uint64_t pre_bucket, next_bucket;
    uint64_t pre_slot, next_slot;
    int64_t i;

    for (i = path_len - 1; i >= 0; --i)
    {
        pre_bucket = paths[i].pre_bucket;
        pre_slot = paths[i].pre_slot;
        next_bucket = paths[i].next_bucket;
        next_slot = paths[i].next_slot;
        memcpy(cuckoo->table[next_bucket].slot[next_slot].value, cuckoo->table[pre_bucket].slot[pre_slot].value, VALUE_LEN);
        memcpy(cuckoo->table[next_bucket].slot[next_slot].key, cuckoo->table[pre_bucket].slot[pre_slot].key, KEY_LEN);
    }
}

status_t cuckoo_insert(cuckoo_hash_t *cuckoo, uint8_t *key, uint8_t *value)
{
    uint64_t f_hash = F_HASH(cuckoo, key);
    uint64_t s_hash = S_HASH(cuckoo, key);
    uint64_t f_idx = F_IDX(f_hash, cuckoo->capacity);
    uint64_t s_idx = S_IDX(s_hash, cuckoo->capacity);
    path_node_t *path;
    uint64_t path_len, path_len1, path_len2;
    uint64_t tmp_idx;
    uint64_t i;

    for (i = 0; i < CUCKOO_HASH_ASSOC_NUM; i++)
    {
        if (strcmp((const char *)cuckoo->table[f_idx].slot[i].key, INVALID) == 0)
        {
            memcpy(cuckoo->table[f_idx].slot[i].value, value, VALUE_LEN);
            memcpy(cuckoo->table[f_idx].slot[i].key, key, KEY_LEN);
            cuckoo->entry_num++;
            return SUCCESS;
        }

        if (strcmp((const char *)cuckoo->table[s_idx].slot[i].key, INVALID) == 0)
        {
            memcpy(cuckoo->table[s_idx].slot[i].value, value, VALUE_LEN);
            memcpy(cuckoo->table[s_idx].slot[i].key, key, KEY_LEN);
            cuckoo->entry_num++;
            return SUCCESS;
        }
    }

    // evict
    for (i = 0; i < CUCKOO_HASH_ASSOC_NUM; i++)
    {
        path_len1 = find_path(cuckoo, f_idx, i, 1);
        path_len2 = find_path(cuckoo, s_idx, i, 2);
        if (path_len1 == 0 && path_len2 == 0)
        {
            continue;
        }
        else
        {
            if (path_len1 == 0)
            {
                path = cuckoo->evict_path2;
                path_len = path_len2;
                tmp_idx = s_idx;
            }
            else if (path_len2 == 0)
            {
                path = cuckoo->evict_path1;
                path_len = path_len1;
                tmp_idx = f_idx;
            }
            else if (path_len1 < path_len2)
            {
                path = cuckoo->evict_path1;
                path_len = path_len1;
                tmp_idx = f_idx;
            }
            else
            {
                path = cuckoo->evict_path2;
                path_len = path_len2;
                tmp_idx = s_idx;
            }
            execute_path(cuckoo, path, path_len);
            cuckoo->evict_len += path_len;

            memcpy(cuckoo->table[tmp_idx].slot[i].value, value, VALUE_LEN);
            memcpy(cuckoo->table[tmp_idx].slot[i].key, key, KEY_LEN);
            cuckoo->entry_num++;
            return SUCCESS;
        }
    }

    return FAIL;
}

status_t cuckoo_update(cuckoo_hash_t *cuckoo, uint8_t *key, uint8_t *value)
{
    uint64_t f_hash = F_HASH(cuckoo, key);
    uint64_t s_hash = S_HASH(cuckoo, key);
    uint64_t f_idx = F_IDX(f_hash, cuckoo->capacity);
    uint64_t s_idx = S_IDX(s_hash, cuckoo->capacity);
    uint64_t i;

    for (i = 0; i < CUCKOO_HASH_ASSOC_NUM; i++)
    {
        if (strcmp((const char *)cuckoo->table[f_idx].slot[i].key, (const char *)key) == 0)
        {
            memcpy(cuckoo->table[f_idx].slot[i].value, value, VALUE_LEN);

            return SUCCESS;
        }

        if (strcmp((const char *)cuckoo->table[s_idx].slot[i].key, (const char *)key) == 0)
        {
            memcpy(cuckoo->table[s_idx].slot[i].value, value, VALUE_LEN);

            return SUCCESS;
        }
    }
    return FAIL;
}

status_t cuckoo_query(cuckoo_hash_t *cuckoo, uint8_t *key, uint8_t *value)
{
    uint64_t f_hash = F_HASH(cuckoo, key);
    uint64_t s_hash = S_HASH(cuckoo, key);
    uint64_t f_idx = F_IDX(f_hash, cuckoo->capacity);
    uint64_t s_idx = S_IDX(s_hash, cuckoo->capacity);
    uint64_t i;

    for (i = 0; i < CUCKOO_HASH_ASSOC_NUM; i++)
    {
        if (strcmp((const char *)cuckoo->table[f_idx].slot[i].key, (const char *)key) == 0)
        {
            memcpy(value, cuckoo->table[f_idx].slot[i].value, VALUE_LEN);
            return SUCCESS;
        }

        if (strcmp((const char *)cuckoo->table[s_idx].slot[i].key, (const char *)key) == 0)
        {
            memcpy(value, cuckoo->table[s_idx].slot[i].value, VALUE_LEN);
            return SUCCESS;
        }
    }
    return FAIL;
}

status_t cuckoo_resize(cuckoo_hash_t *cuckoo)
{
    bucket_t *old_table = cuckoo->table, *new_table = NULL;
    uint64_t old_table_capacity = cuckoo->capacity;
    uint64_t new_table_capacity = cuckoo->capacity * RESIZE_FACTOR;
    uint64_t cur_idx;
    uint64_t f_hash, s_hash, f_idx, s_idx;
    int64_t f_off = 0, s_off = 0, rehash = 0;
    uint64_t new_table_len = new_table_capacity * sizeof(bucket_t);
    uint8_t *key, *value;
    uint64_t i, j, k;

    new_table = (bucket_t *)kzalloc(new_table_capacity * sizeof(bucket_t), GFP_KERNEL);
    if (!new_table)
    {
        printf("resize alloc fails\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        // syscall(__NR_kvremap, (uint64_t)new_table, new_table_len, 0, getpid());
        memset(new_table, 0, new_table_len);
        cuckoo->init_write += new_table_capacity * CUCKOO_HASH_ASSOC_NUM;
    }

    // printf("old_table = %p, table tail = %#lx, new_table = %p, new_table tail = %#lx\n", old_table, (uint64_t)old_table + old_table_capacity * sizeof(bucket_t), new_table, (uint64_t)new_table + new_table_capacity * sizeof(bucket_t));

#ifdef FORK_TEST
    pid = fork();
    if(pid < 0) printf("fork error\n");
#endif

    for (cur_idx = 0; cur_idx < old_table_capacity; cur_idx++)
    {
        for (j = 0; j < CUCKOO_HASH_ASSOC_NUM; j++)
        {
            if (strcmp((const char *)old_table[cur_idx].slot[j].key, INVALID) != 0)
            {
                key = old_table[cur_idx].slot[j].key;
                value = old_table[cur_idx].slot[j].value;

                f_hash = F_HASH(cuckoo, key);
                s_hash = S_HASH(cuckoo, key);
                f_idx = F_IDX(f_hash, new_table_capacity);
                s_idx = S_IDX(s_hash, new_table_capacity);

                f_off = F_IDX(f_hash, old_table_capacity);
                s_off = S_IDX(s_hash, old_table_capacity);

                rehash = 0;

#ifdef RESIZE_SAME_OFFSET
                if (f_off == s_off)
                {
                    for (k = 0; k < CUCKOO_HASH_ASSOC_NUM; k++)
                    {
                        if (strcmp((const char *)new_table[f_idx].slot[k].key, INVALID) == 0)
                        {
                            memcpy(new_table[f_idx].slot[k].value, value, VALUE_LEN);
                            memcpy(new_table[f_idx].slot[k].key, key, KEY_LEN);
                            rehash = 1;
                            break;
                        }
                        else if (strcmp((const char *)new_table[s_idx].slot[k].key, INVALID) == 0)
                        {
                            memcpy(new_table[s_idx].slot[k].value, value, VALUE_LEN);
                            memcpy(new_table[s_idx].slot[k].key, key, KEY_LEN);
                            rehash = 1;
                            break;
                        }
                    }

                    if (!rehash)
                    {
                        printf("resize fail1 cur_idx = %lu, slot = %lu, f_idx = %lu, s_idx = %lu\n", cur_idx, j, f_idx, s_idx);
                        print_cuckoo(old_table, old_table_capacity);
                        print_cuckoo(new_table, new_table_capacity);
                        exit(EXIT_FAILURE);
                    }
                }
                else if (f_off == cur_idx)
                {
                    for (k = 0; k < CUCKOO_HASH_ASSOC_NUM; k++)
                    {
                        if (strcmp((const char *)new_table[f_idx].slot[k].key, INVALID) == 0)
                        {
                            memcpy(new_table[f_idx].slot[k].value, value, VALUE_LEN);
                            memcpy(new_table[f_idx].slot[k].key, key, KEY_LEN);
                            rehash = 1;
                            break;
                        }
                    }

                    if (!rehash)
                    {
                        printf("resize fail2 cur_idx = %lu, slot = %lu, f_idx = %lu, s_idx = %lu\n", cur_idx, j, f_idx, s_idx);
                        print_cuckoo(old_table, old_table_capacity);
                        print_cuckoo(new_table, new_table_capacity);
                        exit(EXIT_FAILURE);
                    }
                }
                else if (s_off == cur_idx)
                {
                    for (k = 0; k < CUCKOO_HASH_ASSOC_NUM; k++)
                    {
                        if (strcmp((const char *)new_table[s_idx].slot[k].key, INVALID) == 0)
                        {
                            memcpy(new_table[s_idx].slot[k].value, value, VALUE_LEN);
                            memcpy(new_table[s_idx].slot[k].key, key, KEY_LEN);
                            rehash = 1;
                            break;
                        }
                    }

                    if (!rehash)
                    {
                        printf("resize fail3 cur_idx = %lu, slot = %lu, f_idx = %lu, s_idx = %lu\n", cur_idx, j, f_idx, s_idx);
                        print_cuckoo(old_table, old_table_capacity);
                        print_cuckoo(new_table, new_table_capacity);
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    printf("offset error! cur_idx = %lu, f_idx = %lu, s_idx = %lu\n", cur_idx, f_idx, s_idx);
                    exit(EXIT_FAILURE);
                }
#else
                for (uint64_t k = 0; k < CUCKOO_HASH_ASSOC_NUM; k++)
                {
                    if (strcmp((const char *)new_table[f_idx].slot[k].key, INVALID) == 0)
                    {
                        memcpy(new_table[f_idx].slot[k].value, value, VALUE_LEN);
                        memcpy(new_table[f_idx].slot[k].key, key, KEY_LEN);
                        rehash = 1;
                        break;
                    }
                    else if (strcmp((const char *)new_table[s_idx].slot[k].key, INVALID) == 0)
                    {
                        memcpy(new_table[s_idx].slot[k].value, value, VALUE_LEN);
                        memcpy(new_table[s_idx].slot[k].key, key, KEY_LEN);
                        rehash = 1;
                        break;
                    }
                }

                if (!rehash)
                {
                    printf("resize fail cur_idx = %lu, slot = %lu, f_idx = %lu, s_idx = %lu\n", cur_idx, j, f_idx, s_idx);
                    print_cuckoo(old_table, old_table_capacity);
                    print_cuckoo(new_table, new_table_capacity);
                    exit(EXIT_FAILURE);
                }
#endif
            }
        }
    }

    // print_cuckoo(old_table, old_table_capacity);
    // print_cuckoo(new_table, new_table_capacity);

    cuckoo->capacity = new_table_capacity;
    cuckoo->table = new_table;
    cuckoo->resize_write += cuckoo->entry_num;

    kfree(old_table);
    old_table = NULL;

    return SUCCESS;
}

void print_cuckoo(bucket_t *table, uint64_t capacity)
{
    uint64_t i = 0, j = 0;

    for (i = 0; i < capacity; i++)
    {
        for (j = 0; j < CUCKOO_HASH_ASSOC_NUM; j++)
        {
            printf("(%lu-%lu-%s-%s), ", i, j, table[i].slot[j].key, table[i].slot[j].value);
        }
    }
    printf("\n");
}