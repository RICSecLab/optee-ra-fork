// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2024, Institute of Information Security (IISEC)
 */

#include <crypto/crypto.h>
#include <crypto/crypto_impl.h>
#include <stdlib_ext.h>
#include <string.h>
#include <utee_defines.h>

#include "sign.h"

#define KEY_SIZE 32
#define KEY_SIZE_BIT (KEY_SIZE * 8)

static struct ecc_keypair *key;
static struct ecc_public_key *pubkey;

#ifdef CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
/*
 * FIXME: Currently, keys are directly embedded within the code. From a security
 * standpoint these keys should be stored in a secure location and properly
 * loaded during program execution in a production environment.
 * The key information has been extracted using the command:
 *   $ echo "MKBCTNIcKUSDii11ySs3526iDZ8AiTo7Tu6KPAqv7D4=" | base64 -d | xxd -p
 * (and similar steps for obtaining the x, y, d values).
 *
 * {
 *   "kty": "EC",
 *   "crv": "P-256",
 *   "x": "MKBCTNIcKUSDii11ySs3526iDZ8AiTo7Tu6KPAqv7D4",
 *   "y": "4Etl6SRW2YiLUrN5vfvVHuhp7x8PxltmWWlbbM4IFyM",
 *   "d": "870MB6gfuTJ4HtUnUvYMyJpr5eUZNP4Bk43bVdj3eAE"
 * }
 */

/* clang-format off */
#define PUBLIC_KEY_X {                                             \
		0x30, 0xa0, 0x42, 0x4c, 0xd2, 0x1c, 0x29, 0x44,    \
		0x83, 0x8a, 0x2d, 0x75, 0xc9, 0x2b, 0x37, 0xe7,    \
		0x6e, 0xa2, 0x0d, 0x9f, 0x00, 0x89, 0x3a, 0x3b,    \
		0x4e, 0xee, 0x8a, 0x3c, 0x0a, 0xaf, 0xec, 0x3e     \
	}
#define PUBLIC_KEY_Y {                                             \
		0xe0, 0x4b, 0x65, 0xe9, 0x24, 0x56, 0xd9, 0x88,    \
		0x8b, 0x52, 0xb3, 0x79, 0xbd, 0xfb, 0xd5, 0x1e,    \
		0xe8, 0x69, 0xef, 0x1f, 0x0f, 0xc6, 0x5b, 0x66,    \
		0x59, 0x69, 0x5b, 0x6c, 0xce, 0x08, 0x17, 0x23     \
	}
#define PRIVATE_KEY {                                              \
		0xf3, 0xbd, 0x0c, 0x07, 0xa8, 0x1f, 0xb9, 0x32,    \
		0x78, 0x1e, 0xd5, 0x27, 0x52, 0xf6, 0x0c, 0xc8,    \
		0x9a, 0x6b, 0xe5, 0xe5, 0x19, 0x34, 0xfe, 0x01,    \
		0x93, 0x8d, 0xdb, 0x55, 0xd8, 0xf7, 0x78, 0x01     \
	}
/* clang-format on */
#else
/* No embedded test key configured */
#endif

static TEE_Result hash_sha256(const uint8_t *msg, size_t msg_len, uint8_t *hash)
{
	TEE_Result res = TEE_SUCCESS;
	void *ctx = NULL;

	DMSG("[veraison-pta] hash_sha256: msg_len=%zu", msg_len);

	res = crypto_hash_alloc_ctx(&ctx, TEE_ALG_SHA256);
	if (res != TEE_SUCCESS)
		return res;
	res = crypto_hash_init(ctx);
	if (res != TEE_SUCCESS)
		goto out;
	res = crypto_hash_update(ctx, msg, msg_len);
	if (res != TEE_SUCCESS)
		goto out;
	res = crypto_hash_final(ctx, hash, TEE_SHA256_HASH_SIZE);

out:
	crypto_hash_free_ctx(ctx);
	DMSG("[veraison-pta] hash_sha256: rc=0x%x", res);
	return res;
}

static void free_keypair(void)
{
	if (!key)
		return;

	crypto_bignum_free(&key->d);
	crypto_bignum_free(&key->x);
	crypto_bignum_free(&key->y);

	free_wipe(key);
	key = NULL;
	DMSG("[veraison-pta] free_keypair");
}

static void free_pubkey(void)
{
	if (!pubkey)
		return;

	crypto_bignum_free(&pubkey->x);
	crypto_bignum_free(&pubkey->y);

	free_wipe(pubkey);
	pubkey = NULL;
	DMSG("[veraison-pta] free_pubkey");
}

static TEE_Result generate_key(const uint8_t *ser_bk, size_t ser_bk_len)
{
	TEE_Result res = TEE_SUCCESS;
	DMSG("[veraison-pta] generate_key: serialized_black_key_len=%zu", ser_bk_len);
    /* Embedded test key material is only available when
     * CFG_VERAISON_ATTESTATION_PTA_TEST_KEY is enabled.
     */
#ifdef CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
    const uint8_t private_key[] = PRIVATE_KEY;
    const uint8_t public_key_x[] = PUBLIC_KEY_X;
    const uint8_t public_key_y[] = PUBLIC_KEY_Y;
#endif

	/* Allocate a private key storage */
	assert(!key);
	key = calloc(1, sizeof(*key));
	if (!key)
		return TEE_ERROR_OUT_OF_MEMORY;
	res = crypto_acipher_alloc_ecc_keypair(key, TEE_TYPE_ECDSA_KEYPAIR,
					       KEY_SIZE_BIT);
	if (res != TEE_SUCCESS)
		goto free_keypair;
	key->curve = TEE_ECC_CURVE_NIST_P256;
	DMSG("[veraison-pta] generate_key: curve=P-256");

    /*
     * Import policy:
     *  - If a serialized CAAM black key is provided, import it directly and
     *    skip public key setup (verification will be skipped).
     *  - Otherwise, if a test key is enabled at build time, import the
     *    embedded plaintext d and set the corresponding public key for
     *    self-verification.
     *  - If neither is available, return TEE_ERROR_NOT_SUPPORTED.
     */
	if (ser_bk && ser_bk_len) {
        /*
         * Accept either plain d (32 bytes) or CAAM serialized black key (>32 bytes).
         * For CAAM black key, CAAM driver must be available.
         */
		if (ser_bk_len == KEY_SIZE) {
            DMSG("Importing plain private key d (32B)");
            res = crypto_bignum_bin2bn(ser_bk, ser_bk_len, key->d);
            if (res != TEE_SUCCESS)
                goto free_keypair;
            return TEE_SUCCESS;
        }
#ifdef CFG_NXP_CAAM_ECC_DRV
        DMSG("Importing serialized key (len=%zu) as CAAM black key", ser_bk_len);
        res = crypto_bignum_bin2bn(ser_bk, ser_bk_len, key->d);
        if (res != TEE_SUCCESS)
            goto free_keypair;
        return TEE_SUCCESS;
#else
		EMSG("[veraison-pta] Serialized black key provided but CAAM not available");
        res = TEE_ERROR_NOT_SUPPORTED;
        goto free_keypair;
#endif
    }

#ifdef CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
    /* Import embedded plaintext private key */
	res = crypto_bignum_bin2bn(private_key, KEY_SIZE, key->d);
    if (res != TEE_SUCCESS)
        goto free_keypair;

    /* Allocate and populate a public key for verification */
	assert(!pubkey);
    pubkey = calloc(1, sizeof(*pubkey));
    if (!pubkey) {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto free_keypair;
    }
	res = crypto_acipher_alloc_ecc_public_key(pubkey,
                          TEE_TYPE_ECDSA_PUBLIC_KEY,
                          KEY_SIZE_BIT);
    if (res != TEE_SUCCESS)
        goto free_pubkey;
	pubkey->curve = TEE_ECC_CURVE_NIST_P256;
	DMSG("[veraison-pta] generate_key: public key ready");

    /* Copy the embedded public key */
    res = crypto_bignum_bin2bn(public_key_x, KEY_SIZE, pubkey->x);
    if (res != TEE_SUCCESS)
        goto free_pubkey;

    res = crypto_bignum_bin2bn(public_key_y, KEY_SIZE, pubkey->y);
    if (res != TEE_SUCCESS)
        goto free_pubkey;

    return TEE_SUCCESS;
#else
    /* No black key and no test key configured */
    res = TEE_ERROR_NOT_SUPPORTED;
    goto free_keypair;
#endif

free_pubkey:
    free_pubkey();
free_keypair:
    free_keypair();

    return res;
}

TEE_Result sign_ecdsa_sha256(const uint8_t *msg, size_t msg_len, uint8_t *sig,
                 size_t *sig_len,
                 const uint8_t *serialized_black_key,
                 size_t serialized_black_key_len)
{
	TEE_Result res = TEE_SUCCESS;
	uint8_t hash_msg[TEE_SHA256_HASH_SIZE] = { };

	EMSG("[veraison-pta] sign_ecdsa_sha256: msg_len=%zu, key_len=%zu",
	     msg_len, serialized_black_key_len);

    /* Allocate the key pair*/
	res = generate_key(serialized_black_key, serialized_black_key_len);
	if (res != TEE_SUCCESS) {
		EMSG("[veraison-pta] generate_key failed: 0x%x", res);
		return res;
	}

	/* Hash the msg */
	res = hash_sha256(msg, msg_len, hash_msg);
	if (res != TEE_SUCCESS) {
		EMSG("[veraison-pta] hash_sha256 failed: 0x%x", res);
		goto free;
	}

	/* Sign the hashed msg by the key pair*/
	res = crypto_acipher_ecc_sign(TEE_ALG_ECDSA_SHA256, key, hash_msg,
			      TEE_SHA256_HASH_SIZE, sig, sig_len);
	EMSG("[veraison-pta] ecc_sign rc=0x%x, sig_len=%zu", res,
	     res == TEE_SUCCESS ? (size_t)*sig_len : (size_t)0);
	if (res != TEE_SUCCESS)
		goto free;

    /* Verify the signature only when a public key is available */
	if (pubkey) {
        TEE_Result verify_res = crypto_acipher_ecc_verify(
            TEE_ALG_ECDSA_SHA256, pubkey, hash_msg,
            TEE_SHA256_HASH_SIZE, sig, *sig_len);
		if (verify_res == TEE_SUCCESS)
			DMSG("[veraison-pta] verify success");
		else
			EMSG("[veraison-pta] verify failed: 0x%x", verify_res);
    }

free:
	free_pubkey();
	assert(!pubkey);
	free_keypair();
	assert(!key);

	DMSG("[veraison-pta] sign_ecdsa_sha256: exit rc=0x%x", res);
	return res;
}
