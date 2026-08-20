// SPDX-License-Identifier: BSD-2-Clause
/*
 * Minimal i.MX uSDHC driver for OP-TEE core.
 *
 * Scope is deliberately narrow: enough of the controller and of the eMMC
 * protocol to move RPMB frames, so that secure storage does not depend on
 * tee-supplicant (and therefore on Linux user space) being up.
 *
 * Register layout and reset sequence follow U-Boot's fsl_esdhc_imx driver.
 */

#include <drivers/imx_usdhc.h>
#include <imx.h>
#include <imx-regs.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/delay.h>
#include <mm/core_memprot.h>
#include <string.h>
#include <trace.h>
#include <util.h>

register_phys_mem_pgdir(MEM_AREA_IO_SEC, CFG_IMX_USDHC_BASE,
			CORE_MMU_PGDIR_SIZE);
register_phys_mem_pgdir(MEM_AREA_IO_SEC, IOMUXC_BASE, CORE_MMU_PGDIR_SIZE);

/* Controller registers (offsets from the uSDHC base) */
#define USDHC_DSADDR		0x00
#define USDHC_BLKATTR		0x04
#define USDHC_CMDARG		0x08
#define USDHC_XFERTYP		0x0c
#define USDHC_CMDRSP0		0x10
#define USDHC_CMDRSP1		0x14
#define USDHC_CMDRSP2		0x18
#define USDHC_CMDRSP3		0x1c
#define USDHC_DATPORT		0x20
#define USDHC_PRSSTAT		0x24
#define USDHC_PROCTL		0x28
#define USDHC_SYSCTL		0x2c
#define USDHC_IRQSTAT		0x30
#define USDHC_IRQSTATEN		0x34
#define USDHC_IRQSIGEN		0x38
#define USDHC_HOSTCAPBLT	0x40
#define USDHC_WML		0x44
#define USDHC_MIXCTRL		0x48
#define USDHC_VENDORSPEC	0xc0

#define BLKATTR_BLKCNT_SHIFT	16

#define XFERTYP_CMDINX_SHIFT	24
#define XFERTYP_CMDTYP_ABORT	SHIFT_U32(3, 22)
#define XFERTYP_DPSEL		BIT32(21)
#define XFERTYP_CICEN		BIT32(20)
#define XFERTYP_CCCEN		BIT32(19)
#define XFERTYP_RSPTYP_NONE	0
#define XFERTYP_RSPTYP_136	SHIFT_U32(1, 16)
#define XFERTYP_RSPTYP_48	SHIFT_U32(2, 16)
#define XFERTYP_RSPTYP_48_BUSY	SHIFT_U32(3, 16)

#define MIXCTRL_MSBSEL		BIT32(5)
#define MIXCTRL_DTDSEL		BIT32(4)
#define MIXCTRL_DDR_EN		BIT32(3)
#define MIXCTRL_AC12EN		BIT32(2)
#define MIXCTRL_BCEN		BIT32(1)
#define MIXCTRL_DMAEN		BIT32(0)

#define PRSSTAT_CIHB		BIT32(0)
#define PRSSTAT_CDIHB		BIT32(1)
#define PRSSTAT_DLA		BIT32(2)
#define PRSSTAT_BREN		BIT32(11)
#define PRSSTAT_BWEN		BIT32(10)

#define SYSCTL_INITA		BIT32(27)
#define SYSCTL_RSTA		BIT32(24)
#define SYSCTL_RSTC		BIT32(25)
#define SYSCTL_RSTD		BIT32(26)
#define SYSCTL_CKEN		BIT32(3)
#define SYSCTL_PEREN		BIT32(2)
#define SYSCTL_HCKEN		BIT32(1)
#define SYSCTL_IPGEN		BIT32(0)
#define SYSCTL_CLOCK_MASK	0x0000fff0
#define SYSCTL_TIMEOUT_MASK	0x000f0000

#define IRQSTAT_CC		BIT32(0)
#define IRQSTAT_TC		BIT32(1)
#define IRQSTAT_BWR		BIT32(4)
#define IRQSTAT_BRR		BIT32(5)
#define IRQSTAT_CTOE		BIT32(16)
#define IRQSTAT_CCE		BIT32(17)
#define IRQSTAT_CEBE		BIT32(18)
#define IRQSTAT_CIE		BIT32(19)
#define IRQSTAT_DTOE		BIT32(20)
#define IRQSTAT_DCE		BIT32(21)
#define IRQSTAT_DEBE		BIT32(22)
#define IRQSTAT_ERROR		(IRQSTAT_CTOE | IRQSTAT_CCE | IRQSTAT_CEBE | \
				 IRQSTAT_CIE | IRQSTAT_DTOE | IRQSTAT_DCE | \
				 IRQSTAT_DEBE)

#define VENDORSPEC_CKEN		BIT32(14)
#define VENDORSPEC_PEREN	BIT32(13)
#define VENDORSPEC_HCKEN	BIT32(12)
#define VENDORSPEC_IPGEN	BIT32(11)
#define VENDORSPEC_FRC_SDCLK_ON	BIT32(8)

/* eMMC commands (JEDEC JESD84) */
#define MMC_CMD_GO_IDLE_STATE		0
#define MMC_CMD_SEND_OP_COND		1
#define MMC_CMD_ALL_SEND_CID		2
#define MMC_CMD_SET_RELATIVE_ADDR	3
#define MMC_CMD_SWITCH			6
#define MMC_CMD_SELECT_CARD		7
#define MMC_CMD_SEND_EXT_CSD		8
#define MMC_CMD_SEND_CSD		9
#define MMC_CMD_SET_BLOCKLEN		16
#define MMC_CMD_READ_MULTIPLE_BLOCK	18
#define MMC_CMD_SET_BLOCK_COUNT		23
#define MMC_CMD_WRITE_MULTIPLE_BLOCK	25

#define MMC_OCR_BUSY			BIT32(31)
#define MMC_OCR_SECTOR_MODE		BIT32(30)
#define MMC_OCR_VOLTAGE_MASK		0x00ff8000

/* EXT_CSD fields we care about */
#define EXT_CSD_PART_CONF		179
#define EXT_CSD_RPMB_MULT		168
#define EXT_CSD_SIZE			512

#define EXT_CSD_PART_ACCESS_MASK	0x7
#define EXT_CSD_PART_ACCESS_RPMB	0x3

#define CMD_TIMEOUT_US			1000000
#define DATA_TIMEOUT_US			5000000
#define OCR_TIMEOUT_US			2000000

struct usdhc_ctx {
	vaddr_t base;
	uint32_t rca;
	uint8_t rpmb_mult;
	uint8_t cur_part;
	bool inited;
};

static struct usdhc_ctx usdhc_ctx = {
	.rca = 1,
	.cur_part = 0xff,
};

struct mmc_cmd {
	uint16_t idx;
	uint32_t arg;
	uint32_t xfertyp;
	uint32_t resp[4];
	void *data;
	size_t blocks;
	bool write;
};

static vaddr_t usdhc_base(void)
{
	if (!usdhc_ctx.base)
		usdhc_ctx.base = core_mmu_get_va(CFG_IMX_USDHC_BASE,
						 MEM_AREA_IO_SEC, 0x10000);
	return usdhc_ctx.base;
}

/*
 * Pad configuration for uSDHC3, mirroring the pinctrl_usdhc3 group of the
 * i.MX8MP EVK device tree. The core runs before U-Boot proper, which is
 * where these pads would otherwise be muxed, so the driver has to do it.
 *
 * Each entry is { mux register, config register, input select register,
 * mux mode, input value } with the pad settings the device tree applies.
 */
struct pad_cfg {
	uint16_t mux_reg;
	uint16_t conf_reg;
	uint16_t input_reg;
	uint8_t mux_mode;
	uint8_t input_val;
	uint16_t conf_val;
};

static const struct pad_cfg usdhc3_pads[] = {
	{ 0x124, 0x384, 0x604, 0x2, 0x1, 0x190 },	/* CLK */
	{ 0x128, 0x388, 0x60c, 0x2, 0x1, 0x1d0 },	/* CMD */
	{ 0x108, 0x368, 0x610, 0x2, 0x1, 0x1d0 },	/* DATA0 */
	{ 0x10c, 0x36c, 0x614, 0x2, 0x1, 0x1d0 },	/* DATA1 */
	{ 0x110, 0x370, 0x618, 0x2, 0x1, 0x1d0 },	/* DATA2 */
	{ 0x114, 0x374, 0x61c, 0x2, 0x1, 0x1d0 },	/* DATA3 */
	{ 0x11c, 0x37c, 0x620, 0x2, 0x1, 0x1d0 },	/* DATA4 */
	{ 0x0ec, 0x34c, 0x624, 0x2, 0x1, 0x1d0 },	/* DATA5 */
	{ 0x0f0, 0x350, 0x628, 0x2, 0x1, 0x1d0 },	/* DATA6 */
	{ 0x0f4, 0x354, 0x62c, 0x2, 0x1, 0x1d0 },	/* DATA7 */
};

static TEE_Result usdhc_pads_configure(void)
{
	vaddr_t iomux = core_mmu_get_va(IOMUXC_BASE, MEM_AREA_IO_SEC, 0x10000);
	size_t i = 0;

	if (!iomux) {
		EMSG("IOMUXC not mapped");
		return TEE_ERROR_GENERIC;
	}

	for (i = 0; i < ARRAY_SIZE(usdhc3_pads); i++) {
		const struct pad_cfg *p = usdhc3_pads + i;

		io_write32(iomux + p->mux_reg, p->mux_mode);
		io_write32(iomux + p->conf_reg, p->conf_val);
		if (p->input_reg)
			io_write32(iomux + p->input_reg, p->input_val);
	}

	return TEE_SUCCESS;
}

/*
 * Ungate the controller clock and point its root at the 24 MHz oscillator.
 * Linux is not running yet at this point, but the boot loader may have left
 * the clock gated: touching the registers in that state faults the core.
 */
static TEE_Result usdhc_clock_enable(void)
{
	vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);

	if (!ccm) {
		EMSG("CCM not mapped");
		return TEE_ERROR_GENERIC;
	}

	/* Root clock: enable, source 0 (24 MHz osc), no pre/post divider */
	io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, BIT32(28));

	/* Ungate the peripheral in all power domains */
	io_write32(ccm + CCM_CCGRx_SET(CFG_IMX_USDHC_CCM_CCGR), 0xffffffff);

	return TEE_SUCCESS;
}

static TEE_Result wait_bits_clear(vaddr_t reg, uint32_t mask, uint32_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);

	while (io_read32(reg) & mask)
		if (timeout_elapsed(tref))
			return TEE_ERROR_BUSY;

	return TEE_SUCCESS;
}

static TEE_Result wait_irq(vaddr_t base, uint32_t mask, uint32_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);
	uint32_t stat = 0;

	do {
		stat = io_read32(base + USDHC_IRQSTAT);

		if (stat & IRQSTAT_ERROR) {
			DMSG("uSDHC error, IRQSTAT 0x%08"PRIx32, stat);
			io_write32(base + USDHC_IRQSTAT, stat);
			return TEE_ERROR_COMMUNICATION;
		}

		if ((stat & mask) == mask) {
			io_write32(base + USDHC_IRQSTAT, mask);
			return TEE_SUCCESS;
		}
	} while (!timeout_elapsed(tref));

	EMSG("timeout waiting for IRQSTAT %#"PRIx32", have %#"PRIx32
	     " PRSSTAT %#"PRIx32, mask, stat,
	     io_read32(base + USDHC_PRSSTAT));

	return TEE_ERROR_BUSY;
}

/*
 * Wait until the controller's data buffer is ready for the next block.
 *
 * The buffer-ready bits in PRSSTAT are the reliable signal in PIO mode; the
 * matching IRQSTAT flags depend on watermark behaviour and are not a
 * dependable trigger here (this cost one board test to learn).
 */
static TEE_Result wait_buffer_ready(vaddr_t base, bool write)
{
	uint32_t mask = write ? PRSSTAT_BWEN : PRSSTAT_BREN;
	uint64_t tref = timeout_init_us(DATA_TIMEOUT_US);
	uint32_t stat = 0;

	do {
		if (io_read32(base + USDHC_PRSSTAT) & mask)
			return TEE_SUCCESS;

		stat = io_read32(base + USDHC_IRQSTAT);
		if (stat & IRQSTAT_ERROR) {
			EMSG("data error, IRQSTAT %#"PRIx32, stat);
			io_write32(base + USDHC_IRQSTAT, stat);
			return TEE_ERROR_COMMUNICATION;
		}
	} while (!timeout_elapsed(tref));

	EMSG("data buffer never became ready, PRSSTAT %#"PRIx32
	     " IRQSTAT %#"PRIx32, io_read32(base + USDHC_PRSSTAT), stat);

	return TEE_ERROR_BUSY;
}

/* PIO transfer: the RPMB path moves a handful of 512-byte blocks. */
static TEE_Result xfer_data(vaddr_t base, struct mmc_cmd *cmd)
{
	uint32_t *p = cmd->data;
	size_t per_block = IMX_USDHC_BLOCK_SIZE / sizeof(uint32_t);
	size_t blk = 0;
	size_t i = 0;
	TEE_Result res = TEE_SUCCESS;

	/*
	 * The buffer-ready bit only promises one watermark worth of words, so
	 * it has to be re-checked as the block is drained rather than once per
	 * block: reading past what the FIFO holds stalls the transfer and the
	 * completion interrupt never arrives.
	 */
	for (blk = 0; blk < cmd->blocks; blk++) {
		for (i = 0; i < per_block; i++) {
			res = wait_buffer_ready(base, cmd->write);
			if (res)
				return res;

			if (cmd->write)
				io_write32(base + USDHC_DATPORT, *p++);
			else
				*p++ = io_read32(base + USDHC_DATPORT);
		}
	}

	return wait_irq(base, IRQSTAT_TC, DATA_TIMEOUT_US);
}

static TEE_Result send_cmd(struct mmc_cmd *cmd)
{
	vaddr_t base = usdhc_base();
	uint32_t mixctrl = 0;
	TEE_Result res = TEE_SUCCESS;

	res = wait_bits_clear(base + USDHC_PRSSTAT,
			      PRSSTAT_CIHB | PRSSTAT_CDIHB | PRSSTAT_DLA,
			      CMD_TIMEOUT_US);
	if (res)
		return res;

	io_write32(base + USDHC_IRQSTAT, 0xffffffff);

	if (cmd->data) {
		io_write32(base + USDHC_BLKATTR,
			   SHIFT_U32(cmd->blocks, BLKATTR_BLKCNT_SHIFT) |
			   IMX_USDHC_BLOCK_SIZE);
		/*
		 * Watermarks in words. Reads are capped at 16 on this
		 * controller; writes take the full 128-word block.
		 */
		io_write32(base + USDHC_WML, SHIFT_U32(0x80, 16) | 0x10);

		mixctrl = MIXCTRL_BCEN;
		if (cmd->blocks > 1)
			mixctrl |= MIXCTRL_MSBSEL;
		if (!cmd->write)
			mixctrl |= MIXCTRL_DTDSEL;

		io_write32(base + USDHC_MIXCTRL,
			   (io_read32(base + USDHC_MIXCTRL) & ~0x3f) | mixctrl);
	}

	io_write32(base + USDHC_CMDARG, cmd->arg);
	io_write32(base + USDHC_XFERTYP,
		   SHIFT_U32(cmd->idx, XFERTYP_CMDINX_SHIFT) | cmd->xfertyp);

	res = wait_irq(base, IRQSTAT_CC, CMD_TIMEOUT_US);
	if (res)
		return res;

	cmd->resp[0] = io_read32(base + USDHC_CMDRSP0);
	cmd->resp[1] = io_read32(base + USDHC_CMDRSP1);
	cmd->resp[2] = io_read32(base + USDHC_CMDRSP2);
	cmd->resp[3] = io_read32(base + USDHC_CMDRSP3);

	if (cmd->data)
		return xfer_data(base, cmd);

	return TEE_SUCCESS;
}

static void set_clock(vaddr_t base, uint32_t divisor, uint32_t prescaler)
{
	uint32_t sysctl = io_read32(base + USDHC_SYSCTL);

	io_clrbits32(base + USDHC_VENDORSPEC, VENDORSPEC_CKEN);

	sysctl &= ~(SYSCTL_CLOCK_MASK | SYSCTL_TIMEOUT_MASK);
	sysctl |= SHIFT_U32(prescaler, 8) | SHIFT_U32(divisor, 4) |
		  SHIFT_U32(14, 16);
	io_write32(base + USDHC_SYSCTL, sysctl);

	udelay(100);

	io_setbits32(base + USDHC_VENDORSPEC,
		     VENDORSPEC_CKEN | VENDORSPEC_PEREN | VENDORSPEC_HCKEN |
		     VENDORSPEC_IPGEN | VENDORSPEC_FRC_SDCLK_ON);
}

static TEE_Result reset_controller(vaddr_t base)
{
	TEE_Result res = TEE_SUCCESS;

	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RSTA);
	res = wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RSTA,
			      CMD_TIMEOUT_US);
	if (res)
		return res;

	io_write32(base + USDHC_MIXCTRL, 0);
	io_write32(base + USDHC_VENDORSPEC, 0x20007809);
	io_write32(base + USDHC_IRQSTATEN, 0xffffffff);
	io_write32(base + USDHC_IRQSIGEN, 0);
	io_write32(base + USDHC_PROCTL, 0x00000020);

	/* Identification speed: 24 MHz / 64 = 375 kHz */
	set_clock(base, 0, 0x20);

	io_setbits32(base + USDHC_SYSCTL, SYSCTL_INITA);
	res = wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_INITA,
			      CMD_TIMEOUT_US);

	return res;
}

static TEE_Result card_identify(void)
{
	struct mmc_cmd cmd = { };
	uint64_t tref = 0;
	TEE_Result res = TEE_SUCCESS;

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_GO_IDLE_STATE, .arg = 0,
				.xfertyp = XFERTYP_RSPTYP_NONE };
	res = send_cmd(&cmd);
	if (res) {
		EMSG("CMD0 (go idle) failed, PRSSTAT %#"PRIx32,
		     io_read32(usdhc_base() + USDHC_PRSSTAT));
		return res;
	}
	IMSG("uSDHC: CMD0 ok");

	mdelay(2);

	tref = timeout_init_us(OCR_TIMEOUT_US);
	do {
		cmd = (struct mmc_cmd){ .idx = MMC_CMD_SEND_OP_COND,
					.arg = MMC_OCR_SECTOR_MODE |
					       MMC_OCR_VOLTAGE_MASK,
					.xfertyp = XFERTYP_RSPTYP_48 };
		res = send_cmd(&cmd);
		if (res) {
			EMSG("CMD1 (send op cond) failed, PRSSTAT %#"PRIx32,
			     io_read32(usdhc_base() + USDHC_PRSSTAT));
			return res;
		}

		if (cmd.resp[0] & MMC_OCR_BUSY)
			break;

		if (timeout_elapsed(tref)) {
			EMSG("eMMC stayed busy, last OCR %#"PRIx32,
			     cmd.resp[0]);
			return TEE_ERROR_BUSY;
		}
		mdelay(1);
	} while (true);

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_ALL_SEND_CID, .arg = 0,
				.xfertyp = XFERTYP_RSPTYP_136 | XFERTYP_CCCEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	IMSG("eMMC CID %08"PRIx32"%08"PRIx32"%08"PRIx32"%08"PRIx32,
	     cmd.resp[3], cmd.resp[2], cmd.resp[1], cmd.resp[0]);

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_RELATIVE_ADDR,
				.arg = SHIFT_U32(usdhc_ctx.rca, 16),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	/* Identification done: 24 MHz / 2 = 12 MHz for the data phases */
	set_clock(usdhc_base(), 0, 0x01);

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SELECT_CARD,
				.arg = SHIFT_U32(usdhc_ctx.rca, 16),
				.xfertyp = XFERTYP_RSPTYP_48_BUSY |
					   XFERTYP_CCCEN | XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_BLOCKLEN,
				.arg = IMX_USDHC_BLOCK_SIZE,
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };

	return send_cmd(&cmd);
}

static TEE_Result read_ext_csd(uint8_t *ext_csd)
{
	struct mmc_cmd cmd = { .idx = MMC_CMD_SEND_EXT_CSD, .arg = 0,
			       .xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					  XFERTYP_CICEN | XFERTYP_DPSEL,
			       .data = ext_csd, .blocks = 1, .write = false };

	return send_cmd(&cmd);
}

static TEE_Result switch_partition(uint8_t part)
{
	struct mmc_cmd cmd = { };
	uint8_t ext_csd[EXT_CSD_SIZE] = { };
	uint8_t value = 0;
	TEE_Result res = TEE_SUCCESS;

	if (usdhc_ctx.cur_part == part)
		return TEE_SUCCESS;

	res = read_ext_csd(ext_csd);
	if (res)
		return res;

	value = (ext_csd[EXT_CSD_PART_CONF] & ~EXT_CSD_PART_ACCESS_MASK) |
		(part & EXT_CSD_PART_ACCESS_MASK);

	/* SWITCH: write byte, index PART_CONF, value, cmd set 0 */
	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SWITCH,
				.arg = SHIFT_U32(3, 24) |
				       SHIFT_U32(EXT_CSD_PART_CONF, 16) |
				       SHIFT_U32(value, 8),
				.xfertyp = XFERTYP_RSPTYP_48_BUSY |
					   XFERTYP_CCCEN | XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	usdhc_ctx.cur_part = part;

	return TEE_SUCCESS;
}

static TEE_Result rpmb_xfer(void *buf, size_t nblocks, bool write)
{
	struct mmc_cmd cmd = { };
	TEE_Result res = TEE_SUCCESS;

	res = imx_usdhc_init();
	if (res)
		return res;

	res = switch_partition(EXT_CSD_PART_ACCESS_RPMB);
	if (res)
		return res;

	/*
	 * SET_BLOCK_COUNT with bit 31 set marks a reliable write, which the
	 * RPMB protocol requires for authenticated data writes.
	 */
	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_BLOCK_COUNT,
				.arg = nblocks | (write ? BIT32(31) : 0),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	cmd = (struct mmc_cmd){ .idx = write ? MMC_CMD_WRITE_MULTIPLE_BLOCK :
					       MMC_CMD_READ_MULTIPLE_BLOCK,
				.arg = 0,
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN | XFERTYP_DPSEL,
				.data = buf, .blocks = nblocks,
				.write = write };

	return send_cmd(&cmd);
}

TEE_Result imx_usdhc_init(void)
{
	uint8_t ext_csd[EXT_CSD_SIZE] = { };
	TEE_Result res = TEE_SUCCESS;

	if (usdhc_ctx.inited)
		return TEE_SUCCESS;

	if (!usdhc_base()) {
		EMSG("uSDHC registers not mapped");
		return TEE_ERROR_GENERIC;
	}

	res = usdhc_pads_configure();
	if (res)
		return res;

	res = usdhc_clock_enable();
	if (res)
		return res;

	res = reset_controller(usdhc_base());
	if (res) {
		EMSG("uSDHC controller reset failed: %#"PRIx32, res);
		return res;
	}

	res = card_identify();
	if (res) {
		EMSG("eMMC identification failed: %#"PRIx32, res);
		return res;
	}

	res = read_ext_csd(ext_csd);
	if (res) {
		EMSG("EXT_CSD read failed: %#"PRIx32, res);
		return res;
	}

	usdhc_ctx.rpmb_mult = ext_csd[EXT_CSD_RPMB_MULT];
	usdhc_ctx.inited = true;

	IMSG("uSDHC eMMC ready, RPMB size %u KiB",
	     usdhc_ctx.rpmb_mult * 128);

	return TEE_SUCCESS;
}

TEE_Result imx_usdhc_rpmb_size(uint8_t *mult)
{
	TEE_Result res = imx_usdhc_init();

	if (res)
		return res;

	*mult = usdhc_ctx.rpmb_mult;

	return TEE_SUCCESS;
}

TEE_Result imx_usdhc_rpmb_read(void *buf, size_t nblocks)
{
	return rpmb_xfer(buf, nblocks, false);
}

TEE_Result imx_usdhc_rpmb_write(const void *buf, size_t nblocks)
{
	return rpmb_xfer((void *)buf, nblocks, true);
}
