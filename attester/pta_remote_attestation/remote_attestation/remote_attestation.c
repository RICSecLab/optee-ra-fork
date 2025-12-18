// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2024, Institute of Information Security (IISEC)
 */

#include <base64.h>
#include <crypto/crypto.h>
#include <kernel/pseudo_ta.h>
#include <mempool.h>
#include <pta_veraison_attestation.h>
#include <stdlib.h>

/*
 * Custom PTA UUID for remote_attestation - different from upstream
 * veraison_attestation (a77955f9-eea1-44fd-add5-4a9d962afcf5).
 * This allows both PTAs to coexist without conflict.
 */
#define PTA_REMOTE_ATTESTATION_UUID \
	{ 0x7ccf76c3, 0x4dfe, 0x4310, \
		{ 0xbf, 0xd4, 0x98, 0x2f, 0x3a, 0x2f, 0xe9, 0xa2 } }

/* Override official OP-TEE command IDs with our extended set */
#undef PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE
#define PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR 0x0
#define PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY  0x1
#define PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE    0x2
#include <string.h>

#ifdef CFG_NXP_CAAM_ECC_DRV
#include <caam_key.h>
#include <caam_status.h>
#endif

#include "cbor.h"
#include "hash.h"
#include "sign.h"

#define PTA_NAME "veraison_attestation.pta"

#define MAX_KEY_SIZE 4096
#define MAX_NONCE_SIZE 64
#define TEE_SHA256_HASH_SIZE 32

#define EAT_PROFILE "http://arm.com/psa/2.0.0"
#define CLIENT_ID 1
#define LIFECYCLE 12288
#define MEASURMENT_TYPE "PRoT"
#define SIGNER_ID_LEN 32
#define INSTANCE_ID_LEN 33

/* clang-format off */
#define SIGNER_ID {                                                \
		0xac, 0xbb, 0x11, 0xc7, 0xe4, 0xda, 0x21, 0x72,    \
		0x05, 0x52, 0x3c, 0xe4, 0xce, 0x1a, 0x24, 0x5a,    \
		0xe1, 0xa2, 0x39, 0xae, 0x3c, 0x6b, 0xfd, 0x9e,    \
		0x78, 0x71, 0xf7, 0xe5, 0xd8, 0xba, 0xe8, 0x6b     \
	}
#define INSTANCE_ID {                                              \
		0x01, 0xce, 0xeb, 0xae, 0x7b, 0x89, 0x27, 0xa3,    \
		0x22, 0x7e, 0x53, 0x03, 0xcf, 0x5e, 0x0f, 0x1f,    \
		0x7b, 0x34, 0xbb, 0x54, 0x2a, 0xd7, 0x25, 0x0a,    \
		0xc0, 0x3f, 0xbc, 0xde, 0x36, 0xec, 0x2f, 0x15,    \
		0x08                                               \
	}
/* clang-format on */

#ifdef CFG_NXP_CAAM_ECC_DRV
/*
 * Convert CAAM status to TEE_Result
 */
static TEE_Result caam_to_tee_status(enum caam_status status)
{
	switch (status) {
	case CAAM_NO_ERROR:
		return TEE_SUCCESS;
	case CAAM_OUT_MEMORY:
		return TEE_ERROR_OUT_OF_MEMORY;
	case CAAM_BAD_PARAM:
		return TEE_ERROR_BAD_PARAMETERS;
	case CAAM_NOT_SUPPORTED:
		return TEE_ERROR_NOT_SUPPORTED;
	case CAAM_SHORT_BUFFER:
		return TEE_ERROR_SHORT_BUFFER;
	default:
		return TEE_ERROR_GENERIC;
	}
}
#endif

static TEE_Result cmd_get_cbor_evidence(uint32_t param_types,
					TEE_Param params[TEE_NUM_PARAMS])
{
    /* Debug: print param types and sizes */
    EMSG("[veraison-pta] param_types=0x%x t0=%u t1=%u t2=%u t3=%u",
         param_types,
         TEE_PARAM_TYPE_GET(param_types, 0),
         TEE_PARAM_TYPE_GET(param_types, 1),
         TEE_PARAM_TYPE_GET(param_types, 2),
         TEE_PARAM_TYPE_GET(param_types, 3));
    
    /* Debug: check if param_types matches expected patterns */
    uint32_t expected1 = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                        TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_NONE);
    uint32_t expected2 = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                        TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_MEMREF_INPUT);
    uint32_t expected3 = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_MEMREF_INOUT,
                                        TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_NONE);
    uint32_t expected4 = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_MEMREF_INOUT,
                                        TEE_PARAM_TYPE_MEMREF_INPUT,
                                        TEE_PARAM_TYPE_MEMREF_INPUT);
    
    EMSG("[veraison-pta] expected1=0x%x expected2=0x%x expected3=0x%x expected4=0x%x",
         expected1, expected2, expected3, expected4);
	const uint8_t *nonce = params[0].memref.buffer;
	const size_t nonce_sz = params[0].memref.size;
	uint8_t *output_buffer = params[1].memref.buffer;
	size_t *output_buffer_len = &params[1].memref.size;
	const uint8_t *psa_implementation_id = params[2].memref.buffer;
	const size_t psa_implementation_id_len = params[2].memref.size;
	TEE_Result status = TEE_SUCCESS;

    EMSG("[veraison-pta] sizes: nonce=%zu out_len=%zu impl_id_len=%zu",
         nonce_sz, *output_buffer_len, psa_implementation_id_len);

	const char eat_profile[] = EAT_PROFILE;
	const int psa_client_id = CLIENT_ID;
	const int psa_security_lifecycle = LIFECYCLE;
	const char measurement_type[] = MEASURMENT_TYPE;
	const uint8_t signer_id[SIGNER_ID_LEN] = SIGNER_ID;
	const uint8_t psa_instance_id[INSTANCE_ID_LEN] = INSTANCE_ID;

	uint8_t measurement_value[TEE_SHA256_HASH_SIZE] = { 0 };
	size_t b64_measurement_value_len = TEE_SHA256_HASH_SIZE * 2;
	char b64_measurement_value[TEE_SHA256_HASH_SIZE * 2] = { 0 };

    UsefulBufC ubc_cbor_evidence = { NULL, 0 };
    UsefulBufC ubc_cose_evidence = { NULL, 0 };
    const uint8_t *serialized_black_key = NULL;
    size_t serialized_black_key_len = 0;

    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                       TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                       TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_MEMREF_INPUT) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_MEMREF_INOUT,
                       TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_MEMREF_INOUT,
                       TEE_PARAM_TYPE_MEMREF_INPUT,
                       TEE_PARAM_TYPE_MEMREF_INPUT))
    {
        EMSG("[veraison-pta] Bad param_types for GET_CBOR_EVIDENCE");
        return TEE_ERROR_BAD_PARAMETERS;
    }

	if (!nonce || !nonce_sz) {
    	EMSG("[veraison-pta] Bad nonce pointer/size");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (!output_buffer && *output_buffer_len) {
		EMSG("[veraison-pta] Bad output buffer pointer/size");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* Calculate measurement hash of memory */
	status = get_hash_ta_memory(measurement_value);
	if (status != TEE_SUCCESS)
		return status;

	/* Encode measurement_value to base64 */
	if (!base64_enc(measurement_value, TEE_SHA256_HASH_SIZE,
			b64_measurement_value,
			&b64_measurement_value_len)) {
		DMSG("Failed to encode measurement_value to base64");
		return TEE_ERROR_GENERIC;
	}
	DMSG("b64_measurement_value: %s", b64_measurement_value);

	/* Encode evidence to CBOR */
	ubc_cbor_evidence = generate_cbor_evidence(eat_profile,
						   psa_client_id,
						   psa_security_lifecycle,
						   psa_implementation_id,
						   psa_implementation_id_len,
						   measurement_type,
						   signer_id,
						   SIGNER_ID_LEN,
						   psa_instance_id,
						   INSTANCE_ID_LEN,
						   nonce,
						   nonce_sz,
						   measurement_value,
						   TEE_SHA256_HASH_SIZE);
	if (UsefulBuf_IsNULLC(ubc_cbor_evidence)) {
		DMSG("Failed to encode evidence to CBOR");
		return TEE_ERROR_GENERIC;
	}

    /* Optional serialized black key in params[3] */
    if (TEE_PARAM_TYPE_GET(param_types, 3) == TEE_PARAM_TYPE_MEMREF_INPUT) {
        serialized_black_key = params[3].memref.buffer;
        serialized_black_key_len = params[3].memref.size;
        EMSG("[veraison-pta] received key blob: %zu bytes", serialized_black_key_len);

        /* Check if black key is provided */
        if (serialized_black_key && serialized_black_key_len > 0) {
#ifndef CFG_NXP_CAAM_ECC_DRV
            /* CAAM is absent; use provided software private key */
            DMSG("CAAM not available; using provided software private key");
#else
            /* CAAM is available, use the provided black key */
            DMSG("Using provided black key (%zu bytes)",
                 serialized_black_key_len);
#endif
        } else {
            /* No black key provided */
#ifdef CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
            DMSG("No black key provided, using embedded test key");
            serialized_black_key = NULL;
            serialized_black_key_len = 0;
#else
            EMSG("No black key provided and test key disabled");
            status = TEE_ERROR_NOT_SUPPORTED;
            goto free_ubc_cbor_evidence;
#endif
        }
    } else {
        /* params[3] is NONE */
#ifdef CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
        DMSG("No black key parameter, using embedded test key");
        serialized_black_key = NULL;
        serialized_black_key_len = 0;
#else
        EMSG("No black key parameter and test key disabled");
        status = TEE_ERROR_NOT_SUPPORTED;
        goto free_ubc_cbor_evidence;
#endif
    }

	/* Sign the CBOR and generate a COSE evidence with optional black key */
	ubc_cose_evidence = generate_cose_evidence(ubc_cbor_evidence,
						   serialized_black_key,
						   serialized_black_key_len);
	if (UsefulBuf_IsNULLC(ubc_cose_evidence)) {
		DMSG("Failed to encode CBOR to COSE");
		status = TEE_ERROR_GENERIC;
		goto free_ubc_cbor_evidence;
	}

	/* Copy COSE evidence for return buffer */
	if (ubc_cose_evidence.len > *output_buffer_len) {
		*output_buffer_len = ubc_cose_evidence.len;
		status = TEE_ERROR_SHORT_BUFFER;
		goto free_ubc_cose_evidence;
	}
	memcpy(output_buffer, ubc_cose_evidence.ptr, ubc_cose_evidence.len);
	*output_buffer_len = ubc_cose_evidence.len;

	/* Free mempool allocation before returning to the caller */
free_ubc_cose_evidence:
	mempool_free(mempool_default, (void *)ubc_cose_evidence.ptr);
free_ubc_cbor_evidence:
	mempool_free(mempool_default, (void *)ubc_cbor_evidence.ptr);

	return status;
}

static TEE_Result cmd_convert_to_blackkey(uint32_t param_types,
					   TEE_Param params[TEE_NUM_PARAMS])
{
#ifdef CFG_NXP_CAAM_ECC_DRV
	/*
	 * Convert plain ECC private key to CAAM black key
	 * [in]  memref[0]: plain private key d (32 bytes for P-256)
	 * [out] memref[1]: serialized black key (size-probe allowed)
	 * [out] memref[2]: public key X coordinate (32 bytes)
	 * [out] memref[3]: public key Y coordinate (32 bytes)
	 */
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT,
					  TEE_PARAM_TYPE_MEMREF_OUTPUT);
	TEE_Result res = TEE_ERROR_GENERIC;
	enum caam_status caam_res = CAAM_FAILURE;
	struct caamkey caam_key = { };
	struct ecc_keypair ecc_key = { };
	uint8_t *plain_d = NULL;
	size_t plain_d_size = 0;
	uint8_t *out_ser = NULL;
	size_t out_ser_size = 0;
	uint8_t *out_x = NULL;
	size_t out_x_size = 0;
	uint8_t *out_y = NULL;
	size_t out_y_size = 0;
	size_t need_size = 0;
	size_t x_len = 0;
	size_t y_len = 0;
	const size_t sec_size = 32; /* P-256 */
	enum caam_key_type enc_type = CAAM_KEY_BLACK_CCM;

	if (param_types != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	plain_d = params[0].memref.buffer;
	plain_d_size = params[0].memref.size;
	out_ser = params[1].memref.buffer;
	out_ser_size = params[1].memref.size;
	out_x = params[2].memref.buffer;
	out_x_size = params[2].memref.size;
	out_y = params[3].memref.buffer;
	out_y_size = params[3].memref.size;

	/* Validate input private key size (P-256) */
	if (!plain_d || plain_d_size != sec_size)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Support size probe for serialized black key */
	if (!out_ser && out_ser_size)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Validate public key output buffers */
	if ((!out_x && out_x_size) || (!out_y && out_y_size))
		return TEE_ERROR_BAD_PARAMETERS;

	/* Initialize CAAM key structure with plain scalar d */
	caam_key.key_type = CAAM_KEY_PLAIN_TEXT;
	caam_key.sec_size = plain_d_size;
	caam_key.is_blob = false;

	caam_res = caam_key_alloc(&caam_key);
	if (caam_res != CAAM_NO_ERROR)
		return caam_to_tee_status(caam_res);

	memcpy(caam_key.buf.data, plain_d, plain_d_size);

	/* Convert to black key (CCM mode) */
	caam_res = caam_key_black_encapsulation(&caam_key, enc_type);
	if (caam_res != CAAM_NO_ERROR) {
		EMSG("[veraison-pta] Black key encapsulation failed: %d", caam_res);
		res = caam_to_tee_status(caam_res);
		goto out_caam;
	}

	/* Compute required serialized size (header + blob) */
	caam_res = caam_key_serialized_size(&caam_key, &need_size);
	if (caam_res != CAAM_NO_ERROR) {
		res = caam_to_tee_status(caam_res);
		goto out_caam;
	}

	if (out_ser_size < need_size) {
		params[1].memref.size = need_size;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out_caam;
	}

	/* Serialize to binary buffer */
	if (out_ser) {
		caam_res = caam_key_serialize_to_bin(out_ser, out_ser_size, &caam_key);
		if (caam_res != CAAM_NO_ERROR) {
			res = caam_to_tee_status(caam_res);
			goto out_caam;
		}
		params[1].memref.size = need_size;
	}

	/*
	 * Compute public key from private key.
	 * We need to allocate an ECC keypair and generate the public key
	 * from the original plain private key.
	 */
	res = crypto_acipher_alloc_ecc_keypair(&ecc_key, TEE_TYPE_ECDSA_KEYPAIR,
					       sec_size * 8);
	if (res != TEE_SUCCESS)
		goto out_caam;

	ecc_key.curve = TEE_ECC_CURVE_NIST_P256;

	/* Import the plain private key */
	ecc_key.d = crypto_bignum_allocate(sec_size * 8);
	if (!ecc_key.d) {
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto out_ecc;
	}
	crypto_bignum_bin2bn(plain_d, plain_d_size, ecc_key.d);

	/* Allocate public key coordinates */
	ecc_key.x = crypto_bignum_allocate(sec_size * 8);
	ecc_key.y = crypto_bignum_allocate(sec_size * 8);
	if (!ecc_key.x || !ecc_key.y) {
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto out_ecc;
	}

	/*
	 * Generate public key from private key using ECC point multiplication.
	 * We generate a temporary keypair and replace the private key to 
	 * compute the corresponding public key.
	 */
	res = crypto_acipher_gen_ecc_key(&ecc_key, sec_size * 8);
	if (res != TEE_SUCCESS) {
		EMSG("[veraison-pta] Failed to generate public key: 0x%x", res);
		goto out_ecc;
	}

	/* Export public key X coordinate */
	x_len = crypto_bignum_num_bytes(ecc_key.x);
	if (out_x_size < sec_size) {
		params[2].memref.size = sec_size;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out_ecc;
	}
	if (out_x) {
		memset(out_x, 0, sec_size);
		if (x_len > sec_size) {
			res = TEE_ERROR_GENERIC;
			goto out_ecc;
		}
		crypto_bignum_bn2bin(ecc_key.x, out_x + (sec_size - x_len));
		params[2].memref.size = sec_size;
	}

	/* Export public key Y coordinate */
	y_len = crypto_bignum_num_bytes(ecc_key.y);
	if (out_y_size < sec_size) {
		params[3].memref.size = sec_size;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out_ecc;
	}
	if (out_y) {
		memset(out_y, 0, sec_size);
		if (y_len > sec_size) {
			res = TEE_ERROR_GENERIC;
			goto out_ecc;
		}
		crypto_bignum_bn2bin(ecc_key.y, out_y + (sec_size - y_len));
		params[3].memref.size = sec_size;
	}

	res = TEE_SUCCESS;

out_ecc:
	crypto_bignum_free(&ecc_key.d);
	crypto_bignum_free(&ecc_key.x);
	crypto_bignum_free(&ecc_key.y);
out_caam:
	caam_key_free(&caam_key);
	return res;
#else
	/* CAAM support is required for black key conversion */
	(void)param_types;
	(void)params;
	return TEE_ERROR_NOT_SUPPORTED;
#endif
}

static TEE_Result cmd_generate_ecc_keypair(uint32_t param_types,
					    TEE_Param params[TEE_NUM_PARAMS])
{
#ifdef CFG_NXP_CAAM_ECC_DRV
	/* Generate CAAM-backed ECC P-256 keypair and export:
	 *  - memref[0]: serialized black key (d), size-probe allowed
	 *  - memref[1]: public X (32 bytes)
	 *  - memref[2]: public Y (32 bytes)
	 */
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
						 TEE_PARAM_TYPE_MEMREF_OUTPUT,
						 TEE_PARAM_TYPE_MEMREF_OUTPUT,
						 TEE_PARAM_TYPE_NONE);
	TEE_Result res = TEE_SUCCESS;
	struct ecc_keypair key = { };
	uint8_t *out_ser = NULL;
	size_t out_ser_size = 0;
	uint8_t *out_x = NULL;
	size_t out_x_size = 0;
	uint8_t *out_y = NULL;
	size_t out_y_size = 0;
	size_t d_len = 0;
	size_t x_len = 0;
	size_t y_len = 0;
	const size_t sec_size = 32; /* P-256 */

	if (param_types != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	out_ser = params[0].memref.buffer;
	out_ser_size = params[0].memref.size;
	out_x = params[1].memref.buffer;
	out_x_size = params[1].memref.size;
	out_y = params[2].memref.buffer;
	out_y_size = params[2].memref.size;

	/* Allocate ECC keypair (P-256) */
	res = crypto_acipher_alloc_ecc_keypair(&key, TEE_TYPE_ECDSA_KEYPAIR,
						   sec_size * 8);
	if (res != TEE_SUCCESS)
		return res;
	key.curve = TEE_ECC_CURVE_NIST_P256;

	/* Generate keypair via CAAM (d returned as serialized black key bignum) */
	res = crypto_acipher_gen_ecc_key(&key, sec_size * 8);
	if (res != TEE_SUCCESS)
		goto out_free;

	/* Determine sizes */
	d_len = crypto_bignum_num_bytes(key.d);
	x_len = crypto_bignum_num_bytes(key.x);
	y_len = crypto_bignum_num_bytes(key.y);

	/* Handle size-probe and buffer checks */
	if (!out_ser && out_ser_size)
		{ res = TEE_ERROR_BAD_PARAMETERS; goto out_free; }
	if (out_ser_size < d_len) {
		params[0].memref.size = d_len;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out_free;
	}
	if (!out_x && out_x_size)
		{ res = TEE_ERROR_BAD_PARAMETERS; goto out_free; }
	if (out_x_size < sec_size) {
		params[1].memref.size = sec_size;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out_free;
	}
	if (!out_y && out_y_size)
		{ res = TEE_ERROR_BAD_PARAMETERS; goto out_free; }
	if (out_y_size < sec_size) {
		params[2].memref.size = sec_size;
		res = TEE_ERROR_SHORT_BUFFER;
		goto out_free;
	}

	/* Export serialized black key (exact length) */
	if (out_ser && d_len) {
		crypto_bignum_bn2bin(key.d, out_ser);
		params[0].memref.size = d_len;
	}

	/* Export public X/Y, left-pad to 32 bytes */
	if (out_x) {
		memset(out_x, 0, sec_size);
		if (x_len > sec_size) { res = TEE_ERROR_GENERIC; goto out_free; }
		crypto_bignum_bn2bin(key.x, out_x + (sec_size - x_len));
		params[1].memref.size = sec_size;
	}
	if (out_y) {
		memset(out_y, 0, sec_size);
		if (y_len > sec_size) { res = TEE_ERROR_GENERIC; goto out_free; }
		crypto_bignum_bn2bin(key.y, out_y + (sec_size - y_len));
		params[2].memref.size = sec_size;
	}

	res = TEE_SUCCESS;

out_free:
	crypto_bignum_free(&key.d);
	crypto_bignum_free(&key.x);
	crypto_bignum_free(&key.y);
	return res;
#else
	/* Keypair generation for black key is only available with CAAM */
	(void)param_types;
	(void)params;
	return TEE_ERROR_NOT_SUPPORTED;
#endif
}

static TEE_Result create_entry_point(void)
{
	return TEE_SUCCESS;
}

static void destroy_entry_point(void)
{
}

static TEE_Result open_session_entry_point(uint32_t nParamTypes __unused,
					   TEE_Param pParams[TEE_NUM_PARAMS] __unused,
					   void **ppSessionContext __unused)
{
	EMSG("[veraison-pta] open_session_entry_point called");
	return TEE_SUCCESS;
}

static void close_session_entry_point(void *pSessionContext __unused)
{
}

static TEE_Result invoke_command(void *sess_ctx __unused, uint32_t cmd_id,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	EMSG("[veraison-pta] invoke_command: cmd_id=%u", cmd_id);

	switch (cmd_id) {
#ifdef CFG_NXP_CAAM_ECC_DRV
	case PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR:
		EMSG("[veraison-pta] Calling cmd_generate_ecc_keypair");
		return cmd_generate_ecc_keypair(param_types, params);
	case PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY:
		EMSG("[veraison-pta] Calling cmd_convert_to_blackkey");
		return cmd_convert_to_blackkey(param_types, params);
#endif
	case PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE:
		EMSG("[veraison-pta] Calling cmd_get_cbor_evidence");
		return cmd_get_cbor_evidence(param_types, params);
	default:
		EMSG("[veraison-pta] Unknown command: %u", cmd_id);
		break;
	}
	return TEE_ERROR_NOT_IMPLEMENTED;
}

pseudo_ta_register(.uuid = PTA_REMOTE_ATTESTATION_UUID, .name = PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS,
		   .create_entry_point = create_entry_point,
		   .destroy_entry_point = destroy_entry_point,
		   .open_session_entry_point = open_session_entry_point,
		   .close_session_entry_point = close_session_entry_point,
		   .invoke_command_entry_point = invoke_command);
