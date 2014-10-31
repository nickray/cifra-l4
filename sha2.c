#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "sha2.h"
#include "bitops.h"
#include "../bignum/handy.h"

static const uint32_t K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline uint32_t CH(uint32_t x, uint32_t y, uint32_t z)
{
  return (x & y) ^ (~x & z);
}

static inline uint32_t MAJ(uint32_t x, uint32_t y, uint32_t z)
{
  return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t BSIG0(uint32_t x)
{
  return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}

static inline uint32_t BSIG1(uint32_t x)
{
  return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}

static inline uint32_t SSIG0(uint32_t x)
{
  return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}

static inline uint32_t SSIG1(uint32_t x)
{
  return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

void cf_sha256_init(cf_sha256_context *ctx)
{
  memset(ctx, 0, sizeof *ctx);
  ctx->H[0] = 0x6a09e667;
  ctx->H[1] = 0xbb67ae85;
  ctx->H[2] = 0x3c6ef372;
  ctx->H[3] = 0xa54ff53a;
  ctx->H[4] = 0x510e527f;
  ctx->H[5] = 0x9b05688c;
  ctx->H[6] = 0x1f83d9ab;
  ctx->H[7] = 0x5be0cd19;
}

void cf_sha224_init(cf_sha256_context *ctx)
{
  memset(ctx, 0, sizeof *ctx);
  ctx->H[0] = 0xc1059ed8;
  ctx->H[1] = 0x367cd507;
  ctx->H[2] = 0x3070dd17;
  ctx->H[3] = 0xf70e5939;
  ctx->H[4] = 0xffc00b31;
  ctx->H[5] = 0x68581511;
  ctx->H[6] = 0x64f98fa7;
  ctx->H[7] = 0xbefa4fa4;
}

static void sha256_update_block(void *vctx, const uint8_t *inp)
{
  cf_sha256_context *ctx = vctx;

  uint32_t W[64];

  for (size_t t = 0; t < 16; t++)
  {
    W[t] = read32_be(inp);
    inp += 4;
  }

  for (size_t t = 16; t < 64; t++)
  {
    W[t] = SSIG1(W[t - 2]) + W[t - 7] + SSIG0(W[t - 15]) + W[t - 16];
  }

  uint32_t a = ctx->H[0],
           b = ctx->H[1],
           c = ctx->H[2],
           d = ctx->H[3],
           e = ctx->H[4],
           f = ctx->H[5],
           g = ctx->H[6],
           h = ctx->H[7];

  for (size_t t = 0; t < 64; t++)
  {
    uint32_t T1 = h + BSIG1(e) + CH(e, f, g) + K[t] + W[t];
    uint32_t T2 = BSIG0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + T1;
    d = c;
    c = b;
    b = a;
    a = T1 + T2;
  }

  ctx->H[0] += a;
  ctx->H[1] += b;
  ctx->H[2] += c;
  ctx->H[3] += d;
  ctx->H[4] += e;
  ctx->H[5] += f;
  ctx->H[6] += g;
  ctx->H[7] += h;
}

static void accumulate(uint8_t *partial, uint8_t *npartial, size_t nblock,
                       const void *inp, size_t nbytes,
                       void (*process)(void *vctx, const uint8_t *data), void *ctx)
{
  const uint8_t *bufin = inp;
  assert(partial && *npartial < nblock);
  assert(inp && nbytes);
  assert(process && ctx);

  /* If we have partial data, copy in to buffer. */
  if (*npartial)
  {
    size_t space = nblock - *npartial;
    size_t taken = MIN(space, nbytes);

    memcpy(partial + *npartial, bufin, taken);

    bufin += taken;
    nbytes -= taken;
    *npartial += taken;

    /* If that gives us a full block, process it. */
    if (*npartial == nblock)
    {
      process(ctx, partial);
      *npartial = 0;
    }
  }

  /* now nbytes < nblock or *npartial == 0. */

  /* If we have a full block of data, process it directly. */
  while (nbytes >= nblock)
  {
    /* Partial buffer must be empty, or we're ignoring extant data */
    assert(*npartial == 0);

    process(ctx, bufin);
    bufin += nblock;
    nbytes -= nblock;
  }

  /* Finally, if we have remaining data, buffer it. */
  while (nbytes)
  {
    size_t space = nblock - *npartial;
    size_t taken = MIN(space, nbytes);

    memcpy(partial + *npartial, bufin, taken);

    bufin += taken;
    nbytes -= taken;
    *npartial += taken;

    if (*npartial == nblock)
    {
      process(ctx, partial);
      *npartial = 0;
    }
  }
}

void cf_sha256_update(cf_sha256_context *ctx, const void *data, size_t nbytes)
{
  accumulate(ctx->partial, &ctx->npartial, sizeof ctx->partial,
             data, nbytes,
             sha256_update_block, ctx);
}

void cf_sha224_update(cf_sha256_context *ctx, const void *data, size_t nbytes)
{
  cf_sha256_update(ctx, data, nbytes);
}

void cf_sha256_final(const cf_sha256_context *ctx, uint8_t hash[CF_SHA256_HASHSZ])
{
  /* We copy the context, so the finalisation doesn't effect the caller's
   * context.  This means the caller can do:
   *
   * x = init()
   * x.update('hello')
   * h1 = x.final()
   * x.update(' world')
   * h2 = x.final()
   *
   * to get h1 = H('hello') and h2 = H('hello world')
   *
   * This wouldn't work if we applied MD-padding to *ctx.
   */

  cf_sha256_context ours = *ctx;
  uint8_t padbuf[CF_SHA256_BLOCKSZ];

  uint64_t digested_bytes = ours.blocks * CF_SHA256_BLOCKSZ + ours.npartial;
  uint64_t digested_bits = digested_bytes * 8;

  size_t zeroes = CF_SHA256_BLOCKSZ - ((digested_bytes + 1 + 8) % CF_SHA256_BLOCKSZ);

  /* Hash 0x80 00 ... block first. */
  padbuf[0] = 0x80;
  memset(padbuf + 1, 0, zeroes);
  cf_sha256_update(&ours, padbuf, 1 + zeroes);

  /* Now hash length. */
  write64_be(digested_bits, padbuf);
  cf_sha256_update(&ours, padbuf, 8);

  /* We ought to have got our padding calculation right! */
  assert(ours.npartial == 0);

  write32_be(ours.H[0], hash + 0);
  write32_be(ours.H[1], hash + 4);
  write32_be(ours.H[2], hash + 8);
  write32_be(ours.H[3], hash + 12);
  write32_be(ours.H[4], hash + 16);
  write32_be(ours.H[5], hash + 20);
  write32_be(ours.H[6], hash + 24);
  write32_be(ours.H[7], hash + 28);
}

void cf_sha224_final(const cf_sha256_context *ctx, uint8_t hash[CF_SHA224_HASHSZ])
{
  uint8_t full[CF_SHA256_HASHSZ];
  cf_sha256_final(ctx, full);
  memcpy(hash, full, CF_SHA224_HASHSZ);
}

const cf_chash cf_sha224 = {
  .hashsz = CF_SHA224_HASHSZ,
  .ctxsz = sizeof(cf_sha256_context),
  .blocksz = CF_SHA256_BLOCKSZ,
  .init = (cf_chash_init) cf_sha224_init,
  .update = (cf_chash_update) cf_sha224_update,
  .final = (cf_chash_final) cf_sha224_final
};

const cf_chash cf_sha256 = {
  .hashsz = CF_SHA256_HASHSZ,
  .ctxsz = sizeof(cf_sha256_context),
  .blocksz = CF_SHA256_BLOCKSZ,
  .init = (cf_chash_init) cf_sha256_init,
  .update = (cf_chash_update) cf_sha256_update,
  .final = (cf_chash_final) cf_sha256_final
};

