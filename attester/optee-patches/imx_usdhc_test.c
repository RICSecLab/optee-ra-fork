// SPDX-License-Identifier: BSD-2-Clause
/*
 * Boot-time smoke test for the i.MX uSDHC driver (CFG_IMX_USDHC_TEST=y).
 *
 * Brings up the eMMC from the OP-TEE core and prints what it found, so the
 * driver can be validated before any RPMB traffic depends on it.
 */

#include <drivers/imx_usdhc.h>
#include <initcall.h>
#include <trace.h>

static TEE_Result imx_usdhc_smoke_test(void)
{
	static uint8_t frame[IMX_USDHC_BLOCK_SIZE] __aligned(8);
	uint8_t mult = 0;
	TEE_Result res = TEE_SUCCESS;

	IMSG("USDHC-TEST: probing on-board eMMC");

	res = imx_usdhc_init();
	if (res) {
		EMSG("USDHC-TEST: init failed: %#"PRIx32, res);
		return TEE_SUCCESS;
	}

	res = imx_usdhc_rpmb_size(&mult);
	if (res) {
		EMSG("USDHC-TEST: RPMB size query failed: %#"PRIx32, res);
		return TEE_SUCCESS;
	}

	if (!mult) {
		IMSG("USDHC-TEST: device reports no RPMB partition");
		return TEE_SUCCESS;
	}

	IMSG("USDHC-TEST: eMMC up, RPMB partition %u KiB", mult * 128);

	/*
	 * Read one RPMB frame. Without a programmed authentication key the
	 * device answers with an error inside the frame rather than failing
	 * the transfer, so a clean return here already proves that command
	 * and data phases work against the RPMB partition.
	 */
	res = imx_usdhc_rpmb_read(frame, 1);
	if (res) {
		EMSG("USDHC-TEST: RPMB read failed: %#"PRIx32, res);
		return TEE_SUCCESS;
	}

	IMSG("USDHC-TEST: PASS, RPMB frame read (resp %02x%02x, result %02x%02x)",
	     frame[510], frame[511], frame[508], frame[509]);

	return TEE_SUCCESS;
}

/*
 * Runs late so the console and the MMU mappings the driver needs are
 * already up. Failures are reported but never block boot.
 */
service_init_late(imx_usdhc_smoke_test);
