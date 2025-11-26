#!/bin/bash
#
# Rebuild optee-os with updated PTA source files
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

YOCTO_DIR="$SCRIPT_DIR/yocto"

echo "========================================="
echo "Rebuilding optee-os with updated PTA"
echo "========================================="

HOST_UID=$(id -u)
HOST_GID=$(id -g)

docker run --rm \
  --user ${HOST_UID}:${HOST_GID} \
  -v "$SCRIPT_DIR:/workspace" \
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

# Setup environment
source ./imx-setup-release.sh -b build 2>&1

# Ensure Veraison layer is added
if ! grep -q "meta-veraison-attestation" conf/bblayers.conf; then
    echo "" >> conf/bblayers.conf
    echo "# Veraison attestation layer" >> conf/bblayers.conf
    echo "BBLAYERS += \"/workspace/meta-veraison-attestation\"" >> conf/bblayers.conf
fi

# Ensure CAAM is enabled
if ! grep -q "CFG_NXP_CAAM" conf/local.conf; then
    echo "" >> conf/local.conf
    echo "# Enable CAAM (Cryptographic Acceleration and Assurance Module)" >> conf/local.conf
    echo "EXTRA_OEMAKE:append:pn-optee-os = \" CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y\"" >> conf/local.conf
fi

echo "bblayers.conf content:"
grep -i veraison conf/bblayers.conf || echo "Not found"

echo ""

# Clean optee-os to rebuild with new PTA structure
echo "Cleaning optee-os..."
bitbake -c cleansstate optee-os

echo "Rebuilding optee-os..."
bitbake optee-os

echo ""
echo "optee-os rebuild complete!"
'
