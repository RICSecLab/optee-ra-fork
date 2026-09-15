# Enable the fTPM TEE driver (tpm_ftpm_tee) for the OP-TEE firmware TPM.
# Gated on the optee-ftpm machine feature so that default builds keep a
# byte-identical kernel configuration.
FILESEXTRAPATHS:prepend := "${THISDIR}/linux-imx:"
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://ftpm.cfg', '', d)}"
# With the fTPM the on-board eMMC belongs to OP-TEE (RPMB secure storage):
# keep Linux off the controller entirely.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0001-imx8mp-evk-leave-usdhc3-to-the-TEE.patch', '', d)}"
do_configure:append() {
    if [ -f ${UNPACKDIR}/ftpm.cfg ]; then
        cat ${UNPACKDIR}/ftpm.cfg >> ${B}/.config
        oe_runmake -C ${S} O=${B} olddefconfig
    fi
}
# The shared NAND/uSDHC bus clock also feeds the TEE-owned eMMC controller.
SRC_URI += "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'file://0002-clk-imx8mp-keep-nand_usdhc_bus-running.patch', '', d)}"
