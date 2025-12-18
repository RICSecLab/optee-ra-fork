SUMMARY = "Veraison Attestation Application for i.MX with OP-TEE"
DESCRIPTION = "Remote attestation application using Veraison service with OP-TEE TA and host application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "optee-os-tadevkit optee-client openssl python3-cryptography-native"

inherit cargo rust-target-config python3native

SRC_URI = "file://src"

S = "${WORKDIR}/src"

# OP-TEE configuration
TA_DEV_KIT_DIR = "${STAGING_INCDIR}/optee/export-user_ta"
OPTEE_CLIENT_EXPORT = "${STAGING_DIR_HOST}${prefix}"
TEEC_EXPORT = "${STAGING_DIR_HOST}${prefix}"

# Rust configuration
CARGO_SRC_DIR = "${S}/host/rust-ffi"
CARGO_MANIFEST_PATH = "${S}/host/rust-ffi/Cargo.toml"

# Enable Cargo features
CARGO_FEATURES = "real"
CARGO_BUILD_FLAGS = "-v --frozen --target ${RUST_HOST_SYS} --release --features ${CARGO_FEATURES} --manifest-path=${CARGO_MANIFEST_PATH}"

# python3-cryptography needs the legacy provider
export OPENSSL_MODULES = "${STAGING_LIBDIR_NATIVE}/ossl-modules"

do_compile() {
    # Build Rust FFI library using Yocto's cargo infrastructure
    cd ${S}/host/rust-ffi
    export OPENSSL_DIR="${STAGING_DIR_TARGET}/usr"
    export OPENSSL_LIB_DIR="${STAGING_DIR_TARGET}/usr/lib"
    export OPENSSL_INCLUDE_DIR="${STAGING_DIR_TARGET}/usr/include"
    export RUSTFLAGS="${RUSTFLAGS}"

    bbnote "Building Rust FFI with target ${RUST_HOST_SYS}"
    cargo build --frozen --target ${RUST_HOST_SYS} --release --features real --manifest-path=${CARGO_MANIFEST_PATH}

    # Build TA - IMPORTANT: Clear Yocto's CFLAGS/LDFLAGS as OP-TEE TA build system
    # has its own flags and uses CROSS_COMPILE for the aarch64 toolchain
    cd ${S}/ta
    unset CFLAGS
    unset CPPFLAGS
    unset CXXFLAGS
    unset LDFLAGS

    # Clean any stale build artifacts that may have incorrect paths from QEMU builds
    rm -f .*.d .*.o.cmd *.o *.lds ta.lds dyn_list *.map *.dmp *.ta *.elf 2>/dev/null || true

    # Use oe_runmake with explicit TA build parameters
    oe_runmake V=1 \
        TA_DEV_KIT_DIR=${TA_DEV_KIT_DIR} \
        CROSS_COMPILE=${HOST_PREFIX} \
        LIBGCC_LOCATE_CFLAGS="--sysroot=${STAGING_DIR_HOST}"

    # Build host application
    cd ${S}/host

    # Clean any stale build artifacts from QEMU builds (x86_64 objects)
    rm -f *.o optee_remote_attestation optee_example_veraison_attestation 2>/dev/null || true

    export TEEC_EXPORT="${STAGING_DIR_HOST}/usr"
    export CROSS_COMPILE="${TARGET_PREFIX}"
    # Restore CFLAGS for host app build
    export CFLAGS="--sysroot=${STAGING_DIR_HOST}"
    export LDFLAGS="--sysroot=${STAGING_DIR_HOST}"
    oe_runmake
}

do_install() {
    # Install TA
    install -d ${D}${nonarch_base_libdir}/optee_armtz
    install -m 0444 ${S}/ta/*.ta ${D}${nonarch_base_libdir}/optee_armtz/

    # Install host application
    install -d ${D}${bindir}
    install -m 0755 ${S}/host/optee_remote_attestation ${D}${bindir}/
}

FILES:${PN} = "${bindir}/optee_remote_attestation"
FILES:${PN} += "${nonarch_base_libdir}/optee_armtz/*.ta"

RDEPENDS:${PN} = "optee-client"
