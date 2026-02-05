#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
  cat <<'USAGE'
Usage: ./secure-boot-imx8mp.sh [options]

Options:
  --yocto-dir PATH   Yocto directory (default: ./yocto)
  --cst-tar PATH     CST tarball path (e.g., cst-3.4.0.tgz)
  --cst-dir PATH     CST directory (already extracted)
  --out-dir PATH     Output directory (default: ./secure-boot-out)
  --pass PASS        CST key passphrase (default: test)
  --patch-wic PATH   Patch WIC image with signed imx-boot (optional)
  -h, --help         Show this help

Notes:
  - This script runs CST inside Docker.
  - Yocto build output must already exist (imx-boot + tools).
  - Open-mode only. Do NOT burn fuses with test keys.
USAGE
}

YOCTO_DIR="${YOCTO_DIR:-${SCRIPT_DIR}/yocto}"
CST_TARBALL=""
CST_DIR=""
OUT_DIR="${SCRIPT_DIR}/secure-boot-out"
CST_PASS="test"
PATCH_WIC=""

while [ $# -gt 0 ]; do
  case "$1" in
    --yocto-dir)
      YOCTO_DIR="$2"; shift 2;;
    --cst-tar)
      CST_TARBALL="$2"; shift 2;;
    --cst-dir)
      CST_DIR="$2"; shift 2;;
    --out-dir)
      OUT_DIR="$2"; shift 2;;
    --pass)
      CST_PASS="$2"; shift 2;;
    --patch-wic)
      PATCH_WIC="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown option: $1" >&2
      usage; exit 1;;
  esac
  done

if [ -z "$CST_TARBALL" ] && [ -z "$CST_DIR" ]; then
  echo "Error: --cst-tar or --cst-dir is required." >&2
  exit 1
fi

IMX_BOOT_DIR="$YOCTO_DIR/build/tmp/deploy/images/imx8mpevk"
IMX_BOOT_BIN="$IMX_BOOT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk"
IMX_MKIMG_DIR="$YOCTO_DIR/build/tmp/work/imx8mpevk-poky-linux/imx-boot/1.0/git"
IMX_MKIMG_SOC_DIR="$IMX_MKIMG_DIR/iMX8M"

if [ ! -f "$IMX_BOOT_BIN" ]; then
  echo "Error: imx-boot not found at $IMX_BOOT_BIN" >&2
  echo "Run: YOCTO_DIR=$YOCTO_DIR ./yocto.sh" >&2
  exit 1
fi

if [ ! -d "$IMX_MKIMG_SOC_DIR" ]; then
  echo "Error: imx-mkimage directory not found at $IMX_MKIMG_SOC_DIR" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

# Build Docker image if needed
if ! docker image inspect veraison-yocto-builder >/dev/null 2>&1; then
  echo "Yocto Docker image not found; building it first..."
  docker build -f "$SCRIPT_DIR/Dockerfile.yocto" -t veraison-yocto-builder "$SCRIPT_DIR"
fi

CST_MOUNT_ARGS=""
if [ -n "$CST_TARBALL" ]; then
  CST_TARBALL_ABS="$(readlink -f "$CST_TARBALL")"
  CST_MOUNT_ARGS="-v $CST_TARBALL_ABS:/opt/cst.tar.gz:ro"
fi
if [ -n "$CST_DIR" ]; then
  CST_DIR_ABS="$(readlink -f "$CST_DIR")"
  CST_MOUNT_ARGS="$CST_MOUNT_ARGS -v $CST_DIR_ABS:/opt/cst:ro"
fi

OUT_DIR_ABS="$(readlink -f "$OUT_DIR")"
YOCTO_DIR_ABS="$(readlink -f "$YOCTO_DIR")"

cat > "$OUT_DIR/secure-boot.env" <<ENV_EOF
CST_PASS=$CST_PASS
ENV_EOF

# Run signing inside Docker
set -x

docker run --rm \
  -v "$YOCTO_DIR_ABS:/yocto" \
  -v "$OUT_DIR_ABS:/out" \
  $CST_MOUNT_ARGS \
  -e CST_PASS="$CST_PASS" \
  -w /out \
  veraison-yocto-builder \
  /bin/bash -c '
set -euo pipefail

if [ -f /opt/cst.tar.gz ]; then
  mkdir -p /opt/cst
  tar -xf /opt/cst.tar.gz -C /opt/cst --strip-components=1
fi

if [ ! -x /opt/cst/linux64/bin/cst ]; then
  echo "CST not found. Provide --cst-tar or --cst-dir with linux64/bin/cst" >&2
  exit 1
fi

CST_BIN=/opt/cst/linux64/bin/cst
SRKTOOL=/opt/cst/linux64/bin/srktool
PKI_SCRIPT=/opt/cst/keys/hab4_pki_tree.sh

if [ ! -x "$PKI_SCRIPT" ]; then
  echo "hab4_pki_tree.sh not found in CST package" >&2
  exit 1
fi

KEY_DIR=/out/keys
CSF_DIR=/out/csf
IMG_DIR=/out/img
LOG_DIR=/out/logs
mkdir -p "$KEY_DIR" "$CSF_DIR" "$IMG_DIR" "$LOG_DIR"

# Generate PKI tree (test keys)
if [ ! -f "$KEY_DIR/SRK_1_2_3_4_table.bin" ]; then
  (cd "$KEY_DIR" && "$PKI_SCRIPT" -n SRK -p "$CST_PASS")
  "$SRKTOOL" -h 4 \
    -t "$KEY_DIR/SRK_1_2_3_4_table.bin" \
    -e "$KEY_DIR/SRK_1_2_3_4_fuse.bin" \
    -d "$KEY_DIR/SRK1_sha256_2048_65537_v3_ca_crt.pem",\
"$KEY_DIR/SRK2_sha256_2048_65537_v3_ca_crt.pem",\
"$KEY_DIR/SRK3_sha256_2048_65537_v3_ca_crt.pem",\
"$KEY_DIR/SRK4_sha256_2048_65537_v3_ca_crt.pem"
fi

IMX_MKIMG_DIR=/yocto/build/tmp/work/imx8mpevk-poky-linux/imx-boot/1.0/git
IMX_MKIMG_SOC_DIR="$IMX_MKIMG_DIR/iMX8M"

# Rebuild unsigned flash.bin to capture HAB blocks
( cd "$IMX_MKIMG_DIR" && make SOC=iMX8MP flash_spl_uboot ) >"$LOG_DIR/mkimage.log" 2>&1
cp "$IMX_MKIMG_SOC_DIR/flash.bin" "$IMG_DIR/flash.bin"

# Extract CSF blocks
spl_line=$(awk "/SPL CSF block/{getline; print}" "$LOG_DIR/mkimage.log")
sld_line=$(awk "/SLD CSF block/{getline; print}" "$LOG_DIR/mkimage.log")

if [ -z "$spl_line" ] || [ -z "$sld_line" ]; then
  echo "Failed to parse CSF blocks. Check $LOG_DIR/mkimage.log" >&2
  exit 1
fi

spl_addr=$(echo "$spl_line" | awk "{print \$3}")
spl_off=$(echo "$spl_line" | awk "{print \$4}")
spl_size=$(echo "$spl_line" | awk "{print \$5}")

sld_addr=$(echo "$sld_line" | awk "{print \$3}")
sld_off=$(echo "$sld_line" | awk "{print \$4}")
sld_size=$(echo "$sld_line" | awk "{print \$5}")

cat > "$CSF_DIR/csf_spl.txt" <<CSF_EOF
[Header]
Version = 4.3
Hash Algorithm = sha256
Engine = CAAM
Engine Configuration = 0
Certificate Format = X509
Signature Format = CMS

[Install SRK]
File = "$KEY_DIR/SRK_1_2_3_4_table.bin"
Source index = 0

[Install CSFK]
File = "$KEY_DIR/CSF1_1_sha256_2048_65537_v3_usr_crt.pem"

[Authenticate CSF]

[Install KEY]
File = "$KEY_DIR/IMG1_1_sha256_2048_65537_v3_usr_crt.pem"

[Authenticate Data]
Verification index = 0
Engine = CAAM
Engine Configuration = 0
Blocks = $spl_addr $spl_off $spl_size "$IMG_DIR/flash.bin", \
         $sld_addr $sld_off $sld_size "$IMG_DIR/flash.bin"
CSF_EOF

"$CST_BIN" -i "$CSF_DIR/csf_spl.txt" -o "$CSF_DIR/csf_spl.bin"

# Build signed flash.bin
( cd "$IMX_MKIMG_SOC_DIR" && \
  ./mkimage_imx8 -version v2 -fit \
    -loader u-boot-spl-ddr.bin 0x920000 \
    -second_loader u-boot.itb 0x40200000 0x60000 \
    -csf "$CSF_DIR/csf_spl.bin" \
    -out "$IMG_DIR/flash-signed.bin" )

cp "$IMG_DIR/flash-signed.bin" "/out/imx-boot-imx8mpevk-sd.bin-flash_evk-signed"

echo "Signed image: /out/imx-boot-imx8mpevk-sd.bin-flash_evk-signed"
'

set +x

SIGNED_IMX_BOOT="$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed"
if [ ! -f "$SIGNED_IMX_BOOT" ]; then
  echo "Error: signed imx-boot not generated" >&2
  exit 1
fi

echo "Signed imx-boot generated at: $SIGNED_IMX_BOOT"

if [ -n "$PATCH_WIC" ]; then
  WIC_PATH="$PATCH_WIC"
  if [ ! -f "$WIC_PATH" ]; then
    echo "Error: WIC image not found: $WIC_PATH" >&2
    exit 1
  fi

  WIC_BASE="$OUT_DIR/$(basename "$WIC_PATH")"
  cp "$WIC_PATH" "$WIC_BASE"

  if [[ "$WIC_BASE" == *.zst ]]; then
    zstd -d --rm "$WIC_BASE"
    WIC_RAW="${WIC_BASE%.zst}"
  else
    WIC_RAW="$WIC_BASE"
  fi

  # imx-boot is written at 32KB (seek=32)
  dd if="$SIGNED_IMX_BOOT" of="$WIC_RAW" bs=1024 seek=32 conv=notrunc status=progress

  if [[ "$WIC_PATH" == *.zst ]]; then
    zstd -f "$WIC_RAW"
    echo "Patched WIC: ${WIC_RAW}.zst"
  else
    echo "Patched WIC: $WIC_RAW"
  fi
fi

