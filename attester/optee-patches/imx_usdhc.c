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
#include <arm.h>
#include <io.h>
#include <kernel/boot.h>
#include <kernel/cache_helpers.h>
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
/* Holds SMP_CLK_SEL/EXE_TUNE on i.MX8M; set by HS200 tuning, kept by RSTA */
#define USDHC_AUTOCMD12_ERR	0x3c
#define USDHC_HOSTCAPBLT	0x40
#define USDHC_WML		0x44
#define USDHC_MIXCTRL		0x48
#define USDHC_DLLCTRL		0x60
#define USDHC_CLKTUNECTRL	0x68
#define USDHC_STROBE_DLL_CTRL	0x70
#define USDHC_STROBE_DLL_STAT	0x74
#define USDHC_VENDSPEC2		0xc8
#define USDHC_TUNING_CTRL	0xcc
#define TUNING_CTRL_STD_EN	BIT32(24)
#define STROBE_DLL_CTRL_RESET	BIT32(1)
#define USDHC_VENDORSPEC	0xc0
#define USDHC_MMCBOOT		0xc4

#define BLKATTR_BLKCNT_SHIFT	16

/* Watermark fields; the rest of the register holds the burst lengths. */
#define WML_RD_MASK		0x000000ff
#define WML_WR_MASK		0x00ff0000

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
/* DAT0 line level: the card pulls it low while it is programming. */
#define PRSSTAT_DAT0_LEVEL	BIT32(24)

#define SYSCTL_INITA		BIT32(27)
#define SYSCTL_RSTA		BIT32(24)
#define SYSCTL_RSTT		BIT32(28)	/* reset tuning */
#define SYSCTL_RST_FIFO		BIT32(22)
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
/* Same bit positions in IRQSTATEN */
#define IRQSTATEN_BWR		BIT32(4)
#define IRQSTATEN_BRR		BIT32(5)
#define IRQSTAT_BRR		BIT32(5)
#define IRQSTAT_CTOE		BIT32(16)
#define IRQSTAT_CCE		BIT32(17)
#define IRQSTAT_CEBE		BIT32(18)
#define IRQSTAT_CIE		BIT32(19)
#define IRQSTAT_DTOE		BIT32(20)
#define IRQSTAT_DCE		BIT32(21)
#define IRQSTAT_DEBE		BIT32(22)
#define IRQSTAT_DMAE		BIT32(28)
/* DMA finished moving the data (DINT) */
#define IRQSTAT_DINT		BIT32(3)
#define IRQSTAT_ERROR		(IRQSTAT_CTOE | IRQSTAT_CCE | IRQSTAT_CEBE | \
				 IRQSTAT_CIE | IRQSTAT_DTOE | IRQSTAT_DCE | \
				 IRQSTAT_DEBE | IRQSTAT_DMAE)

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
#define MMC_CMD_SEND_STATUS		13
#define MMC_CMD_READ_SINGLE_BLOCK	17
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
#define EXT_CSD_REL_WR_SEC_C		222
#define EXT_CSD_SIZE			512

#define EXT_CSD_PART_ACCESS_MASK	0x7
#define EXT_CSD_PART_ACCESS_RPMB	0x3
#define EXT_CSD_PART_ACCESS_USER	0x0

#define CMD_TIMEOUT_US			1000000
#define DATA_TIMEOUT_US			15000000
#define OCR_TIMEOUT_US			2000000

struct usdhc_ctx {
	vaddr_t base;
	uint32_t rca;
	uint8_t rpmb_mult;
	uint8_t rel_wr_sec_c;
	uint8_t part_conf;
	/* SYSCTL divider bits this driver programmed, to spot outside changes */
	uint32_t clock_bits;
	uint8_t cid[IMX_USDHC_CID_SIZE];
	uint8_t cur_part;
	bool inited;
};

/*
 * All DMA goes through this driver-owned buffer: page aligned, in core
 * memory the controller (a secure master) can reach, and independent of
 * where callers keep their frames.
 */
#define DMA_BOUNCE_BLOCKS	32
static uint8_t dma_bounce[DMA_BOUNCE_BLOCKS * IMX_USDHC_BLOCK_SIZE]
	__aligned(4096);

/*
 * Diagnostic fallback: after a DMA data phase fails, move the data with
 * the CPU through the data port (U-Boot's esdhc_pio_read_write) so a
 * bus-side problem can be told apart from a card-side one.
 */
static bool pio_mode;

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
	/* Only wait for the command line; used to query a wedged transfer. */
	bool status_only;
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
/*
 * The controller's bus side (data buffer and DMA) runs on the shared
 * NAND/uSDHC bus root. Nothing in Linux uses that root once uSDHC3 is
 * disabled in its device tree, so its clock framework switches the root
 * off after boot (clk_disable_unused): commands still answer on the SD
 * clock, but every data phase then stalls. This controller is owned by
 * the TEE, so the TEE keeps its bus clock alive.
 */
#define CCM_NAND_USDHC_BUS_ROOT	0x8900
#define CCM_ROOT_ENABLE		BIT32(28)
/* CCGRn plain (read) register; the SET alias comes from the platform headers */
#define CCM_CCGRn(n)		(0x4000 + (n) * 0x10)

static void usdhc_bus_clock_ensure(vaddr_t ccm, const char *when)
{
	uint32_t v = io_read32(ccm + CCM_NAND_USDHC_BUS_ROOT);
	uint32_t r = io_read32(ccm + CFG_IMX_USDHC_CCM_TARGET);
	uint32_t g = io_read32(ccm + CCM_CCGRn(CFG_IMX_USDHC_CCM_CCGR));
	bool fix = false;

	if (!(v & CCM_ROOT_ENABLE)) {
		IMSG("uSDHC bus clock root was off (%#"PRIx32") at %s, enabling",
		     v, when);
		io_write32(ccm + CCM_NAND_USDHC_BUS_ROOT, v | CCM_ROOT_ENABLE);
		fix = true;
	}
	if (!(r & CCM_ROOT_ENABLE)) {
		IMSG("uSDHC clock root was off (%#"PRIx32") at %s, enabling",
		     r, when);
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, r | CCM_ROOT_ENABLE);
		fix = true;
	}
	if ((g & 0x3) != 0x3) {
		IMSG("uSDHC clock gate was %#"PRIx32" at %s, opening", g, when);
		io_write32(ccm + CCM_CCGRx_SET(CFG_IMX_USDHC_CCM_CCGR), 0xffffffff);
		fix = true;
	}
	if (fix)
		udelay(10);
}

static TEE_Result usdhc_clock_enable(void)
{
	vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);

	if (!ccm) {
		EMSG("CCM not mapped");
		return TEE_ERROR_GENERIC;
	}

	/* Root clock: enable, source 0 (24 MHz osc), no pre/post divider */
	io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, BIT32(28));
	IMSG("uSDHC CCM root: wrote %#"PRIx32", reads %#"PRIx32, BIT32(28),
	     io_read32(ccm + CFG_IMX_USDHC_CCM_TARGET));

	/* Ungate the peripheral in all power domains */
	io_write32(ccm + CCM_CCGRx_SET(CFG_IMX_USDHC_CCM_CCGR), 0xffffffff);

	usdhc_bus_clock_ensure(ccm, "init");

	return TEE_SUCCESS;
}

static TEE_Result wait_bits_clear(vaddr_t reg, uint32_t mask, uint32_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);

	while (io_read32(reg) & mask) {
		if (timeout_elapsed(tref)) {
			EMSG("bits %#"PRIx32" stuck, register holds %#"PRIx32,
			     mask, io_read32(reg));
			return TEE_ERROR_BUSY;
		}
	}

	return TEE_SUCCESS;
}

static uint64_t measure_sdclk_khz(vaddr_t base);

static void dump_regs(vaddr_t base, const char *tag)
{
	IMSG("uSDHC %s: PRSSTAT %#"PRIx32" IRQSTAT %#"PRIx32" MIXCTRL %#"PRIx32
	     " BLKATTR %#"PRIx32" WML %#"PRIx32, tag,
	     io_read32(base + USDHC_PRSSTAT), io_read32(base + USDHC_IRQSTAT),
	     io_read32(base + USDHC_MIXCTRL), io_read32(base + USDHC_BLKATTR),
	     io_read32(base + USDHC_WML));
	IMSG("uSDHC %s: PROCTL %#"PRIx32" SYSCTL %#"PRIx32" VENDSPEC %#"PRIx32
	     " DSADDR %#"PRIx32" IRQSTATEN %#"PRIx32, tag,
	     io_read32(base + USDHC_PROCTL), io_read32(base + USDHC_SYSCTL),
	     io_read32(base + USDHC_VENDORSPEC), io_read32(base + USDHC_DSADDR),
	     io_read32(base + USDHC_IRQSTATEN));
	IMSG("uSDHC %s: DLLCTRL %#"PRIx32" CLKTUNE %#"PRIx32" STROBEDLL %#"PRIx32
	     "/%#"PRIx32" VENDSPEC2 %#"PRIx32" TUNINGCTRL %#"PRIx32, tag,
	     io_read32(base + USDHC_DLLCTRL), io_read32(base + USDHC_CLKTUNECTRL),
	     io_read32(base + USDHC_STROBE_DLL_CTRL),
	     io_read32(base + USDHC_STROBE_DLL_STAT),
	     io_read32(base + USDHC_VENDSPEC2), io_read32(base + USDHC_TUNING_CTRL));
	IMSG("uSDHC %s: AUTOCMD12_ERR %#"PRIx32" (bit23 SMP_CLK_SEL, bit22 EXE_TUNE)",
	     tag, io_read32(base + USDHC_AUTOCMD12_ERR));
	{
		vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);
		vaddr_t iomux = core_mmu_get_va(IOMUXC_BASE, MEM_AREA_IO_SEC, 0x10000);

		if (ccm)
			IMSG("uSDHC %s: CCM usdhc3_root %#"PRIx32" nand_usdhc_bus %#"PRIx32
			     " ipg %#"PRIx32" CCGR83 %#"PRIx32" CCGR94 %#"PRIx32, tag,
			     io_read32(ccm + 0xbc80), io_read32(ccm + 0x8900),
			     io_read32(ccm + 0x9080), io_read32(ccm + 0x4530),
			     io_read32(ccm + 0x45e0));
		if (iomux)
			IMSG("uSDHC %s: IOMUX CLK mux/pad %#"PRIx32"/%#"PRIx32
			     " CMD %#"PRIx32"/%#"PRIx32" D0 %#"PRIx32"/%#"PRIx32, tag,
			     io_read32(iomux + 0x124), io_read32(iomux + 0x384),
			     io_read32(iomux + 0x128), io_read32(iomux + 0x388),
			     io_read32(iomux + 0x108), io_read32(iomux + 0x368));
	}
}

static TEE_Result wait_irq(vaddr_t base, uint32_t mask, uint32_t timeout)
{
	uint64_t tref = timeout_init_us(timeout);
	uint32_t stat = 0;

	do {
		stat = io_read32(base + USDHC_IRQSTAT);

		if (stat & IRQSTAT_ERROR) {
			EMSG("uSDHC error, IRQSTAT %#"PRIx32" PRSSTAT %#"PRIx32, stat,
			     io_read32(base + USDHC_PRSSTAT));
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
	dump_regs(base, "at command timeout");

	return TEE_ERROR_BUSY;
}


/*
 * DMA transfer, as U-Boot's fsl_esdhc_imx does it: the data phase is over
 * when both the transfer-complete and the DMA-complete flags are set. On
 * this controller DINT means "DMA finished", not a page-boundary pause, and
 * writing DSADDR while a transfer runs restarts the engine, so the address
 * register is never touched here.
 */
static TEE_Result xfer_data(vaddr_t base, struct mmc_cmd *cmd __unused)
{
	uint64_t tref = timeout_init_us(DATA_TIMEOUT_US);
	const uint32_t done = IRQSTAT_TC | IRQSTAT_DINT;
	uint32_t stat = 0;

	do {
		stat = io_read32(base + USDHC_IRQSTAT);

		if (stat & IRQSTAT_ERROR) {
			EMSG("data error, IRQSTAT %#"PRIx32, stat);
			io_write32(base + USDHC_IRQSTAT, stat);
			return TEE_ERROR_COMMUNICATION;
		}

		if ((stat & done) == done) {
			io_write32(base + USDHC_IRQSTAT, done);
			return TEE_SUCCESS;
		}
	} while (!timeout_elapsed(tref));

	EMSG("transfer did not complete, IRQSTAT %#"PRIx32" PRSSTAT %#"PRIx32,
	     stat, io_read32(base + USDHC_PRSSTAT));

	return TEE_ERROR_BUSY;
}

/* CPU-driven data phase through the data port, one 512-byte block at a time. */
static TEE_Result xfer_data_pio(vaddr_t base, struct mmc_cmd *cmd)
{
	uint64_t tref = timeout_init_us(DATA_TIMEOUT_US);
	uint32_t *buf = cmd->data;
	size_t blocks = cmd->blocks;
	uint32_t ready = cmd->write ? PRSSTAT_BWEN : PRSSTAT_BREN;
	uint32_t stat = 0;

	/* Words the buffer flag vouches for: the watermark levels set below */
	const size_t chunk = cmd->write ? 0x80 : 0x10;

	while (blocks) {
		size_t words = IMX_USDHC_BLOCK_SIZE / 4;

		while (words) {
			size_t n = words < chunk ? words : chunk;

			while (!(io_read32(base + USDHC_PRSSTAT) & ready)) {
				stat = io_read32(base + USDHC_IRQSTAT);
				if (stat & IRQSTAT_ERROR) {
					EMSG("PIO data error, IRQSTAT %#"PRIx32, stat);
					return TEE_ERROR_COMMUNICATION;
				}
				if (timeout_elapsed(tref)) {
					EMSG("PIO buffer never ready, PRSSTAT %#"PRIx32
					     " IRQSTAT %#"PRIx32,
					     io_read32(base + USDHC_PRSSTAT), stat);
					return TEE_ERROR_BUSY;
				}
			}
			words -= n;
			while (n--) {
				if (cmd->write)
					io_write32(base + USDHC_DATPORT, *buf++);
				else
					*buf++ = io_read32(base + USDHC_DATPORT);
			}
		}
		blocks--;
	}

	do {
		stat = io_read32(base + USDHC_IRQSTAT);
		if (stat & IRQSTAT_ERROR) {
			EMSG("PIO data error after data, IRQSTAT %#"PRIx32, stat);
			return TEE_ERROR_COMMUNICATION;
		}
		if (stat & IRQSTAT_TC) {
			io_write32(base + USDHC_IRQSTAT, IRQSTAT_TC);
			return TEE_SUCCESS;
		}
	} while (!timeout_elapsed(tref));

	EMSG("PIO transfer did not complete, IRQSTAT %#"PRIx32" PRSSTAT %#"PRIx32,
	     stat, io_read32(base + USDHC_PRSSTAT));
	return TEE_ERROR_BUSY;
}

/*
 * Clear a wedged transfer. Without this one failure leaves the command and
 * data lines inhibited, so every later command fails as well and the real
 * cause is buried under the fallout.
 */
static void reset_transfer(vaddr_t base)
{
	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RSTC | SYSCTL_RSTD);
	wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RSTC | SYSCTL_RSTD,
			CMD_TIMEOUT_US);
	io_write32(base + USDHC_IRQSTAT, 0xffffffff);
}

/* Where the time goes, averaged over the last 50 transfers (microseconds) */
static struct {
	uint64_t t_init, t_switch, t_ready, t_cmd23;
	uint64_t t_data_rd, t_data_wr;
	unsigned int n, n_rd, n_wr;
} xfer_stats;

/* Split of a command's cost: bus-inhibit wait vs. response wait */
static uint64_t cmd_inhibit_us, cmd_cc_us;

static uint64_t us_since(uint64_t t0)
{
	return (read_cntpct() - t0) * 1000000ULL / read_cntfrq();
}

/*
 * Issue a command that needs the command line only (SEND_STATUS) while a
 * data transfer may still be wedged: skip the data-inhibit wait, and do not
 * reset anything, so the controller state under inspection is preserved.
 */
static TEE_Result send_status_only(struct mmc_cmd *cmd)
{
	vaddr_t base = usdhc_base();
	TEE_Result res = TEE_SUCCESS;

	res = wait_bits_clear(base + USDHC_PRSSTAT, PRSSTAT_CIHB,
			      CMD_TIMEOUT_US);
	if (res)
		return res;

	io_write32(base + USDHC_IRQSTAT, IRQSTAT_CC | IRQSTAT_CTOE |
		   IRQSTAT_CCE | IRQSTAT_CEBE | IRQSTAT_CIE);
	io_write32(base + USDHC_CMDARG, cmd->arg);
	io_write32(base + USDHC_XFERTYP,
		   SHIFT_U32(cmd->idx, XFERTYP_CMDINX_SHIFT) | cmd->xfertyp);

	res = wait_irq(base, IRQSTAT_CC, CMD_TIMEOUT_US);
	if (res)
		return res;

	cmd->resp[0] = io_read32(base + USDHC_CMDRSP0);

	return TEE_SUCCESS;
}

static TEE_Result send_cmd(struct mmc_cmd *cmd)
{
	vaddr_t base = usdhc_base();
	uint32_t mixctrl = 0;
	TEE_Result res = TEE_SUCCESS;

	uint64_t tc0 = read_cntpct();

	if (cmd->status_only)
		return send_status_only(cmd);

	res = wait_bits_clear(base + USDHC_PRSSTAT,
			      PRSSTAT_CIHB | PRSSTAT_CDIHB | PRSSTAT_DLA,
			      CMD_TIMEOUT_US);
	if (cmd->idx == MMC_CMD_SET_BLOCK_COUNT) {
		cmd_inhibit_us += us_since(tc0);
		tc0 = read_cntpct();
	}
	if (res) {
		reset_transfer(base);
		res = wait_bits_clear(base + USDHC_PRSSTAT,
				      PRSSTAT_CIHB | PRSSTAT_CDIHB |
				      PRSSTAT_DLA, CMD_TIMEOUT_US);
	}
	if (res)
		return res;

	io_write32(base + USDHC_IRQSTAT, 0xffffffff);

	if (cmd->data) {
		paddr_t pa = virt_to_phys(dma_bounce);
		size_t len = cmd->blocks * IMX_USDHC_BLOCK_SIZE;

		if (!pa) {
			EMSG("transfer buffer has no physical address");
			return TEE_ERROR_GENERIC;
		}
		if (len > sizeof(dma_bounce)) {
			EMSG("transfer of %zu blocks exceeds the DMA buffer",
			     cmd->blocks);
			return TEE_ERROR_EXCESS_DATA;
		}

		/*
		 * The controller reads and writes memory directly, so the
		 * caches have to be settled around it: clean what is about to
		 * be sent, and drop stale lines over the destination.
		 */
		if (cmd->write) {
			memcpy(dma_bounce, cmd->data, len);
			dcache_clean_range(dma_bounce, len);
		} else {
			dcache_inv_range(dma_bounce, len);
		}

		io_write32(base + USDHC_DSADDR, pa);
		io_write32(base + USDHC_BLKATTR,
			   SHIFT_U32(cmd->blocks, BLKATTR_BLKCNT_SHIFT) |
			   IMX_USDHC_BLOCK_SIZE);
		/*
		 * Watermarks in words: reads are capped at 16 on this
		 * controller, writes take the full 128-word block.
		 *
		 * Only those two fields may be touched. The same register
		 * carries the burst lengths, and writing it whole leaves them
		 * at zero - the controller then accepts data into its buffer
		 * but never puts it on the bus, so a write never completes.
		 */
		io_clrsetbits32(base + USDHC_WML, WML_RD_MASK | WML_WR_MASK,
				SHIFT_U32(0x80, 16) | 0x10);

		/*
		 * Multi-block select follows the command, not the block
		 * count: RPMB moves single frames with the multiple-block
		 * commands, and a write left in single-block mode never
		 * completes - the controller keeps the transfer active while
		 * the device sits idle.
		 */
		mixctrl = pio_mode ? 0 : MIXCTRL_DMAEN;
		if (cmd->blocks > 1)
			mixctrl |= MIXCTRL_MSBSEL | MIXCTRL_BCEN;
		if (!cmd->write)
			mixctrl |= MIXCTRL_DTDSEL;

		/*
		 * Write the whole register: the upper bits select DDR,
		 * HS400 and tuning modes, none of which apply to legacy
		 * single-data-rate transfers. Preserving them carried
		 * whatever the register held at boot into every transfer.
		 */
		io_write32(base + USDHC_MIXCTRL, mixctrl);
		if (io_read32(base + USDHC_MIXCTRL) != mixctrl)
			EMSG("MIXCTRL wrote %#"PRIx32" reads %#"PRIx32,
			     mixctrl, io_read32(base + USDHC_MIXCTRL));
	}

	if (IS_ENABLED(CFG_IMX_USDHC_VERBOSE) && cmd->data && cmd->write)
		dump_regs(base, "before write cmd");

	io_write32(base + USDHC_CMDARG, cmd->arg);
	io_write32(base + USDHC_XFERTYP,
		   SHIFT_U32(cmd->idx, XFERTYP_CMDINX_SHIFT) | cmd->xfertyp);

	res = wait_irq(base, IRQSTAT_CC, CMD_TIMEOUT_US);
	if (res)
		return res;
	if (cmd->idx == MMC_CMD_SET_BLOCK_COUNT)
		cmd_cc_us += us_since(tc0);

	if (IS_ENABLED(CFG_IMX_USDHC_VERBOSE) && cmd->data && cmd->write)
		dump_regs(base, "after cmd complete");

	cmd->resp[0] = io_read32(base + USDHC_CMDRSP0);
	cmd->resp[1] = io_read32(base + USDHC_CMDRSP1);
	cmd->resp[2] = io_read32(base + USDHC_CMDRSP2);
	cmd->resp[3] = io_read32(base + USDHC_CMDRSP3);

	if (cmd->data) {
		res = pio_mode ? xfer_data_pio(base, cmd) : xfer_data(base, cmd);
		if (!res && !cmd->write && !pio_mode) {
			size_t len = cmd->blocks * IMX_USDHC_BLOCK_SIZE;

			dcache_inv_range(dma_bounce, len);
			memcpy(cmd->data, dma_bounce, len);
		}
		if (res) {
			struct mmc_cmd st = {
				.idx = MMC_CMD_SEND_STATUS,
				.arg = SHIFT_U32(usdhc_ctx.rca, 16),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN,
			};

			EMSG("CMD%"PRIu16" data phase failed, response %#"
			     PRIx32, cmd->idx, cmd->resp[0]);
			dump_regs(base, "at data failure");

			/*
			 * Ask the device first, with the controller untouched:
			 * this is its true state while the transfer is wedged.
			 */
			st.status_only = true;
			if (!send_cmd(&st))
				EMSG("card state before reset %"PRIu32
				     ", status %#"PRIx32,
				     (st.resp[0] >> 9) & 0xf, st.resp[0]);
			else
				EMSG("card state query before reset failed");
			st.status_only = false;

			reset_transfer(base);
			if (!pio_mode) {
				IMSG("uSDHC: DMA data phase failed, switching to PIO");
				pio_mode = true;
			}
			/* Start from scratch next time: card and controller */
			usdhc_ctx.inited = false;
			usdhc_ctx.cur_part = 0xff;

			/*
			 * Ask the device where it ended up. State 4 means it
			 * never started receiving, 6 that it is still waiting
			 * for data, 7 that it is programming what it got.
			 */
			if (!send_cmd(&st))
				EMSG("card now in state %"PRIu32
				     ", status %#"PRIx32,
				     (st.resp[0] >> 9) & 0xf, st.resp[0]);
		}
		return res;
	}

	return TEE_SUCCESS;
}

static void set_clock(vaddr_t base, uint32_t divisor, uint32_t prescaler)
{
	uint32_t sysctl = 0;
	uint32_t want = 0;
	uint64_t tref = 0;

	/*
	 * Stop only the card clock while the divider changes (U-Boot's
	 * set_sysctl). Gating the module's ipg/hclk enables as well leaves
	 * the divider logic unclocked during the write: the register reads
	 * back but the SD clock stays at the reset divider, /256.
	 */
	io_clrbits32(base + USDHC_VENDORSPEC, VENDORSPEC_CKEN);

	sysctl = io_read32(base + USDHC_SYSCTL);
	sysctl &= ~(SYSCTL_CLOCK_MASK | SYSCTL_TIMEOUT_MASK);
	want = SHIFT_U32(prescaler, 8) | SHIFT_U32(divisor, 4);
	sysctl |= want | SHIFT_U32(14, 16);
	io_write32(base + USDHC_SYSCTL, sysctl);

	/* Wait for the internal clock to settle on the new divider */
	tref = timeout_init_us(CMD_TIMEOUT_US);
	while (!(io_read32(base + USDHC_PRSSTAT) & BIT32(3))) {
		if (timeout_elapsed(tref)) {
			EMSG("uSDHC clock never stabilised, PRSSTAT %#"PRIx32,
			     io_read32(base + USDHC_PRSSTAT));
			break;
		}
	}

	io_setbits32(base + USDHC_VENDORSPEC,
		     VENDORSPEC_CKEN | VENDORSPEC_PEREN | VENDORSPEC_HCKEN |
		     VENDORSPEC_IPGEN | VENDORSPEC_FRC_SDCLK_ON);

	usdhc_ctx.clock_bits = want;
	sysctl = io_read32(base + USDHC_SYSCTL);
	IMSG("uSDHC clock: divider wanted %#"PRIx32", SYSCTL now %#"PRIx32
	     "%s", want, sysctl,
	     ((sysctl & SYSCTL_CLOCK_MASK) == want) ? "" : " (MISMATCH)");
}

static TEE_Result reset_controller(vaddr_t base)
{
	TEE_Result res = TEE_SUCCESS;

	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RSTA | SYSCTL_RSTT);
	res = wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RSTA | SYSCTL_RSTT,
			      CMD_TIMEOUT_US);
	if (res)
		return res;

	/* As U-Boot: RSTA leaves these untouched, clear them by hand */
	io_write32(base + USDHC_MMCBOOT, 0);
	io_write32(base + USDHC_MIXCTRL, 0);
	io_write32(base + USDHC_CLKTUNECTRL, 0);
	io_write32(base + USDHC_VENDORSPEC,
		   0x20007809 | (io_read32(base + USDHC_VENDORSPEC) & BIT32(1)));
	io_write32(base + USDHC_DLLCTRL, 0);
	/* HS400 strobe DLL and HS200 standard tuning: off for legacy timing */
	io_write32(base + USDHC_STROBE_DLL_CTRL, STROBE_DLL_CTRL_RESET);
	udelay(10);
	io_write32(base + USDHC_STROBE_DLL_CTRL, 0);
	io_clrbits32(base + USDHC_TUNING_CTRL, TUNING_CTRL_STD_EN);
	/*
	 * As Linux (sdhci-esdhc-imx probe and esdhc_reset_tuning): the
	 * sample-clock select and execute-tuning bits live here on i.MX8M
	 * and survive RSTA. Left set after a HS200 tuning run they make
	 * the receiver sample with the tuned delay clock, which garbles
	 * every response at legacy speed.
	 */
	io_write32(base + USDHC_AUTOCMD12_ERR, 0);
	io_setbits32(base + USDHC_SYSCTL, SYSCTL_RST_FIFO);
	wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_RST_FIFO, CMD_TIMEOUT_US);
	/* W1C on BRR clears the IP's execute_tuning_with_clr_buf flag */
	io_write32(base + USDHC_IRQSTAT, IRQSTAT_BRR);
	/* Buffer-ready flags are polled in PRSSTAT, keep them out of IRQSTAT */
	io_write32(base + USDHC_IRQSTATEN,
		   0xffffffff & ~(IRQSTATEN_BRR | IRQSTATEN_BWR));
	io_write32(base + USDHC_IRQSIGEN, 0);
	io_write32(base + USDHC_PROCTL, 0x00000020);

	/* Identification speed: 24 MHz / 64 = 375 kHz */
	set_clock(base, 0, 0x20);

	io_setbits32(base + USDHC_SYSCTL, SYSCTL_INITA);
	res = wait_bits_clear(base + USDHC_SYSCTL, SYSCTL_INITA,
			      CMD_TIMEOUT_US);

	if (IS_ENABLED(CFG_IMX_USDHC_VERBOSE))
		dump_regs(base, "after controller init");

	return res;
}

static TEE_Result card_identify(void)
{
	struct mmc_cmd cmd = { };
	uint64_t tref = 0;
	size_t i = 0;
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

		if (cmd.resp[0] == 0xffffffff) {
			EMSG("CMD1 response reads all ones, PRSSTAT %#"PRIx32,
			     io_read32(usdhc_base() + USDHC_PRSSTAT));
			return TEE_ERROR_COMMUNICATION;
		}
		if (cmd.resp[0] & MMC_OCR_BUSY) {
			IMSG("uSDHC: OCR %#"PRIx32, cmd.resp[0]);
			break;
		}

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

	/*
	 * The controller strips the CRC byte, so the response holds the CID
	 * shifted right by 8 bits. Rebuild the 16-byte CID the RPMB layer
	 * compares against, most significant byte first.
	 */
	for (i = 0; i < 4; i++) {
		uint32_t w = cmd.resp[3 - i];

		usdhc_ctx.cid[i * 4] = w >> 24;
		usdhc_ctx.cid[i * 4 + 1] = w >> 16;
		usdhc_ctx.cid[i * 4 + 2] = w >> 8;
		usdhc_ctx.cid[i * 4 + 3] = w;
	}

	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_RELATIVE_ADDR,
				.arg = SHIFT_U32(usdhc_ctx.rca, 16),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;

	/*
	 * Identification done. The measured SD clock with SDCLKFS=0x01 was
	 * ~92 kHz (24 MHz / 256), not the 12 MHz the register description
	 * suggests, so run the data phases undivided: 24 MHz is within the
	 * 26 MHz legacy-timing limit of the device.
	 */
	set_clock(usdhc_base(), 0, 0);
	IMSG("uSDHC SD clock measured: %"PRIu64" kHz", measure_sdclk_khz(usdhc_base()));

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
	uint8_t value = 0;
	TEE_Result res = TEE_SUCCESS;

	if (usdhc_ctx.cur_part == part)
		return TEE_SUCCESS;

	/*
	 * Use the partition configuration captured at init rather than
	 * re-reading EXT_CSD here: that read is a data transfer of its own,
	 * and issuing one while the device sits on the RPMB partition only
	 * gives the data line more chances to wedge.
	 */
	value = (usdhc_ctx.part_conf & ~EXT_CSD_PART_ACCESS_MASK) |
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

	/* Bit 7 of the status reports a rejected SWITCH. */
	if (cmd.resp[0] & BIT32(7)) {
		EMSG("partition switch rejected, card status %#"PRIx32,
		     cmd.resp[0]);
		return TEE_ERROR_GENERIC;
	}

	usdhc_ctx.cur_part = part;

	return TEE_SUCCESS;
}

/* Wait for the card to release DAT0, which it holds low while programming. */
static TEE_Result wait_card_ready(void)
{
	vaddr_t base = usdhc_base();
	uint64_t tref = timeout_init_us(DATA_TIMEOUT_US);

	do {
		if (io_read32(base + USDHC_PRSSTAT) & PRSSTAT_DAT0_LEVEL)
			return TEE_SUCCESS;
	} while (!timeout_elapsed(tref));

	EMSG("card stayed busy, PRSSTAT %#"PRIx32,
	     io_read32(base + USDHC_PRSSTAT));

	return TEE_ERROR_BUSY;
}


static TEE_Result rpmb_xfer(void *buf, size_t nblocks, bool write,
			    bool reliable)
{
	struct mmc_cmd cmd = { };
	TEE_Result res = TEE_SUCCESS;
	uint64_t t0 = read_cntpct();

	res = imx_usdhc_init();
	if (res)
		return res;
	xfer_stats.t_init += us_since(t0); t0 = read_cntpct();

	res = switch_partition(EXT_CSD_PART_ACCESS_RPMB);
	if (res)
		return res;
	xfer_stats.t_switch += us_since(t0); t0 = read_cntpct();

	res = wait_card_ready();
	if (res)
		return res;
	xfer_stats.t_ready += us_since(t0); t0 = read_cntpct();

	/*
	 * Bit 31 of SET_BLOCK_COUNT marks a reliable write. It belongs on
	 * authenticated data writes and on key programming only: request
	 * frames that ask the device for something are plain writes, and
	 * marking those reliable makes the device ignore the transfer.
	 */
	/*
	 * Mark every RPMB write reliable. The spec reserves the flag for
	 * authenticated writes, but this device leaves a plain write sitting
	 * in the controller and never takes the data.
	 */
	cmd = (struct mmc_cmd){ .idx = MMC_CMD_SET_BLOCK_COUNT,
				.arg = nblocks | (write ? BIT32(31) : 0),
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN };
	res = send_cmd(&cmd);
	if (res)
		return res;
	xfer_stats.t_cmd23 += us_since(t0); t0 = read_cntpct();

	DMSG("RPMB %s: part %u, block count status %#"PRIx32,
	     write ? "write" : "read", usdhc_ctx.cur_part, cmd.resp[0]);

	cmd = (struct mmc_cmd){ .idx = write ? MMC_CMD_WRITE_MULTIPLE_BLOCK :
					       MMC_CMD_READ_MULTIPLE_BLOCK,
				.arg = 0,
				.xfertyp = XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
					   XFERTYP_CICEN | XFERTYP_DPSEL,
				.data = buf, .blocks = nblocks,
				.write = write };

	/*
	 * Stay on the RPMB partition: a request frame and the response that
	 * follows it are one transaction, and switching partitions in between
	 * loses the pending response.
	 */
	res = send_cmd(&cmd);
	if (write) {
		xfer_stats.t_data_wr += us_since(t0);
		xfer_stats.n_wr++;
	} else {
		xfer_stats.t_data_rd += us_since(t0);
		xfer_stats.n_rd++;
	}
	if (++xfer_stats.n == 50) {
		IMSG("uSDHC avg us over 50: init %"PRIu64" switch %"PRIu64
		     " ready %"PRIu64" cmd23 %"PRIu64" (inhibit %"PRIu64
		     " cc %"PRIu64") | data read %"PRIu64" x%u, write %"PRIu64" x%u",
		     xfer_stats.t_init / 50, xfer_stats.t_switch / 50,
		     xfer_stats.t_ready / 50, xfer_stats.t_cmd23 / 50,
		     cmd_inhibit_us / 50, cmd_cc_us / 50,
		     xfer_stats.n_rd ? xfer_stats.t_data_rd / xfer_stats.n_rd : 0,
		     xfer_stats.n_rd,
		     xfer_stats.n_wr ? xfer_stats.t_data_wr / xfer_stats.n_wr : 0,
		     xfer_stats.n_wr);
		memset(&xfer_stats, 0, sizeof(xfer_stats));
		cmd_inhibit_us = 0;
		cmd_cc_us = 0;
	}
	return res;
}

/*
 * U-Boot proper (bootstd scanning every MMC device) and Linux both drive
 * this controller too, and leave it in 8-bit HS400 with their own clock.
 * Any of those differs from what this driver programmed, so compare the
 * registers rather than trusting the cached state.
 */
static bool controller_state_foreign(vaddr_t base)
{
	if (io_read32(base + USDHC_PROCTL) & 0x6)		/* DTW != 1-bit */
		return true;
	if ((io_read32(base + USDHC_SYSCTL) & SYSCTL_CLOCK_MASK) !=
	    usdhc_ctx.clock_bits)
		return true;
	if (io_read32(base + USDHC_MIXCTRL) & 0xffffff00)	/* DDR/HS400/tuning */
		return true;
	return false;
}

TEE_Result imx_usdhc_init(void)
{
	uint8_t ext_csd[EXT_CSD_SIZE] = { };
	TEE_Result res = TEE_SUCCESS;

	if (usdhc_ctx.inited) {
		vaddr_t base = usdhc_base();
		vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);

		if (ccm)
			usdhc_bus_clock_ensure(ccm, "transfer");

		if (!controller_state_foreign(base))
			return TEE_SUCCESS;

		IMSG("uSDHC reconfigured by another master (PROCTL %#"PRIx32
		     " SYSCTL %#"PRIx32" MIXCTRL %#"PRIx32"), re-initialising",
		     io_read32(base + USDHC_PROCTL),
		     io_read32(base + USDHC_SYSCTL),
		     io_read32(base + USDHC_MIXCTRL));
		dump_regs(base, "left by other master");
		usdhc_ctx.inited = false;
		usdhc_ctx.cur_part = 0xff;
	}

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

	IMSG("eMMC EXT_CSD: PARTITION_SWITCH_TIME %u x10ms, GENERIC_CMD6_TIME %u x10ms,"
	     " REL_WR_SEC_C %u", ext_csd[199], ext_csd[248], ext_csd[222]);
	usdhc_ctx.rpmb_mult = ext_csd[EXT_CSD_RPMB_MULT];
	usdhc_ctx.rel_wr_sec_c = ext_csd[EXT_CSD_REL_WR_SEC_C];
	usdhc_ctx.part_conf = ext_csd[EXT_CSD_PART_CONF];
	usdhc_ctx.cur_part = usdhc_ctx.part_conf & EXT_CSD_PART_ACCESS_MASK;
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

/*
 * Once the device is on the RPMB partition it stays there: U-Boot and
 * Linux have the controller disabled in their device trees, so nobody
 * needs the user area back, and every switch costs a CMD6 plus the
 * device's partition switch time.
 */
TEE_Result imx_usdhc_rpmb_done(void)
{
	return TEE_SUCCESS;
}

TEE_Result imx_usdhc_dev_info(uint8_t *cid, uint8_t *rpmb_size_mult,
			      uint8_t *rel_wr_sec_c)
{
	TEE_Result res = imx_usdhc_init();

	if (res)
		return res;

	if (cid)
		memcpy(cid, usdhc_ctx.cid, sizeof(usdhc_ctx.cid));
	if (rpmb_size_mult)
		*rpmb_size_mult = usdhc_ctx.rpmb_mult;
	if (rel_wr_sec_c)
		*rel_wr_sec_c = usdhc_ctx.rel_wr_sec_c;

	return TEE_SUCCESS;
}

TEE_Result imx_usdhc_rpmb_read(void *buf, size_t nblocks)
{
	return rpmb_xfer(buf, nblocks, false, false);
}

TEE_Result imx_usdhc_rpmb_write(const void *buf, size_t nblocks, bool reliable)
{
	TEE_Result res = rpmb_xfer((void *)buf, nblocks, true, reliable);

	if (res)
		return res;

	/*
	 * The device programs the data after the transfer completes; the next
	 * command must not arrive while it is still busy.
	 */
	return wait_card_ready();
}


/*
 * Controller/device micro-benchmark, independent of the RPMB layer: plain
 * commands and single-block reads from the user area (read-only). Prints
 * average microseconds so the cost of a command and of a 512-byte data
 * phase can be attributed to the controller, the clock gating, or the DMA.
 */
static uint64_t bench_cmd13(unsigned int n)
{
	uint64_t t0 = read_cntpct();
	unsigned int i = 0;

	for (i = 0; i < n; i++) {
		struct mmc_cmd st = { .idx = MMC_CMD_SEND_STATUS,
				      .arg = SHIFT_U32(usdhc_ctx.rca, 16),
				      .xfertyp = XFERTYP_RSPTYP_48 |
						 XFERTYP_CCCEN | XFERTYP_CICEN };
		if (send_cmd(&st))
			return 0;
	}
	return us_since(t0) / n;
}

static uint64_t bench_read1(unsigned int n)
{
	static uint8_t buf[IMX_USDHC_BLOCK_SIZE] __aligned(8);
	uint64_t t0 = read_cntpct();
	unsigned int i = 0;

	for (i = 0; i < n; i++) {
		struct mmc_cmd cmd = { .idx = MMC_CMD_READ_SINGLE_BLOCK, .arg = 0,
				       .xfertyp = XFERTYP_RSPTYP_48 |
						  XFERTYP_CCCEN | XFERTYP_CICEN |
						  XFERTYP_DPSEL,
				       .data = buf, .blocks = 1, .write = false };
		if (send_cmd(&cmd))
			return 0;
	}
	return us_since(t0) / n;
}

/* Time 80 SD clock cycles (INITA), return the implied SD clock in kHz */
static uint64_t measure_sdclk_khz(vaddr_t base)
{
	uint64_t t0 = 0;
	uint64_t us = 0;
	uint64_t tref = timeout_init_us(CMD_TIMEOUT_US);

	while (io_read32(base + USDHC_PRSSTAT) & (PRSSTAT_CIHB | PRSSTAT_CDIHB))
		if (timeout_elapsed(tref))
			return 0;
	t0 = read_cntpct();
	io_setbits32(base + USDHC_SYSCTL, SYSCTL_INITA);
	while (io_read32(base + USDHC_SYSCTL) & SYSCTL_INITA)
		if (timeout_elapsed(tref))
			return 0;
	us = us_since(t0);
	return us ? (80 * 1000) / us : 0;
}

void imx_usdhc_benchmark(void)
{
	vaddr_t base = usdhc_base();
	vaddr_t ccm = core_mmu_get_va(CCM_BASE, MEM_AREA_IO_SEC, CCM_SIZE);
	uint64_t a = 0, b = 0;
	uint32_t root = 0;

	if (imx_usdhc_init() || switch_partition(EXT_CSD_PART_ACCESS_USER))
		return;

	IMSG("BENCH: SD clock via INITA: current (SYSCTL %#"PRIx32") = %"PRIu64" kHz",
	     io_read32(base + USDHC_SYSCTL), measure_sdclk_khz(base));
	set_clock(base, 0, 0x80);
	IMSG("BENCH: SDCLKFS=0x80 (/256): %"PRIu64" kHz", measure_sdclk_khz(base));
	set_clock(base, 0, 0x20);
	IMSG("BENCH: SDCLKFS=0x20 (/64):  %"PRIu64" kHz", measure_sdclk_khz(base));
	set_clock(base, 0, 0x01);
	IMSG("BENCH: SDCLKFS=0x01 (/2):   %"PRIu64" kHz", measure_sdclk_khz(base));
	set_clock(base, 1, 0);
	IMSG("BENCH: DVS=1 (/2):          %"PRIu64" kHz", measure_sdclk_khz(base));
	set_clock(base, 0, 0);
	IMSG("BENCH: /1:                  %"PRIu64" kHz", measure_sdclk_khz(base));
	if (ccm) {
		root = io_read32(ccm + CFG_IMX_USDHC_CCM_TARGET);
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, root | 1);	/* POST_PODF /2 */
		udelay(100);
		IMSG("BENCH: CCM root POST_PODF=1 (/2), SYSCTL /1: %"PRIu64" kHz",
		     measure_sdclk_khz(base));
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, root);
		udelay(100);

		/*
		 * U-Boot and Linux never feed this controller from osc_24m:
		 * both select sys_pll1_400m. Try the PLL sources.
		 * MUX bits 26:24, POST_PODF bits 5:0 (divide by n+1).
		 */
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET,
			   CCM_ROOT_ENABLE | SHIFT_U32(1, 24) | 7);	/* 400M/8 = 50 MHz */
		udelay(100);
		set_clock(base, 0, 0);
		IMSG("BENCH: root=sys_pll1_400m/8 (50 MHz), SYSCTL /1: %"PRIu64" kHz",
		     measure_sdclk_khz(base));
		set_clock(base, 0, 0x01);
		IMSG("BENCH: root=sys_pll1_400m/8 (50 MHz), SDCLKFS=0x01: %"PRIu64" kHz",
		     measure_sdclk_khz(base));
		set_clock(base, 1, 0);
		IMSG("BENCH: root=sys_pll1_400m/8 (50 MHz), DVS=1: %"PRIu64" kHz",
		     measure_sdclk_khz(base));
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET,
			   CCM_ROOT_ENABLE | SHIFT_U32(7, 24));	/* sys_pll1_100m */
		udelay(100);
		set_clock(base, 0, 0);
		IMSG("BENCH: root=sys_pll1_100m, SYSCTL /1: %"PRIu64" kHz",
		     measure_sdclk_khz(base));
		set_clock(base, 0, 0x02);
		IMSG("BENCH: root=sys_pll1_100m, SDCLKFS=0x02 (/4): %"PRIu64" kHz",
		     measure_sdclk_khz(base));
		/* back to the 24 MHz source the driver currently uses */
		io_write32(ccm + CFG_IMX_USDHC_CCM_TARGET, root);
		udelay(100);
		set_clock(base, 0, 0);
	}

	IMSG("BENCH: SYSCTL %#"PRIx32" VENDSPEC %#"PRIx32" PRSSTAT %#"PRIx32,
	     io_read32(base + USDHC_SYSCTL), io_read32(base + USDHC_VENDORSPEC),
	     io_read32(base + USDHC_PRSSTAT));
	a = bench_cmd13(20);
	b = bench_read1(10);
	IMSG("BENCH: DMA, FRC_SDCLK_ON=1: CMD13 %"PRIu64" us, read 512B %"PRIu64" us", a, b);

	io_clrbits32(base + USDHC_VENDORSPEC, VENDORSPEC_FRC_SDCLK_ON);
	a = bench_cmd13(20);
	b = bench_read1(10);
	IMSG("BENCH: DMA, FRC_SDCLK_ON=0: CMD13 %"PRIu64" us, read 512B %"PRIu64" us", a, b);
	io_setbits32(base + USDHC_VENDORSPEC, VENDORSPEC_FRC_SDCLK_ON);

	pio_mode = true;
	a = bench_cmd13(20);
	b = bench_read1(10);
	IMSG("BENCH: PIO, FRC_SDCLK_ON=1: CMD13 %"PRIu64" us, read 512B %"PRIu64" us", a, b);
	pio_mode = false;

	/* SYSCTL[3:0] as U-Boot leaves it after RSTA (0x7 -> IPG/HCK/PER enables) */
	io_clrsetbits32(base + USDHC_SYSCTL, 0xf, 0x7);
	a = bench_cmd13(20);
	b = bench_read1(10);
	IMSG("BENCH: DMA, SYSCTL[3:0]=7: CMD13 %"PRIu64" us, read 512B %"PRIu64" us", a, b);

	/* data clock /2 via DVS as Linux programs it */
	set_clock(base, 1, 0);
	a = bench_cmd13(20);
	b = bench_read1(10);
	IMSG("BENCH: DMA, DVS=1 (Linux-style /2): CMD13 %"PRIu64" us, read 512B %"PRIu64" us", a, b);
	set_clock(base, 0, 0);
}
