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
 * Read or write @nblocks of IMX_USDHC_BLOCK_SIZE bytes on the RPMB
 * partition. The caller passes complete RPMB data frames; authentication
 * is done by the RPMB layer, not here.
 */
TEE_Result imx_usdhc_rpmb_read(void *buf, size_t nblocks);
TEE_Result imx_usdhc_rpmb_write(const void *buf, size_t nblocks);

#endif /* __DRIVERS_IMX_USDHC_H */
