#!/bin/bash
# Build script for dual-mode attester (QEMU/i.MX)
# Usage: ./build.sh [qemu|imx]

set -e

PLATFORM=${1:-qemu}

echo "========================================="
echo "Building Attester with CAAM Support"
echo "Platform: $PLATFORM"
echo "========================================="

# Set build configuration based on platform
export PLATFORM=$PLATFORM
export CFG_TEE_CORE_LOG_LEVEL=3
export CFG_TEE_TA_LOG_LEVEL=3
export CFG_VERAISON_ATTESTATION_PTA=y
export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_TA=aarch64-linux-gnu-

case "$PLATFORM" in
    qemu)
        echo "Target: QEMU (software crypto, test keys)"
        export CFG_NXP_CAAM=n
        export CFG_NXP_CAAM_ECC_DRV=n
        export CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y
        export PLATFORM_FLAVOR=virt
        # For Docker container environment
        if [ -d "/optee" ]; then
            export TA_DEV_KIT_DIR=/optee/optee_os/out/arm/export-ta_arm64
            export OPTEE_CLIENT_EXPORT=/optee/optee_client/out/export/usr
        fi
        ;;
    imx)
        echo "Target: i.MX 8M Plus (CAAM hardware crypto)"
        export CFG_NXP_CAAM=y
        export CFG_NXP_CAAM_ECC_DRV=y
        export CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=n
        export PLATFORM_FLAVOR=mx8mpevk
        export CFG_IMX_CAAM=y
        export CFG_DT=y
        # For i.MX SDK environment
        if [ -z "$TA_DEV_KIT_DIR" ]; then
            echo "Warning: TA_DEV_KIT_DIR not set. Please set it to your i.MX OP-TEE export directory"
        fi
        ;;
    *)
        echo "Usage: $0 [qemu|imx]"
        exit 1
        ;;
esac

# Build TA
if [ -d "ta" ]; then
    echo "Building TA..."
    cd ta
    make clean
    make PLATFORM=$PLATFORM
    cd ..
fi

# Build host application
if [ -d "host" ]; then
    echo "Building host application..."
    cd host
    make clean
    make PLATFORM=$PLATFORM
    cd ..
fi

# Create output directory
OUT_DIR="out/$PLATFORM"
mkdir -p $OUT_DIR

# Copy built files
echo "Copying output to $OUT_DIR..."
[ -f ta/*.ta ] && cp ta/*.ta $OUT_DIR/ 2>/dev/null || true
[ -f host/optee_example_veraison_attestation ] && cp host/optee_example_veraison_attestation $OUT_DIR/ 2>/dev/null || true

echo "========================================="
echo "Build complete!"
echo "Output: $OUT_DIR/"
echo "CAAM: $CFG_NXP_CAAM_ECC_DRV"
echo "Test keys: $CFG_VERAISON_ATTESTATION_PTA_TEST_KEY"
echo "========================================="