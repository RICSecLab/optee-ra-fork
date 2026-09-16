/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Minimal i.MX uSDHC driver for OP-TEE core: eMMC RPMB access without
 * relying on tee-supplicant.
 *
 * Only what RPMB needs: controller bring-up, eMMC identification and
 * 512-byte block transfers on the RPMB partition.
 */
#ifndef __DRIVERS_IMX_USDHC_H
#define __DRIVERS_IMX_USDHC_H

#include <stdbool.h>
#include <stdint.h>
#include <tee_api_types.h>

#define IMX_USDHC_BLOCK_SIZE	512U

/*
 * Bring up the controller and the attached eMMC device.
 * Idempotent: later calls return TEE_SUCCESS without touching the device.
 */
TEE_Result imx_usdhc_init(void);

/* Size of the RPMB partition in 128 KiB units, as reported by the device. */
TEE_Result imx_usdhc_rpmb_size(uint8_t *mult);

/*
 * Identification of the attached device, in the form the RPMB layer needs:
 * the raw CID, the RPMB size multiplier (EXT_CSD 168) and the reliable
 * write sector count (EXT_CSD 222). Any output pointer may be NULL.
 */
#define IMX_USDHC_CID_SIZE	16

TEE_Result imx_usdhc_dev_info(uint8_t *cid, uint8_t *rpmb_size_mult,
			      uint8_t *rel_wr_sec_c);

/*
 * Read or write @nblocks of IMX_USDHC_BLOCK_SIZE bytes on the RPMB
 * partition. The caller passes complete RPMB data frames; authentication
 * is done by the RPMB layer, not here.
 */
TEE_Result imx_usdhc_rpmb_read(void *buf, size_t nblocks);

/*
 * @reliable marks the transfer as a reliable write, which the RPMB
 * specification requires for authenticated data writes and for programming
 * the key. The current driver issues every RPMB write as a reliable write
 * (see rpmb_xfer()); the flag records the caller's intent.
 */
TEE_Result imx_usdhc_rpmb_write(const void *buf, size_t nblocks,
				bool reliable);

/*
 * Called once a request and its response have both been exchanged. The
 * device is left on the RPMB partition (nothing else uses the eMMC), so
 * this is currently a no-op kept for the transaction boundary.
 */
TEE_Result imx_usdhc_rpmb_done(void);

/* Boot-time controller micro-benchmark (read-only on the device) */
void imx_usdhc_benchmark(void);

#endif /* __DRIVERS_IMX_USDHC_H */
