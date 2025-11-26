// SPDX-License-Identifier: BSD-3-Clause
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <string.h>

#include <pta_attestation.h>
#include <pta_veraison_attestation.h>
#include <veraison_attestation_ta.h>

/* Force correct PTA command definitions (override any old header definitions) */
#ifdef PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR
#undef PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR
#endif
#define PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR 0x0

#ifdef PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY
#undef PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY
#endif
#define PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY 0x1

#ifdef PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE
#undef PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE
#endif
#define PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE 0x2

static TEE_Result try_generate_black_key(TEE_TASessionHandle sess,
                                         void **out_blob,
                                         size_t *out_blob_len) {
    TEE_Result res = TEE_SUCCESS;
    uint32_t ret_orig = 0;
    uint8_t pub_x[32] = {0};
    uint8_t pub_y[32] = {0};
    /* First, size probe for black key blob */
    TEE_Param gen_params[4] = {
        { .memref = { .buffer = NULL,  .size = 0 } },
        { .memref = { .buffer = pub_x, .size = sizeof(pub_x) } },
        { .memref = { .buffer = pub_y, .size = sizeof(pub_y) } },
        { 0 }
    };
    uint32_t gen_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                               TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                               TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                               TEE_PARAM_TYPE_NONE);

    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                              PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR,
                              gen_param_types, gen_params, &ret_orig);
    if (res != TEE_ERROR_SHORT_BUFFER && res != TEE_SUCCESS)
        return res;

    if (res == TEE_ERROR_SHORT_BUFFER && gen_params[0].memref.size == 0)
        return TEE_ERROR_GENERIC;

    size_t blob_len = gen_params[0].memref.size;
    void *blob = TEE_Malloc(blob_len, 0);
    if (!blob)
        return TEE_ERROR_OUT_OF_MEMORY;

    /* Second call: actual allocation */
    gen_params[0].memref.buffer = blob;
    /* sizes for X/Y already set */
    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                              PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR,
                              gen_param_types, gen_params, &ret_orig);
    if (res != TEE_SUCCESS) {
        TEE_Free(blob);
        return res;
    }

    *out_blob = blob;
    *out_blob_len = gen_params[0].memref.size;
    return TEE_SUCCESS;
}


TEE_Result call_pta_for_cbor_evidence(uint32_t param_types,
                                      TEE_Param params[4]) {
    TEE_TASessionHandle sess = TEE_HANDLE_NULL;
    TEE_UUID att_uuid = PTA_VERAISON_ATTESTATION_UUID;
    TEE_Result res = TEE_ERROR_GENERIC;
    uint32_t ret_orig = 0;

    EMSG("[veraison-ta] call_pta_for_cbor_evidence entry");
    EMSG("[veraison-ta] incoming param_types=0x%x p0=%u p1=%u p2=%u p3=%u",
               param_types,
               TEE_PARAM_TYPE_GET(param_types, 0),
               TEE_PARAM_TYPE_GET(param_types, 1),
               TEE_PARAM_TYPE_GET(param_types, 2),
               TEE_PARAM_TYPE_GET(param_types, 3));
    if (TEE_PARAM_TYPE_GET(param_types, 0) == TEE_PARAM_TYPE_MEMREF_INPUT)
        EMSG("[veraison-ta] sizes: nonce=%zu out=%zu",
                   params[0].memref.size, params[1].memref.size);

    res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE, 0, NULL, &sess,
                            &ret_orig);
    if (res != TEE_SUCCESS) {
        EMSG("TEE_OpenTASession failed\n");
        goto cleanup_return;
    }

    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE)) {
        EMSG("[veraison-ta] Bad TA param_types=0x%x t0=%u t1=%u t2=%u t3=%u",
             param_types,
             TEE_PARAM_TYPE_GET(param_types, 0),
             TEE_PARAM_TYPE_GET(param_types, 1),
             TEE_PARAM_TYPE_GET(param_types, 2),
             TEE_PARAM_TYPE_GET(param_types, 3));
        res = TEE_ERROR_BAD_PARAMETERS;
        goto cleanup_return;
    }

    /* Setup implementation ID */
    static const uint8_t psa_implementation_id[IMPLEMENTATION_ID_LEN] = IMPLEMENTATION_ID;

    /* Optional key blob from host (black key or SW private key d) */
    void *key_blob = NULL;
    size_t key_blob_len = 0;
    if (TEE_PARAM_TYPE_GET(param_types, 2) == TEE_PARAM_TYPE_MEMREF_INPUT) {
        key_blob = params[2].memref.buffer;
        key_blob_len = params[2].memref.size;
    }

    /* Forward params to PTA */
    uint32_t pta_param_types = 0;
    TEE_Param pta_params[4] = {0};
    pta_params[0].memref.buffer = params[0].memref.buffer;
    pta_params[0].memref.size = params[0].memref.size;
    pta_params[1].memref.buffer = params[1].memref.buffer;
    pta_params[1].memref.size = params[1].memref.size;
    pta_params[2].memref.buffer = (void *)psa_implementation_id;
    pta_params[2].memref.size = IMPLEMENTATION_ID_LEN;

    if (key_blob && key_blob_len > 0) {
        EMSG("TA passing key blob to PTA: %zu bytes", key_blob_len);
        pta_params[3].memref.buffer = key_blob;
        pta_params[3].memref.size = key_blob_len;
        pta_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                          TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_MEMREF_INPUT);
    } else {
        /* No key provided: rely on PTA policy (e.g., embedded test key) */
        pta_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                          TEE_PARAM_TYPE_MEMREF_INPUT,
                                          TEE_PARAM_TYPE_NONE);
    }

    EMSG("[veraison-ta] PTA call types=0x%x p0=%u p1=%u p2=%u p3=%u sizes: n=%zu out=%zu impl=%zu key=%zu",
         pta_param_types,
         TEE_PARAM_TYPE_GET(pta_param_types, 0),
         TEE_PARAM_TYPE_GET(pta_param_types, 1),
         TEE_PARAM_TYPE_GET(pta_param_types, 2),
         TEE_PARAM_TYPE_GET(pta_param_types, 3),
         params[0].memref.size,
         params[1].memref.size,
         (size_t)IMPLEMENTATION_ID_LEN,
         key_blob_len);

    EMSG("[veraison-ta] About to call PTA cmd_id=%u (GET_CBOR_EVIDENCE=%u)",
         (uint32_t)PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE,
         (uint32_t)PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE);

    /* DEBUG: If black key is provided (92 bytes), return unique error to verify TA is called */
    if (key_blob_len == 92) {
        TEE_CloseTASession(sess);
        return TEE_ERROR_SECURITY;  /* 0xffff0010 - unique error to identify this code path */
    }

    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                              PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE,
                              pta_param_types, pta_params, &ret_orig);
    if (res != TEE_SUCCESS) {
        EMSG("TEE_InvokeTACommand failed res=0x%x\n", res);
        goto cleanup_return;
    }
    /* Update buffer size actually used　*/
    params[1].memref.size = pta_params[1].memref.size;
    /* nothing */

cleanup_return:
    TEE_CloseTASession(sess);
    return res;
}

/*******************************************************************************
 * Mandatory TA functions.
 ******************************************************************************/
TEE_Result TA_CreateEntryPoint(void) {
    /* Debug: Confirm TA is built and loaded - using all log levels */
    trace_printf(NULL, 0, TRACE_ERROR, true, "=== VERAISON TA CREATE ENTRY POINT ===");
    EMSG("================================================");
    EMSG("=== VERAISON TA LOADED ===");
    EMSG("=== Build Date: %s ===", __DATE__);
    EMSG("=== Build Time: %s ===", __TIME__);
    EMSG("=== TA VERSION: 1.0.3 ===");
    EMSG("================================================");
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
    /* Nothing to do */
    return;
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param __unused params[4],
                                    void __unused **sess_ctx) {
    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE)) {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *sess_ctx) {
    /* Nothing to do */
    return;
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *sess_ctx, uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    trace_printf(NULL, 0, TRACE_ERROR, true, "[veraison-ta] TA_InvokeCommandEntryPoint cmd_id=%u", cmd_id);
    EMSG("[veraison-ta] TA_InvokeCommandEntryPoint: cmd_id=%u param_types=0x%x",
         cmd_id, param_types);

    switch (cmd_id) {
    case TA_VERAISON_ATTESTATION_CMD_GENERATE_BLACKKEY: {
        /* Generate new black key from scratch */
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_NONE))
            return TEE_ERROR_BAD_PARAMETERS;

        TEE_TASessionHandle sess2 = TEE_HANDLE_NULL;
        TEE_UUID att_uuid2 = PTA_VERAISON_ATTESTATION_UUID;
        uint32_t ret_orig2 = 0;
        TEE_Result res2 = TEE_OpenTASession(&att_uuid2, TEE_TIMEOUT_INFINITE,
                                            0, NULL, &sess2, &ret_orig2);
        if (res2 != TEE_SUCCESS)
            return res2;
        res2 = TEE_InvokeTACommand(sess2, TEE_TIMEOUT_INFINITE,
                                   PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR,
                                   param_types, params, &ret_orig2);
        TEE_CloseTASession(sess2);
        return res2;
    }
    
    case TA_VERAISON_ATTESTATION_CMD_CONVERT_TO_BLACKKEY: {
        /* Convert plain key to black key */
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT))
            return TEE_ERROR_BAD_PARAMETERS;

        TEE_TASessionHandle sess3 = TEE_HANDLE_NULL;
        TEE_UUID att_uuid3 = PTA_VERAISON_ATTESTATION_UUID;
        uint32_t ret_orig3 = 0;
        TEE_Result res3 = TEE_OpenTASession(&att_uuid3, TEE_TIMEOUT_INFINITE,
                                            0, NULL, &sess3, &ret_orig3);
        if (res3 != TEE_SUCCESS)
            return res3;
        res3 = TEE_InvokeTACommand(sess3, TEE_TIMEOUT_INFINITE,
                                   PTA_VERAISON_ATTESTATION_CONVERT_TO_BLACKKEY,
                                   param_types, params, &ret_orig3);
        TEE_CloseTASession(sess3);
        return res3;
    }

    case TA_VERAISON_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE: {
        /* Get CBOR evidence - simply forward params to PTA */
        /* Expected params: nonce(in), evidence(out), impl_id(in), key(in, optional) */
        TEE_TASessionHandle sess = TEE_HANDLE_NULL;
        TEE_UUID att_uuid = PTA_VERAISON_ATTESTATION_UUID;
        uint32_t ret_orig = 0;
        TEE_Result res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE,
                                           0, NULL, &sess, &ret_orig);
        if (res != TEE_SUCCESS)
            return res;
        /* Forward all parameters directly to PTA */
        res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                                  PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE,
                                  param_types, params, &ret_orig);
        TEE_CloseTASession(sess);
        return res;
    }

    default:
        return TEE_ERROR_BAD_PARAMETERS;
    }
}
