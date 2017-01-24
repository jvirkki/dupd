/*
  Copyright 2012-2017 Jyri J. Virkki <jyri@virkki.com>

  This file is part of dupd.

  dupd is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  dupd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with dupd.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __APPLE__
#include <openssl/md5.h>
#include <openssl/sha.h>
#endif

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#define MD5_CTX CC_MD5_CTX
#define MD5_Init CC_MD5_Init
#define MD5_Update CC_MD5_Update
#define MD5_Final CC_MD5_Final
#define SHA_CTX CC_SHA1_CTX
#define SHA_Init CC_SHA1_Init
#define SHA_Update CC_SHA1_Update
#define SHA_Final CC_SHA1_Final
#define SHA512_CTX CC_SHA512_CTX
#define SHA512_Init CC_SHA512_Init
#define SHA512_Update CC_SHA512_Update
#define SHA512_Final CC_SHA512_Final
#endif

#include "main.h"
#include "hash.h"
#include "stats.h"

#define MAX_BLOCK (1024 * 256)

static char buffer[MAX_BLOCK];


/** ***************************************************************************
 * MD5 implementation for hash_fn().
 *
 */
static int md5(char * output, uint64_t blocks, int bsize, int file)
{
  uint64_t counter = blocks;
  ssize_t bytes;
  MD5_CTX ctx;

  MD5_Init(&ctx);
  while ((bytes = read(file, buffer, bsize)) > 0) {
    stats_total_bytes_hashed += bytes;
    stats_total_bytes_read += bytes;
    MD5_Update(&ctx, buffer, bytes);
    if (blocks) {
      counter--;
      if (counter == 0) { break; }
    }
  }

  close(file);
  MD5_Final((unsigned char *)output, &ctx);

  return(0);
}


/** ***************************************************************************
 * MD5 implementation for hash_fn_buf().
 *
 */
static int md5_buf(const char * buffer, int bufsize, char * output)
{
  MD5_CTX ctx;

  MD5_Init(&ctx);
  MD5_Update(&ctx, buffer, bufsize);
  MD5_Final((unsigned char *)output, &ctx);
  stats_total_bytes_hashed += bufsize;
  return(0);
}


/** ***************************************************************************
 * SHA1 implementation for hash_fn().
 *
 */
static int sha1(char * output, uint64_t blocks, int bsize, int file)
{
  uint64_t counter = blocks;
  ssize_t bytes;
  SHA_CTX ctx;

  SHA1_Init(&ctx);
  while ((bytes = read(file, buffer, bsize)) > 0) {
    stats_total_bytes_hashed += bytes;
    stats_total_bytes_read += bytes;
    SHA1_Update(&ctx, buffer, bytes);
    if (blocks) {
      counter--;
      if (counter == 0) { break; }
    }
  }

  close(file);
  SHA1_Final((unsigned char *)output, &ctx);

  return(0);
}


/** ***************************************************************************
 * SHA1 implementation for hash_fn_buf().
 *
 */
static int sha1_buf(const char * buffer, int bufsize, char * output)
{
  SHA_CTX ctx;

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, buffer, bufsize);
  SHA1_Final((unsigned char *)output, &ctx);
  stats_total_bytes_hashed += bufsize;
  return(0);
}


/** ***************************************************************************
 * SHA512 implementation for hash_fn().
 *
 */
static int sha512(char * output, uint64_t blocks, int bsize, int file)
{
  uint64_t counter = blocks;
  ssize_t bytes;
  SHA512_CTX ctx;

  SHA512_Init(&ctx);
  while ((bytes = read(file, buffer, bsize)) > 0) {
    stats_total_bytes_hashed += bytes;
    stats_total_bytes_read += bytes;
    SHA512_Update(&ctx, buffer, bytes);
    if (blocks) {
      counter--;
      if (counter == 0) { break; }
    }
  }

  close(file);
  SHA512_Final((unsigned char *)output, &ctx);

  return(0);
}


/** ***************************************************************************
 * SHA512 implementation for hash_fn_buf().
 *
 */
static int sha512_buf(const char * buffer, int bufsize, char * output)
{
  SHA512_CTX ctx;

  SHA512_Init(&ctx);
  SHA512_Update(&ctx, buffer, bufsize);
  SHA512_Final((unsigned char *)output, &ctx);
  stats_total_bytes_hashed += bufsize;
  return(0);
}


/** ***************************************************************************
 * Public function, see hash.h
 *
 */
int hash_get_bufsize(int hash_function)
{
  switch(hash_function) {

  case HASH_FN_MD5:
    return 16;

  case HASH_FN_SHA1:
    return 20;

  case HASH_FN_SHA512:
    return 64;

  default:                                                   // LCOV_EXCL_START
    printf("error: invalid hash_function value %d\n", hash_function);
    exit(1);                                                 // LCOV_EXCL_STOP
  }
}


/** ***************************************************************************
 * Public function, see hash.h
 *
 */
int hash_fn(const char * path, char * output, uint64_t blocks,
            int bsize, uint64_t skip)
{
  int block_size = bsize;

  if (bsize > MAX_BLOCK) {                                   // LCOV_EXCL_START
    printf("error: hash requested block size too big. max is %d\n", MAX_BLOCK);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  // When reading the entire file, use max block size to reduce read calls
  if (blocks == 0) {
    block_size = MAX_BLOCK;
  }

  if (verbosity >= 8) {
    printf("hash_fn: blocks(%d)=%" PRIu64 " skip=%" PRIu64 " path=%s\n",
           block_size, blocks, skip, path);
  }

  int file = open(path, O_RDONLY);
  if (file < 0) {
    if (verbosity >= 1) {
      printf("HASH: Error opening [%s]\n", path);
    }
    return(-1);
  }

  if (skip > 0) {
    lseek(file, skip * bsize, SEEK_SET);
  }

  switch(hash_function) {

  case HASH_FN_MD5:
    return md5(output, blocks, block_size, file);

  case HASH_FN_SHA1:
    return sha1(output, blocks, block_size, file);

  case HASH_FN_SHA512:
    return sha512(output, blocks, block_size, file);

  default:                                                   // LCOV_EXCL_START
    printf("error: invalid hash_function value %d\n", hash_function);
    exit(1);                                                 // LCOV_EXCL_STOP
  }
}


/** ***************************************************************************
 * Public function, see hash.h
 *
 */
int hash_fn_buf(const char * buffer, int bufsize, char * output)
{
  switch(hash_function) {

  case HASH_FN_MD5:
    return md5_buf(buffer, bufsize, output);

  case HASH_FN_SHA1:
    return sha1_buf(buffer, bufsize, output);

  case HASH_FN_SHA512:
    return sha512_buf(buffer, bufsize, output);

  default:                                                   // LCOV_EXCL_START
    printf("error: invalid hash_function value %d\n", hash_function);
    exit(1);                                                 // LCOV_EXCL_STOP
  }
}


/** ***************************************************************************
 * Public function, see hash.h
 *
 */
void * hash_fn_buf_init()
{
  switch(hash_function) {

  case HASH_FN_MD5: {
    MD5_CTX * ctx = (MD5_CTX *)malloc(sizeof(MD5_CTX));
    MD5_Init(ctx);
    return (void *)ctx;
  }

  case HASH_FN_SHA1: {
    SHA_CTX * ctx = (SHA_CTX *)malloc(sizeof(SHA_CTX));
    SHA1_Init(ctx);
    return (void *)ctx;
  }

  case HASH_FN_SHA512: {
    SHA512_CTX * ctx = (SHA512_CTX *)malloc(sizeof(SHA512_CTX));
    SHA512_Init(ctx);
    return (void *)ctx;
  }

  default:                                                   // LCOV_EXCL_START
    printf("error: invalid hash_function value %d\n", hash_function);
    exit(1);                                                 // LCOV_EXCL_STOP
  }

  return 0;
}


/** ***************************************************************************
 * Public function, see hash.h
 *
 */
int hash_fn_buf_update(void * ctx, const char * buffer, int bufsize)
{
  switch(hash_function) {

  case HASH_FN_MD5:
    MD5_Update((MD5_CTX *)ctx, buffer, bufsize);
    break;

  case HASH_FN_SHA1:
    SHA1_Update((SHA_CTX *)ctx, buffer, bufsize);
    break;

  case HASH_FN_SHA512:
    SHA512_Update((SHA512_CTX *)ctx, buffer, bufsize);
    break;

  default:                                                   // LCOV_EXCL_START
    printf("error: invalid hash_function value %d\n", hash_function);
    exit(1);                                                 // LCOV_EXCL_STOP
  }

  return 0;
}


/** ***************************************************************************
 * Public function, see hash.h
 *
 */
int hash_fn_buf_final(void * ctx, const char * buffer, int bufsize,
                      char * output)
{
  switch(hash_function) {

  case HASH_FN_MD5:
    MD5_Update((MD5_CTX *)ctx, buffer, bufsize);
    MD5_Final((unsigned char *)output, (MD5_CTX *)ctx);
    break;

  case HASH_FN_SHA1:
    SHA1_Update((SHA_CTX *)ctx, buffer, bufsize);
    SHA1_Final((unsigned char *)output, (SHA_CTX *)ctx);
    break;

  case HASH_FN_SHA512:
    SHA512_Update((SHA512_CTX *)ctx, buffer, bufsize);
    SHA512_Final((unsigned char *)output, (SHA512_CTX *)ctx);
    break;

  default:                                                   // LCOV_EXCL_START
    printf("error: invalid hash_function value %d\n", hash_function);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  return 0;
}
