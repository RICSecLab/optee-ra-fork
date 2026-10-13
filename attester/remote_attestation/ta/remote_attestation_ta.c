#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <inttypes.h>
#include <string.h>

#include <remote_attestation_ta.h>

#ifdef CFG_REMOTE_ATTESTATION_PERF
/*
 * Performance measurement logging (CFG_REMOTE_ATTESTATION_PERF=y).
 * User TAs have no access to a high-resolution counter, so TA-level
 * durations come from TEE_GetSystemTime and are accurate to ~1 ms only.
 * Reported in microseconds for consistency with the PTA/host log lines:
 *   RA_PERF|ta|<event>|<duration_us>|key=pta
 */
static uint64_t ra_perf_now_us(void) {
    TEE_Time t = {};

    TEE_GetSystemTime(&t);
    return ((uint64_t)t.seconds * 1000 + t.millis) * 1000;
}
#endif

/* The implementation-id claim, 32 bytes without the terminating NUL */
static const char implementation_id[] = RA_IMPLEMENTATION_ID;

static TEE_Result open_pta_session(TEE_TASessionHandle *sess) {
    TEE_UUID pta_uuid = PTA_VERAISON_ATTESTATION_UUID;
    uint32_t ret_orig = 0;
    TEE_Result res = TEE_ERROR_GENERIC;

    res = TEE_OpenTASession(&pta_uuid, TEE_TIMEOUT_INFINITE, 0, NULL, sess,
                            &ret_orig);
    if (res != TEE_SUCCESS)
        EMSG("TEE_OpenTASession failed: 0x%" PRIx32, res);
    return res;
}

static TEE_Result call_pta_for_cbor_evidence(uint32_t param_types,
                                             TEE_Param params[4]) {
    TEE_TASessionHandle sess = TEE_HANDLE_NULL;
    TEE_Result res = TEE_ERROR_GENERIC;
    uint32_t ret_orig = 0;
    uint8_t *nonce_buf = NULL;
    uint8_t *out_buf = NULL;
    size_t nonce_len = 0;
    size_t out_len = 0;
    uint32_t pta_param_types = 0;
    TEE_Param pta_params[4] = {};

#ifdef CFG_REMOTE_ATTESTATION_PERF
    uint64_t perf_cmd_start = ra_perf_now_us();
    uint64_t perf_invoke_start = 0;
    uint64_t perf_invoke_us = 0;
#endif

    /*
     * Host → TA param layout:
     *   [0] nonce (INPUT)
     *   [1] evidence (OUTPUT; INOUT accepted for older clients)
     */
    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE) &&
        param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_INOUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE))
        return TEE_ERROR_BAD_PARAMETERS;

    if (!params[0].memref.buffer || !params[0].memref.size ||
        !params[1].memref.buffer || !params[1].memref.size)
        return TEE_ERROR_BAD_PARAMETERS;

    nonce_len = params[0].memref.size;
    out_len = params[1].memref.size;

    nonce_buf = TEE_Malloc(nonce_len, 0);
    out_buf = TEE_Malloc(out_len, 0);
    if (!nonce_buf || !out_buf) {
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto cleanup_return;
    }
    TEE_MemMove(nonce_buf, params[0].memref.buffer, nonce_len);

    res = open_pta_session(&sess);
    if (res != TEE_SUCCESS)
        goto cleanup_return;

    /* PTA layout: nonce, evidence, implementation-id */
    pta_params[0].memref.buffer = nonce_buf;
    pta_params[0].memref.size = nonce_len;
    pta_params[1].memref.buffer = out_buf;
    pta_params[1].memref.size = out_len;
    pta_params[2].memref.buffer = (void *)implementation_id;
    pta_params[2].memref.size = sizeof(implementation_id) - 1;
    pta_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                      TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                      TEE_PARAM_TYPE_MEMREF_INPUT,
                                      TEE_PARAM_TYPE_NONE);

#ifdef CFG_REMOTE_ATTESTATION_PERF
    perf_invoke_start = ra_perf_now_us();
#endif
    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
                              PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE,
                              pta_param_types, pta_params, &ret_orig);
#ifdef CFG_REMOTE_ATTESTATION_PERF
    perf_invoke_us = ra_perf_now_us() - perf_invoke_start;
#endif
    TEE_CloseTASession(sess);
    if (res != TEE_SUCCESS) {
        EMSG("GET_CBOR_EVIDENCE failed: 0x%" PRIx32 " origin %" PRIu32, res,
             ret_orig);
        goto cleanup_return;
    }

    if (pta_params[1].memref.size > out_len) {
        res = TEE_ERROR_SHORT_BUFFER;
        goto cleanup_return;
    }
    TEE_MemMove(params[1].memref.buffer, out_buf, pta_params[1].memref.size);
    params[1].memref.size = pta_params[1].memref.size;

cleanup_return:
    TEE_Free(out_buf);
    TEE_Free(nonce_buf);
#ifdef CFG_REMOTE_ATTESTATION_PERF
    /* Durations measured before printing; ~1 ms resolution (see above) */
    if (perf_invoke_us) {
        uint64_t perf_cmd_us = ra_perf_now_us() - perf_cmd_start;

        IMSG("RA_PERF|ta|pta_invoke|%" PRIu64 "|key=pta", perf_invoke_us);
        IMSG("RA_PERF|ta|cmd_total|%" PRIu64 "|key=pta", perf_cmd_us);
    }
#endif
    return res;
}

/* Forward a key command to the PTA as is, after checking its layout */
static TEE_Result forward_key_command(uint32_t pta_cmd,
                                      uint32_t expected_param_types,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    TEE_TASessionHandle sess = TEE_HANDLE_NULL;
    TEE_Result res = TEE_ERROR_GENERIC;
    uint32_t ret_orig = 0;

    if (param_types != expected_param_types)
        return TEE_ERROR_BAD_PARAMETERS;

    res = open_pta_session(&sess);
    if (res != TEE_SUCCESS)
        return res;

    res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE, pta_cmd,
                              param_types, params, &ret_orig);
    TEE_CloseTASession(sess);
    return res;
}

/*******************************************************************************
 * Mandatory TA functions.
 ******************************************************************************/
TEE_Result TA_CreateEntryPoint(void) {
    /* Nothing to do */
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
    switch (cmd_id) {
    case TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE:
        return call_pta_for_cbor_evidence(param_types, params);

    case TA_REMOTE_ATTESTATION_CMD_IMPORT_KEY:
        return forward_key_command(
            PTA_VERAISON_ATTESTATION_IMPORT_KEY,
            TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                            TEE_PARAM_TYPE_MEMREF_INPUT,
                            TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_NONE),
            param_types, params);

    case TA_REMOTE_ATTESTATION_CMD_GET_PUBLIC_KEY:
        return forward_key_command(
            PTA_VERAISON_ATTESTATION_GET_PUBLIC_KEY,
            TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                            TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE),
            param_types, params);

    default:
        return TEE_ERROR_BAD_PARAMETERS;
    }
}
