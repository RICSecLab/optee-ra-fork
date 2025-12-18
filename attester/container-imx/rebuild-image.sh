#!/bin/bash
#
# Rebuild core-image-minimal with updated packages
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

YOCTO_DIR="$SCRIPT_DIR/yocto"

echo "========================================="
echo "Rebuilding core-image-minimal"
echo "========================================="

HOST_UID=$(id -u)
HOST_GID=$(id -g)

ATTESTER_DIR="$(dirname "$SCRIPT_DIR")"

docker run --rm \
  --user ${HOST_UID}:${HOST_GID} \
  -v "$SCRIPT_DIR:/workspace" \
  -v "$ATTESTER_DIR:/attester" \
  -v "$YOCTO_DIR:/yocto" \
  -v /etc/passwd:/etc/passwd:ro \
  -v /etc/group:/etc/group:ro \
  -e HOME=/tmp \
  -w /workspace \
  veraison-yocto-builder \
  /bin/bash -c '
set -e

cd /yocto
MACHINE="imx8mpevk"
DISTRO="fsl-imx-xwayland"

source ./imx-setup-release.sh -b build 2>&1

# Ensure Veraison layer is added
if ! grep -q "meta-veraison-attestation" conf/bblayers.conf; then
    echo "" >> conf/bblayers.conf
    echo "# Veraison attestation layer" >> conf/bblayers.conf
    echo "BBLAYERS += \"/workspace/meta-veraison-attestation\"" >> conf/bblayers.conf
fi

# Ensure veraison-attestation is in IMAGE_INSTALL
if ! grep -q "veraison-attestation" conf/local.conf; then
    cat >> conf/local.conf <<LOCALEOF

# OP-TEE configuration
MACHINE_FEATURES:append = " optee"
DISTRO_FEATURES:append = " optee"

# Enable CAAM (Cryptographic Acceleration and Assurance Module)
EXTRA_OEMAKE:append:pn-optee-os = " CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y"

# Install Veraison attestation application
IMAGE_INSTALL:append = " veraison-attestation"
IMAGE_INSTALL:append = " optee-os optee-client optee-test"
LOCALEOF
    echo "Added veraison-attestation to local.conf"
fi

echo "Cleaning core-image-minimal..."
bitbake -c cleansstate core-image-minimal

echo "Building core-image-minimal..."
bitbake core-image-minimal

echo ""
echo "Build complete!"
ls -lh tmp/deploy/images/${MACHINE}/core-image-minimal*.wic.zst | tail -1
'

echo ""
echo "========================================="
echo "Image Ready"
echo "========================================="
ls -lh "$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"core-image-minimal*.wic.zst
