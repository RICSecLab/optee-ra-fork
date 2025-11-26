SUMMARY = "Veraison Attestation Application for i.MX with OP-TEE"
DESCRIPTION = "Remote attestation application using Veraison service with OP-TEE TA and host application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "optee-os optee-client rust-native"

inherit cargo

SRC_URI = "file://src"

S = "${WORKDIR}/src"

# Rust dependencies
CARGO_SRC_DIR = "${S}/host/rust-ffi"

do_compile() {
    # Build Rust FFI library
    cd ${S}/host/rust-ffi
    export OPENSSL_DIR=${STAGING_DIR_NATIVE}/usr
    cargo build --features real --target aarch64-unknown-linux-gnu --release

    # Build TA
    cd ${S}/ta
    export TA_DEV_KIT_DIR="${STAGING_INCDIR}/optee/export-ta_arm64"
    export CROSS_COMPILE="${TARGET_PREFIX}"
    oe_runmake

    # Build host application
    cd ${S}/host
    export TEEC_EXPORT="${STAGING_DIR_TARGET}/usr"
    export CROSS_COMPILE="${TARGET_PREFIX}"
    oe_runmake
}

do_install() {
    # Install TA
    install -d ${D}${nonarch_base_libdir}/optee_armtz
    install -m 0444 ${S}/ta/*.ta ${D}${nonarch_base_libdir}/optee_armtz/

    # Install host application
    install -d ${D}${bindir}
    install -m 0755 ${S}/host/optee_example_veraison_attestation ${D}${bindir}/
}

FILES:${PN} = "${bindir}/optee_example_veraison_attestation"
FILES:${PN} += "${nonarch_base_libdir}/optee_armtz/*.ta"

RDEPENDS:${PN} = "optee-client"
