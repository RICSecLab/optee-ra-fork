#!/bin/bash
#
# Complete Yocto image build in Docker container
# This creates a fresh Yocto environment in ./yocto directory
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "Yocto Image Build in Docker Container"
echo "with Veraison Attestation Application"
echo "========================================="
echo ""
echo "This will:"
echo "  1. Build a Docker image with Ubuntu 22.04 (Python 3.10+)"
echo "  2. Download i.MX Yocto BSP to ./yocto directory"
echo "  3. Build complete system image (4-8 hours)"
echo "  4. Output: ./yocto/build/tmp/deploy/images/imx8mpevk/*.wic.zst"
echo ""
echo "Required: ~100GB disk space"
echo ""

# Build Docker image
echo "Building Yocto Docker environment..."
docker build -f Dockerfile.yocto -t veraison-yocto-builder .

# Create yocto directory in current location with proper permissions
mkdir -p yocto
chmod 777 yocto  # Allow container user to write
YOCTO_DIR="$SCRIPT_DIR/yocto"

echo ""
echo "========================================="
echo "Starting Yocto Build in Container"
echo "========================================="
echo ""
echo "Yocto sources will be downloaded to: $YOCTO_DIR"
echo "This may take 1-2 hours for initial download..."
echo ""

# Run Yocto build in Docker (as host user to avoid permission issues)
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

# Configuration
IMX_RELEASE="imx-6.12.20-2.0.0"
MACHINE="imx8mpevk"
DISTRO="fsl-imx-xwayland"

echo "========================================="
echo "Step 1: Download i.MX Yocto BSP"
echo "========================================="

cd /yocto

# Download BSP if not exists
if [ ! -d "sources" ]; then
    echo "Downloading i.MX BSP ${IMX_RELEASE}..."
    repo init -u https://github.com/nxp-imx/imx-manifest -b imx-linux-walnascar -m ${IMX_RELEASE}.xml
    repo sync -j$(nproc)
else
    echo "Using existing Yocto sources"
fi

echo ""
echo "========================================="
echo "Step 2: Setup Build Environment"
echo "========================================="

# Setup build environment using i.MX setup script
EULA=1 MACHINE=${MACHINE} DISTRO=${DISTRO} source ./imx-setup-release.sh -b build

# Add custom layer
if ! grep -q "meta-veraison-attestation" conf/bblayers.conf; then
    echo "" >> conf/bblayers.conf
    echo "# Veraison attestation layer" >> conf/bblayers.conf
    echo "BBLAYERS += \"/workspace/meta-veraison-attestation\"" >> conf/bblayers.conf
fi

# Configure local.conf
if ! grep -q "veraison-attestation" conf/local.conf; then
    cat >> conf/local.conf <<EOF

# OP-TEE configuration
MACHINE_FEATURES:append = " optee"
DISTRO_FEATURES:append = " optee"

# Enable CAAM (Cryptographic Acceleration and Assurance Module)
EXTRA_OEMAKE:append:pn-optee-os = " CFG_NXP_CAAM=y CFG_NXP_CAAM_ECC_DRV=y"

# Install Veraison attestation application
IMAGE_INSTALL:append = " veraison-attestation"
IMAGE_INSTALL:append = " optee-os optee-client optee-test"
EOF
fi

echo ""
echo "========================================="
echo "Step 3: Building Image (4-8 hours)"
echo "========================================="

# Build the image (core-image-minimal to avoid GUI dependencies)
bitbake core-image-minimal

echo ""
echo "========================================="
echo "Build Complete!"
echo "========================================="
echo ""
echo "Output image:"
ls -lh tmp/deploy/images/${MACHINE}/core-image-minimal*.wic.zst | tail -1
echo ""
echo "Image location: /yocto/build/tmp/deploy/images/${MACHINE}/"
echo ""
'

echo ""
echo "========================================="
echo "Build Finished!"
echo "========================================="
echo ""
echo "Image location: $YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"
ls -lh "$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"core-image-minimal*.wic.zst 2>/dev/null || echo "No image found - check build log"
echo ""
echo "To flash to SD card:"
echo "  cd $YOCTO_DIR/build/tmp/deploy/images/imx8mpevk/"
echo "  zstd -d imx-image-core-imx8mpevk.rootfs.wic.zst"
echo "  sudo dd if=imx-image-core-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress && sync"
echo ""
