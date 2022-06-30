//#include </usr/include/stdint.h>
// refer to https://github.com/Pfzuo/Level-Hashing/blob/master/level_hashing/
/*
Function: hash() 
        This function is used to compute the hash value of a string key;
        For integer keys, two different and independent hash functions should be used. 
        For example, Jenkins Hash is used for the first hash funciton, and murmur3 hash is used for
        the second hash funciton.
*/
unsigned long hash(const void *data, unsigned long length, unsigned long seed);