#!/usr/bin/env python3
"""Route OP-TEE's RPMB transport through the core uSDHC driver.

The RPMB protocol layer (frame building, HMAC, write counter, FAT) already
lives in the core; only the delivery of frames goes out to tee-supplicant.
This patch adds a compile-time alternative for that delivery so secure
storage works before Linux user space exists.

Idempotent: running it on an already patched tree changes nothing.
"""

import sys

MARKER = "CFG_IMX_RPMB_NATIVE"

INCLUDE_OLD = "#include <kernel/thread.h>\n"
INCLUDE_NEW = """#include <kernel/thread.h>
#if defined(CFG_IMX_RPMB_NATIVE)
#include <drivers/imx_usdhc.h>
#endif
"""

# Buffer allocation: the shared-memory cache talks to the normal world, which
# is not available early in boot, so the native path uses core memory.
ALLOC_OLD = """	if (rpmb_ctx->legacy_operation)
		req_size += sizeof(struct rpmb_req);
	req_s = ROUNDUP(req_size, SMALL_PAGE_SIZE);"""

ALLOC_NEW = """	if (rpmb_ctx->legacy_operation)
		req_size += sizeof(struct rpmb_req);

	if (IS_ENABLED(CFG_IMX_RPMB_NATIVE)) {
		/*
		 * No normal world involved: plain core memory holds the
		 * frames, so this works before Linux is up.
		 *
		 * Callers never release the buffers (with the RPC transport
		 * the shared memory cache owns them), so the previous pair is
		 * released here instead. RPMB operations are serialised, so
		 * at most one pair is ever live.
		 */
		static void *prev_req;
		static void *prev_resp;
		void *req = NULL;
		void *resp = NULL;

		free(prev_req);
		free(prev_resp);
		prev_req = NULL;
		prev_resp = NULL;

		req = calloc(1, req_size);
		resp = calloc(1, resp_size);
		if (!req || !resp) {
			free(req);
			free(resp);
			return TEE_ERROR_OUT_OF_MEMORY;
		}

		prev_req = req;
		prev_resp = resp;

		*mem = (struct tee_rpmb_mem){
			.req_size = req_size,
			.resp_size = resp_size,
			.req_data = req,
			.resp_data = resp,
		};

		return TEE_SUCCESS;
	}

	req_s = ROUNDUP(req_size, SMALL_PAGE_SIZE);"""

# Frame delivery.
INVOKE_OLD = """static TEE_Result tee_rpmb_invoke(struct tee_rpmb_mem *mem)
{
	struct thread_param params[2] = {"""

INVOKE_NEW = """#if defined(CFG_IMX_RPMB_NATIVE)
/*
 * Hand the request frames to the device and read the response back. The
 * eMMC returns the result of a write only when asked for it, so a write is
 * always followed by a result-read frame, which is what the RPMB layer
 * expects to find in the response buffer.
 */
static TEE_Result rpmb_native_invoke(struct tee_rpmb_mem *mem)
{
	size_t req_blocks = mem->req_size / RPMB_DATA_FRAME_SIZE;
	size_t resp_blocks = mem->resp_size / RPMB_DATA_FRAME_SIZE;
	struct rpmb_data_frame *req = mem->req_data;
	uint16_t msg_type = 0;
	TEE_Result res = TEE_SUCCESS;

	if (!req_blocks || !resp_blocks)
		return TEE_ERROR_BAD_PARAMETERS;

	bytes_to_u16(req[0].msg_type, &msg_type);

	switch (msg_type) {
	case RPMB_MSG_TYPE_REQ_AUTH_KEY_PROGRAM:
	case RPMB_MSG_TYPE_REQ_AUTH_DATA_WRITE:
		/* Authenticated writes are the reliable-write case. */
		res = imx_usdhc_rpmb_write(req, req_blocks, true);
		if (res)
			return res;

		/* Ask for the result of the write just issued. */
		memset(mem->resp_data, 0, mem->resp_size);
		u16_to_bytes(RPMB_MSG_TYPE_REQ_RESULT_READ,
			     mem->resp_data[0].msg_type);
		res = imx_usdhc_rpmb_write(mem->resp_data, 1, false);
		if (res)
			return res;

		return imx_usdhc_rpmb_read(mem->resp_data, 1);

	default:
		/* A request frame is a plain write, not a reliable one. */
		res = imx_usdhc_rpmb_write(req, req_blocks, false);
		if (res)
			return res;

		return imx_usdhc_rpmb_read(mem->resp_data, resp_blocks);
	}
}
#endif /* CFG_IMX_RPMB_NATIVE */

static TEE_Result tee_rpmb_invoke(struct tee_rpmb_mem *mem)
{
	struct thread_param params[2] = {"""

INVOKE_BODY_OLD = """	uint32_t cmd = OPTEE_RPC_CMD_RPMB_FRAMES;

	if (rpmb_ctx->legacy_operation)
		cmd = OPTEE_RPC_CMD_RPMB;"""

INVOKE_BODY_NEW = """	uint32_t cmd = OPTEE_RPC_CMD_RPMB_FRAMES;

	if (IS_ENABLED(CFG_IMX_RPMB_NATIVE))
		return rpmb_native_invoke(mem);

	if (rpmb_ctx->legacy_operation)
		cmd = OPTEE_RPC_CMD_RPMB;"""

# Device discovery.
PROBE_RESET_OLD = """static TEE_Result rpmb_probe_reset(void)
{
	struct thread_param params[1] = {
		[0] = THREAD_PARAM_VALUE(OUT, 0, 0, 0),
	};
	TEE_Result res = TEE_SUCCESS;
"""

PROBE_RESET_NEW = """static TEE_Result rpmb_probe_reset(void)
{
	struct thread_param params[1] = {
		[0] = THREAD_PARAM_VALUE(OUT, 0, 0, 0),
	};
	TEE_Result res = TEE_SUCCESS;

#if defined(CFG_IMX_RPMB_NATIVE)
	res = imx_usdhc_init();
	if (res)
		return res;

	rpmb_ctx->legacy_operation = false;
	rpmb_ctx->dev_id = 0;
	rpmb_ctx->shm_type = THREAD_SHM_TYPE_KERNEL_PRIVATE;
	rpmb_ctx->native_probe_done = false;

	return TEE_SUCCESS;
#endif
"""

PROBE_NEXT_OLD = """	struct thread_param params[2] = { };
	TEE_Result res = TEE_SUCCESS;
	struct mobj *mobj = NULL;
	void *va = NULL;
"""

PROBE_NEXT_NEW = """	struct thread_param params[2] = { };
	TEE_Result res = TEE_SUCCESS;
	struct mobj *mobj = NULL;
	void *va = NULL;

#if defined(CFG_IMX_RPMB_NATIVE)
	/*
	 * There is exactly one device behind this driver. The caller walks
	 * the probe until it either finds a usable device or gets an error,
	 * so the second call has to report that the list is exhausted -
	 * otherwise a device that cannot be used (no authentication key
	 * programmed, say) would spin the caller forever.
	 */
	if (rpmb_ctx->native_probe_done)
		return TEE_ERROR_ITEM_NOT_FOUND;

	*dev_info = (struct rpmb_dev_info){
		.ret_code = RPMB_CMD_GET_DEV_INFO_RET_OK,
	};

	res = imx_usdhc_dev_info(dev_info->cid, &dev_info->rpmb_size_mult,
				 &dev_info->rel_wr_sec_c);
	if (!res)
		rpmb_ctx->native_probe_done = true;

	return res;
#endif
"""

# Context flag backing the single-device probe above.
CTX_OLD = """	bool legacy_operation;
"""

CTX_NEW = """	bool legacy_operation;
#if defined(CFG_IMX_RPMB_NATIVE)
	/* The single device behind the core driver has been reported. */
	bool native_probe_done;
#endif
"""

EDITS = [
    (INCLUDE_OLD, INCLUDE_NEW),
    (CTX_OLD, CTX_NEW),
    (ALLOC_OLD, ALLOC_NEW),
    (INVOKE_OLD, INVOKE_NEW),
    (INVOKE_BODY_OLD, INVOKE_BODY_NEW),
    (PROBE_RESET_OLD, PROBE_RESET_NEW),
    (PROBE_NEXT_OLD, PROBE_NEXT_NEW),
]


def main():
    path = sys.argv[1]
    text = open(path).read()

    if MARKER in text:
        print("tee_rpmb_fs.c already patched")
        return

    for old, new in EDITS:
        if old not in text:
            sys.exit("anchor not found in %s:\n%s" % (path, old[:120]))
        text = text.replace(old, new, 1)

    open(path, "w").write(text)
    print("tee_rpmb_fs.c patched for the native RPMB backend")


main()
