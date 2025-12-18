#!/bin/bash
#
# Rebuild veraison-attestation (TA + host app) with updated source files
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

YOCTO_DIR="$SCRIPT_DIR/yocto"

echo "========================================="
echo "Rebuilding veraison-attestation"
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

# Setup environment
source ./imx-setup-release.sh -b build 2>&1

# Ensure Veraison layer is added
if ! grep -q "meta-veraison-attestation" conf/bblayers.conf; then
    echo "" >> conf/bblayers.conf
    echo "# Veraison attestation layer" >> conf/bblayers.conf
    echo "BBLAYERS += \"/workspace/meta-veraison-attestation\"" >> conf/bblayers.conf
fi

echo ""

# Clean and rebuild veraison-attestation
echo "Cleaning veraison-attestation..."
bitbake -c cleansstate veraison-attestation

echo "Rebuilding veraison-attestation..."
bitbake veraison-attestation

echo ""
echo "veraison-attestation rebuild complete!"
'
