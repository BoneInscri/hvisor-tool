#ifndef _SAFE_SERVICE_H_
#define _SAFE_SERVICE_H_

// NPUCore Safe Service IDs
#define NPUCore_SERVICE_ECHO_ID 1 /* 回显 for test only */
#define NPUCore_SERVICE_FLIP_ID 2 /* 翻转 for test only */

// 1. encryption and  decryption
#define NPUCore_SERVICE_CAESAR_ENCRYPT_ID 3
#define NPUCore_SERVICE_CAESAR_DECRYPT_ID 4
#define NPUCore_SERVICE_XOR_ENCRYPT_ID 5
#define NPUCore_SERVICE_XOR_DECRYPT_ID 6
#define NPUCore_SERVICE_ROT13_ENCRYPT_ID 7
#define NPUCore_SERVICE_ROT13_DECRYPT_ID 8

// 2. hash function
#define NPUCore_SERVICE_DJB2_HASH_ID 9
#define NPUCore_SERVICE_CRC32_HASH_ID 10
#define NPUCore_SERVICE_FNV_HASH_ID 11

// 3. random generator
#define NPUCore_SERVICE_XORSHIFT_RAND_NUMBER_ID 12
#define NPUCore_SERVICE_LCG_RAND_NUMBER_ID 13
#define NPUCore_SERVICE_RAND_STRING_ID 14
#define NPUCore_SERVICE_RAND_CHAR_ID 15

#define NPUCore_SERVICE_MAX_ID 16

typedef struct {
  unsigned int id;
  const char *description;
  int need_fetch_data;// need fetch data from shm (modified by NPUCore)
} Service;

#endif // _SAFE_SERVICE_H_