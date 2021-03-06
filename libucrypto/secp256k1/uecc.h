// Copyright 2017 Altronix Corp.
// This file is part of the tiny-ether library
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

/**
 * @author Thomas Chiantia <thomas@altronix>
 * @date 2017
 */

#ifndef DH_H_
#define DH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "secp256k1.h"
#include "secp256k1_ecdh.h"
#include "secp256k1_recovery.h"

typedef unsigned char byte;

// clang-format off
typedef struct { byte b[256] ;} h2048;
typedef struct { byte b[128] ;} h1024;
typedef struct { byte b[65]  ;} h520;
typedef struct { byte b[64]  ;} h512;
typedef struct { byte b[33]  ;} h264;
typedef struct { byte b[32]  ;} h256;
typedef struct { byte b[20]  ;} h160;
typedef struct { byte b[16]  ;} h128;
typedef struct { byte b[8]   ;} h64;
// clang-format on

/*!< r: [0, 32), s: [32, 64), v: 64 */
typedef secp256k1_ecdsa_recoverable_signature uecc_signature;
typedef secp256k1_pubkey uecc_public_key; // without header
typedef h520 uecc_public_key_w_header;    // with header.
typedef h256 uecc_private_key;
typedef h256 uecc_shared_secret;
typedef h264 uecc_shared_secret_w_header;

typedef struct
{
    secp256k1_context* grp;        /*!< lib export */
    uecc_private_key d;            /*!< private key */
    uecc_public_key Q;             /*!< public key */
    uecc_public_key Qp;            /*!< remote public key */
    uecc_shared_secret_w_header z; /*!< shared secret */
} uecc_ctx;

/**
 * @brief initialize a key context
 *
 * @param uecc_ctx
 *
 * @return
 */
int uecc_key_init(uecc_ctx*, const uecc_private_key* d);
int uecc_key_init_binary(uecc_ctx*, const uecc_private_key* d);
int uecc_key_init_string(uecc_ctx* ctx, int radix, const char*);
int uecc_key_init_new(uecc_ctx*);

/**
 * @brief
 *
 * @param uecc_ctx
 */
void uecc_key_deinit(uecc_ctx*);

int uecc_sig_to_bin(const uecc_signature* sig, byte* b65);
int uecc_qtob(const uecc_public_key* q, byte* b, size_t l);
int uecc_btoq(const byte*, size_t l, uecc_public_key* q);
int uecc_cmpq(const uecc_public_key* a, const uecc_public_key* b);

/**
 * @brief
 *
 * @param ctx
 * @param k
 *
 * @return
 */
int uecc_agree(uecc_ctx* ctx, const uecc_public_key* k);
int uecc_agree_bin(uecc_ctx* ctx, const byte* bytes, size_t blen);
/**
 * @brief
 *
 * @param ctx
 * @param b
 * @param sz
 * @param uecc_signature
 *
 * @return
 */
int uecc_sign(uecc_ctx* ctx, const byte* b, size_t sz, uecc_signature*);

int uecc_z_cmp(
    const uecc_shared_secret_w_header* a,
    const uecc_shared_secret_w_header* b);

int uecc_z_cmp_str(const uecc_shared_secret_w_header* a, const char* b);

/**
 * @brief
 *
 * @param q
 * @param b
 * @param sz
 * @param uecc_signature
 *
 * @return
 */
int uecc_verify(
    const uecc_public_key* q,
    const byte* b,
    size_t sz,
    uecc_signature*);

/**
 * @brief
 *
 * @param bytes
 * @param digest
 * @param key
 *
 * @return
 */
int uecc_recover_bin(const byte* bytes, byte* b, uecc_public_key* key);

#ifdef __cplusplus
}
#endif
#endif
