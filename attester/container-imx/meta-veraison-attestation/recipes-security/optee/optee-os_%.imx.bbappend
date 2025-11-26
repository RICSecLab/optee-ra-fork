# Add CAAM-enabled Remote Attestation PTA to OP-TEE OS
# Note: veraison_attestation (non-CAAM) is already included in OP-TEE 4.6.0
# This adds remote_attestation (CAAM-enabled version we're developing)
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://pta_remote_attestation/remote_attestation/veraison_attestation.c \
    file://pta_remote_attestation/remote_attestation/cbor.c \
    file://pta_remote_attestation/remote_attestation/cbor.h \
    file://pta_remote_attestation/remote_attestation/hash.c \
    file://pta_remote_attestation/remote_attestation/hash.h \
    file://pta_remote_attestation/remote_attestation/sign.c \
    file://pta_remote_attestation/remote_attestation/sign.h \
    file://pta_remote_attestation/remote_attestation/sub.mk \
"

# Copy PTA source to OP-TEE OS source tree before compile
do_configure:append() {
    # Create PTA directory in OP-TEE OS source
    install -d ${S}/core/pta/remote_attestation

    # Copy PTA source files (files are in sources-unpack/pta_remote_attestation/remote_attestation/)
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/veraison_attestation.c ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/cbor.c ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/cbor.h ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/hash.c ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/hash.h ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/sign.c ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/sign.h ${S}/core/pta/remote_attestation/
    cp ${WORKDIR}/sources-unpack/pta_remote_attestation/remote_attestation/sub.mk ${S}/core/pta/remote_attestation/

    # Add remote_attestation PTA to OP-TEE OS build
    # (veraison_attestation is already in upstream OP-TEE 4.6.0)
    if ! grep -q "^subdirs-y += remote_attestation" ${S}/core/pta/sub.mk; then
        echo "" >> ${S}/core/pta/sub.mk
        echo "subdirs-y += remote_attestation" >> ${S}/core/pta/sub.mk
    fi
}
