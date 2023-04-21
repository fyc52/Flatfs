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
#include <stddef.h>      /* size_t */
#include <stdint.h>      /* uint64_t */
#define XXH_INLINE_ALL   /* XXH128_hash_t */
#include "./xxHash/xxhash.h"
#include "./cityhash_c/city.h"
#endif

#define file_num 6000000
#define MIN_FILE_BUCKET_BITS 18
#define TEST_MAX_FILE_NUM   /* test max file nums while the first bucket over 8 slots */
#define HASH_BUCKET_OUT     /* output every bucket slots to hash_text_out.txt */
#define HASH_LINEAR_TEST    /* test file linear */

uint32_t
murmurhash (const char *key, uint32_t len, uint32_t seed) {
    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;
    uint32_t r1 = 15;
    uint32_t r2 = 13;
    uint32_t m = 5;
    uint32_t n = 0xe6546b64;
    uint32_t h = 0;
    uint32_t k = 0;
    uint8_t *d = (uint8_t *) key; // 32 bit extract from `key'
    const uint32_t *chunks = NULL;
    const uint8_t *tail = NULL; // tail - last 8 bytes
    int i = 0;
    int l = len / 4; // chunk length

    h = seed;

    chunks = (const uint32_t *) (d + l * 4); // body
    tail = (const uint8_t *) (d + l * 4); // last 8 byte chunk of `key'

    // for each 4 byte chunk of `key'
    for (i = -l; i != 0; ++i) {
        // next 4 byte chunk of `key'
        k = chunks[i];

        // encode next 4 byte chunk of `key'
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;

        // append to hash
        h ^= k;
        h = (h << r2) | (h >> (32 - r2));
        h = h * m + n;
    }

    k = 0;

    // remainder
    switch (len & 3) { // `len % 4'
        case 3: k ^= (tail[2] << 16);
        case 2: k ^= (tail[1] << 8);

        case 1:
        k ^= tail[0];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        h ^= k;
    }

    h ^= len;

    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    h *= 0xc2b2ae35;
    h ^= (h >> 16);

    return h;
}

struct HashTable {
    int valid_slot_count[1 << MIN_FILE_BUCKET_BITS];
}ht;

int slot_num[1024];

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
uint32_t
fnv_32a_buf(void *buf, size_t len, uint32_t hval)
{
    unsigned char *bp = (unsigned char *)buf;	/* start of buffer */
    unsigned char *be = bp + len;		/* beyond end of buffer */

    /*
     * FNV-1a hash each octet in the buffer
     */
    while (bp < be) {

	/* xor the bottom with the current octet */
	hval ^= (uint32_t)*bp++;

	/* multiply by the 32 bit FNV magic prime mod 2^32 */
#if defined(NO_FNV_GCC_OPTIMIZATION)
	hval *= FNV_32_PRIME;
#else
	hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) + (hval<<24);
#endif
    }

    /* return our new hash value */
    return hval;
}

long long caculate_linear_times(int len)
{
    long long res = 0;
    for (long long i = 1; i <= len; i ++) {
        res += i;
    }
    return res;
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
        sprintf(filename ,"%08d", i);
        /* xxhash */
        //XXH64_hash_t hashcode =  XXH64(filename, 21, 131);

        /* murmurhash */
        //unsigned long hashcode = murmurhash (filename, 21, 131);

        /* FNV */
        //unsigned long hashcode =  fnv_32a_buf(filename, 21, 131);

        /* BKDRHash */
        unsigned long hashcode = BKDRHash(filename);

        /* CityHash */
        //unsigned long hashcode = CityHash64(filename, 21);
	    unsigned long bucket_id = (unsigned long)hashcode & ((1LU << (MIN_FILE_BUCKET_BITS)) - 1LU);
        ht.valid_slot_count[bucket_id] ++;

#ifdef TEST_MAX_FILE_NUM
        if ( ht.valid_slot_count[bucket_id] > 8) {
            printf("Max file num:%d\n", i);
            break;
        }
#endif
    }

    for(int i = 0; i < (1 << MIN_FILE_BUCKET_BITS); i ++)
    {
        char buf[100];
        //if(ht.valid_slot_count[i] == 0) continue;
        slot_num[ht.valid_slot_count[i]] ++;
        if(ht.valid_slot_count[i] > 3) over_3slot_bucket_num ++;
        if(ht.valid_slot_count[i] > 7) full_bucket_num ++;

#ifdef HASH_BUCKET_OUT    
        sprintf(buf, "%d %d\n", i , ht.valid_slot_count[i]);
        write(fd, buf, strlen(buf));
#endif
    }

    for(int i = 0; i < 1024; i ++)
    {
        if(slot_num[i] > 0) printf("slot num %d, total num %d\n", i, slot_num[i]);
    }

#ifdef HASH_LINEAR_TEST
    int over8_tt_num = 0, file_linear_num = 0;
    long long tt_linear_times = 0;
    for(int i = 9; i < 100; i ++)
    {
        over8_tt_num += slot_num[i];
        file_linear_num += slot_num[i] * (i - 8);
        tt_linear_times += caculate_linear_times(i - 8) * slot_num[i];
    }
    printf("over8_tt_num:%d, file_linear_num:%d, tt_linear_times:%lld\n", over8_tt_num, file_linear_num, tt_linear_times);
    printf("over_3slot_bucket: %d, full_bucket_num: %d\n", over_3slot_bucket_num, full_bucket_num);
#endif
    close(fd);
    return 0;
}