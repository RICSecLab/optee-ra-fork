# OP-TEE OS of the image: NXP's 4.6.0.imx tree plus the attestation PTA work
# of this project. The PTA is OP-TEE's own core/pta/veraison_attestation with
# the series submitted upstream (https://github.com/OP-TEE/optee_os/pull/8109,
# patches 0001-0009, and its follow-up, 0010-0012): the PTA generates its
# ECDSA P-256 signing key on first use and keeps it in its secure storage (a
# CAAM black key with the CAAM ECC driver), or installs a key imported before
# that, and never replaces it; it takes the lifecycle and the boot signer from
# the i.MX fuses, identifies the signer of the calling TA and reports the TEE
# core as a software component. The NXP tree is OP-TEE 4.6.0 for the files
# these patches touch, so the upstream commits apply as they are, except 0002
# whose key type check is placed in NXP's version of the function.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI += " \
    file://0001-pta-veraison_attestation-free-the-COSE-work-buffers.patch \
    file://0002-drivers-caam-do-not-assert-on-short-key-buffers.patch \
    file://0003-drivers-caam-wipe-the-plain-key-after-black-key-enca.patch \
    file://0004-pta-veraison_attestation-keep-the-signing-key-in-sec.patch \
    file://0005-pta-veraison_attestation-wrap-an-imported-key-in-the.patch \
    file://0006-pta-veraison_attestation-take-lifecycle-and-boot-sig.patch \
    file://0007-pta-veraison_attestation-identify-the-signer-of-the-.patch \
    file://0008-pta-veraison_attestation-derive-the-client-id-from-t.patch \
    file://0009-pta-veraison_attestation-document-the-key-the-claims.patch \
    file://0010-pta-veraison_attestation-label-the-TA-component-ARoT.patch \
    file://0011-pta-veraison_attestation-report-the-TEE-core-as-a-so.patch \
    file://0012-pta-veraison_attestation-document-the-software-compo.patch \
"

# The PTA with its key in secure storage (CFG_REE_FS or CFG_RPMB_FS, both
# available here) instead of the build-time test key, and the platform claims
# from the fuses. CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y come from local.conf
# (yocto.sh) and make the key a CAAM black key.
EXTRA_OEMAKE:append = " CFG_VERAISON_ATTESTATION_PTA=y CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=n CFG_VERAISON_ATTESTATION_PTA_PLATFORM_CLAIMS=y"

# Core uSDHC driver and native RPMB backend (attester/optee-patches): OP-TEE
# reaches the on-board eMMC RPMB without tee-supplicant, so the fTPM can keep
# its state there and start before Linux (IMA then finds a TPM). Private
# storage moves entirely to RPMB (CFG_REE_FS=n) because a REE FS would need
# the supplicant during kernel init. Gated on the optee-ftpm machine feature.
USDHC_PATCHES_SRC ?= "/attester/optee-patches"
EXTRA_OEMAKE:append = "${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', ' CFG_IMX_USDHC=y CFG_IMX_RPMB_NATIVE=y CFG_RPMB_FS=y CFG_RPMB_WRITE_KEY=y CFG_REE_FS=n', '', d)}"

do_configure:append() {
    if ${@bb.utils.contains('MACHINE_FEATURES', 'optee-ftpm', 'true', 'false', d)}; then
        if [ ! -x "${USDHC_PATCHES_SRC}/apply-usdhc.sh" ]; then
            bbfatal "uSDHC driver sources not found at ${USDHC_PATCHES_SRC}. Set USDHC_PATCHES_SRC or mount /attester."
        fi
        "${USDHC_PATCHES_SRC}/apply-usdhc.sh" "${S}"
    fi
}
