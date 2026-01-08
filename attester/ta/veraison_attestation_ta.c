// SPDX-License-Identifier: BSD-3-Clause
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <string.h>

#include <pta_attestation.h>
#include <pta_veraison_attestation.h>
#include <veraison_attestation_ta.h>

/*
 * Custom PTA UUID for remote_attestation - must match the PTA's UUID.
 * Different from upstream veraison_attestation (a77955f9-eea1-44fd-add5-4a9d962afcf5).
 */
#define PTA_REMOTE_ATTESTATION_UUID \
	{ 0x7ccf76c3, 0x4dfe, 0x4310, \
		{ 0xbf, 0xd4, 0x98, 0x2f, 0x3a, 0x2f, 0xe9, 0xa2 } }

/* PTA command IDs - must match remote_attestation PTA */
#define PTA_CMD_GENERATE_ECC_KEYPAIR  0x0
#define PTA_CMD_CONVERT_TO_BLACKKEY   0x1
#define PTA_CMD_GET_CBOR_EVIDENCE     0x2

TEE_Result call_pta_for_cbor_evidence(uint32_t param_types,
                                      TEE_Param params[4]) {
    TEE_TASessionHandle sess = TEE_HANDLE_NULL;
    TEE_UUID att_uuid = PTA_REMOTE_ATTESTATION_UUID;
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
        EMSG("TEE_OpenTASession failed res=0x%x", res);
        goto cleanup_return;
    }

    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INPUT)) {
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
    if (TEE_PARAM_TYPE_GET(param_types, 3) == TEE_PARAM_TYPE_MEMREF_INPUT) {
        key_blob = params[3].memref.buffer;
        key_blob_len = params[3].memref.size;
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
        EMSG("[veraison-ta] Passing key blob to PTA: %zu bytes", key_blob_len);
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

    EMSG("[veraison-ta] PTA call types=0x%x sizes: nonce=%zu out=%zu impl=%zu key=%zu",
         pta_param_types,
         params[0].memref.size,
         params[1].memref.size,
         (size_t)IMPLEMENTATION_ID_LEN,
         key_blob_len);

    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                              PTA_CMD_GET_CBOR_EVIDENCE,
                              pta_param_types, pta_params, &ret_orig);
    if (res != TEE_SUCCESS) {
        EMSG("TEE_InvokeTACommand failed res=0x%x", res);
        goto cleanup_return;
    }
    /* Update buffer size actually used */
    params[1].memref.size = pta_params[1].memref.size;

cleanup_return:
    TEE_CloseTASession(sess);
    return res;
}

/*******************************************************************************
 * Mandatory TA functions.
 ******************************************************************************/
TEE_Result TA_CreateEntryPoint(void) {
    EMSG("================================================");
    EMSG("=== VERAISON TA LOADED ===");
    EMSG("=== Build: %s %s ===", __DATE__, __TIME__);
    EMSG("================================================");
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
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
    return;
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *sess_ctx, uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    EMSG("[veraison-ta] InvokeCommand cmd_id=%u param_types=0x%x", cmd_id, param_types);

    switch (cmd_id) {
    case TA_VERAISON_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE:
        return call_pta_for_cbor_evidence(param_types, params);

    case TA_VERAISON_ATTESTATION_CMD_GENERATE_BLACKKEY: {
        /* Generate new black key from scratch (CAAM only) */
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_NONE))
            return TEE_ERROR_BAD_PARAMETERS;

        TEE_TASessionHandle sess = TEE_HANDLE_NULL;
        TEE_UUID att_uuid = PTA_REMOTE_ATTESTATION_UUID;
        uint32_t ret_orig = 0;
        TEE_Result res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE,
                                            0, NULL, &sess, &ret_orig);
        if (res != TEE_SUCCESS)
            return res;
        res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                                   PTA_CMD_GENERATE_ECC_KEYPAIR,
                                   param_types, params, &ret_orig);
        TEE_CloseTASession(sess);
        return res;
    }

    case TA_VERAISON_ATTESTATION_CMD_CONVERT_TO_BLACKKEY: {
        /* Convert plain key to black key (CAAM only) */
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                           TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                           TEE_PARAM_TYPE_NONE,
                                           TEE_PARAM_TYPE_NONE))
            return TEE_ERROR_BAD_PARAMETERS;

        TEE_TASessionHandle sess = TEE_HANDLE_NULL;
        TEE_UUID att_uuid = PTA_REMOTE_ATTESTATION_UUID;
        uint32_t ret_orig = 0;
        TEE_Result res = TEE_OpenTASession(&att_uuid, TEE_TIMEOUT_INFINITE,
                                            0, NULL, &sess, &ret_orig);
        if (res != TEE_SUCCESS)
            return res;
        res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                                   PTA_CMD_CONVERT_TO_BLACKKEY,
                                   param_types, params, &ret_orig);
        TEE_CloseTASession(sess);
        return res;
    }

    default:
        return TEE_ERROR_BAD_PARAMETERS;
    }
}
