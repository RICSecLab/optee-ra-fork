# Secure Boot (i.MX8MP EVK, HABv4, SD boot)

This guide describes a reproducible secure-boot flow for i.MX8MP EVK using
HABv4. It follows the NXP HABv4 secure boot procedure (SRK/CSF/Authenticate
Data) and the i.MX8M boot image format. The repo helper scripts build and sign
`imx-boot` and the kernel `Image`, then patch the WIC image.

Secure boot is **irreversible** once the fuses are burned. Always validate in
Open mode before closing the device.

## Prerequisites

- Run commands from the repo root
- Yocto build directory: `/dev/shm/yocto`
- CST tarball placed at: `./cst-3.1.0.tgz`
- Output directory (relative): `../optee-ra-artifacts/secure-boot`
- SD card device path (example: `/dev/sdX`)

## 1) Build Yocto image (first time only)

```bash
cd ./attester/container-imx
YOCTO_DIR=/dev/shm/yocto ./yocto.sh
```

## 2) Generate signing keys (one-time)

```bash
OUT_DIR=../optee-ra-artifacts/secure-boot

./attester/container-imx/secure-boot-gen-keys.sh \
  --cst-tar ./cst-3.1.0.tgz \
  --out-dir "$OUT_DIR"
```

This extracts CST, generates a HABv4 PKI tree (RSA 2048, SHA-256, 4 SRK CAs),
and prints the SRK fuse values. Keys are stored in `$OUT_DIR/cst/`.

The script refuses to run if keys already exist to prevent accidental
overwrite. Delete `$OUT_DIR/cst/` to regenerate.

## 3) Backup the signing keys (mandatory)

Archive keys before burning fuses:

```bash
tar -czf "$OUT_DIR/secure-boot-keys.tar.gz" \
  -C "$OUT_DIR" cst
```

## 4) Sign and patch WIC

```bash
OUT_WIC=/dev/shm/yocto/build/tmp/deploy/images/imx8mpevk/core-image-minimal-imx8mpevk.rootfs.wic.zst

./attester/container-imx/secure-boot-imx8mp.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-dir "$OUT_DIR/cst" \
  --out-dir "$OUT_DIR" \
  --patch-wic "$OUT_WIC"
```

Outputs:
- Signed imx-boot
  - `$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed`
- Signed kernel Image
  - `$OUT_DIR/img/Image-signed`
- WIC with signed imx-boot injected at 32KB and signed `Image` in the boot partition
  - `$OUT_DIR/core-image-minimal-imx8mpevk.rootfs.wic.zst`

You can run the individual steps directly:

```bash
./attester/container-imx/secure-boot-sign-imx-boot.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-dir "$OUT_DIR/cst" \
  --out-dir "$OUT_DIR"

./attester/container-imx/secure-boot-sign-kernel.sh \
  --yocto-dir /dev/shm/yocto \
  --cst-dir "$OUT_DIR/cst" \
  --out-dir "$OUT_DIR"

./attester/container-imx/secure-boot-patch-wic.sh \
  --wic "$OUT_WIC" \
  --signed-imx-boot "$OUT_DIR/imx-boot-imx8mpevk-sd.bin-flash_evk-signed" \
  --signed-image "$OUT_DIR/img/Image-signed" \
  --out-dir "$OUT_DIR"
```

## 5) Extract SRK fuse values

```bash
hexdump -e '/4 "0x"' -e '/4 "%X""\n"' \
  "$OUT_DIR/cst/keys/SRK_1_2_3_4_fuses.bin"
```

This prints 8 lines. You will use them in `fuse prog` below.

## 6) Write SD card

```bash
cd "$OUT_DIR"
zstd -d core-image-minimal-imx8mpevk.rootfs.wic.zst
sudo dd if=core-image-minimal-imx8mpevk.rootfs.wic of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with the actual SD device.

## 7) Open mode validation (U-Boot)

```text
=> hab_status
```

Expected: `Secure boot disabled` (Open). This is correct before closing.

## 8) Program SRK fuses (U-Boot)

**Before writing**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

**Write SRK hash** (replace values with the 8 lines from Step 5):

```text
=> fuse prog 6 0 0x........
=> fuse prog 6 1 0x........
=> fuse prog 6 2 0x........
=> fuse prog 6 3 0x........

=> fuse prog 7 0 0x........
=> fuse prog 7 1 0x........
=> fuse prog 7 2 0x........
=> fuse prog 7 3 0x........
```

**After writing**:

```text
=> fuse read 6 0 4
=> fuse read 7 0 4
```

Reboot:

```text
=> reset
```

## 9) Close the device (irreversible)

Only after Open validation is complete:

```text
=> fuse prog 1 3 0x2000000
```

Then reboot and check:

```text
=> hab_status
```

Expected: `Secure boot enabled`.

## 10) Remote Attestation checks

If `relying-party-service` is not resolvable, add it:

```bash
echo "192.168.8.244 relying-party-service" >> /etc/hosts
```

Read the device key, which the PTA generates now if the device has none
yet, and register it with the Verifier (README section 8.1):

```bash
optee_remote_attestation --get-pubkey
```

Run attestation:

```bash
optee_remote_attestation
```

Expected: `ear.status: "affirming"`.

A key generated before closing the device does not survive the closing:
Secure Boot changes the CAAM key context (MPMR/BKEK), so the stored CAAM
blob can no longer be unwrapped and evidence generation fails
(`TEEC_InvokeCommand failed`). Wipe the key object and restart the TEE
(README section 8.3), then read and register the new key.

## 11) Per-device keys (two devices)

The CAAM blob the PTA stores is bound to the device: it is wrapped with the
device's JDKEK, derived from unique hardware entropy, so two devices closed
with the same SRK still sign with different keys, and the Verifier must know
each of them. The blob never leaves the PTA, so this is checked through the
Verifier: a device whose key is not registered is rejected.

Both devices must be **closed** with the same SRK (Steps 8-9), and Device A
registered as in Step 10.

### 11.1 Device B with its own, unregistered key

On **Device B**:

```bash
optee_remote_attestation --get-pubkey
optee_remote_attestation
```

Expected: `ear.status: "contraindicated"` with `"no trust anchor for evidence"`.

Device B signs with its own key (the fuses, the reference values and the
boot signer are the same as A's), but its public key, and so its
`psa-instance-id`, is not registered as a trust anchor.

### 11.2 Register Device B and verify

Compute the instance-id and PEM public key from the `--get-pubkey` output
(see the helper in README section 8), create a provisioning JSON with
Device B's values and submit it:

```bash
# Build and submit Device B's trust anchor
veraison -- cocli comid create \
    --template provisoning/data/comid-psa-ta-deviceB.json \
    --template provisoning/data/comid-psa-refval-imx.json \
    --output-dir provisoning/data

veraison -- cocli corim create \
    --template provisoning/data/corim-psa.json \
    --comid provisoning/data/comid-psa-refval-imx.cbor \
    --comid provisoning/data/comid-psa-ta-deviceB.cbor \
    --output provisoning/data/psa-endorsements-deviceB.cbor

veraison -- cocli corim submit \
    --corim-file provisoning/data/psa-endorsements-deviceB.cbor \
    --api-server "https://provisioning-service:9443/endorsement-provisioning/v1/submit" \
    --media-type 'application/corim-unsigned+cbor; profile="http://arm.com/psa/iot/1"'
```

Run attestation again on **Device B**:

```bash
optee_remote_attestation
```

Expected: `ear.status: "affirming"`.

### Summary

| Test | Result | Meaning |
|------|--------|---------|
| Device B, key unregistered | `no trust anchor` | Each device signs with its own CAAM-bound key; its instance-id is unknown to the Verifier |
| Device B after registration | `affirming` | Trust anchor added, attestation succeeds |
| Key from before the close | evidence fails | The CAAM blob is bound to the secure state; start over after closing |

## Notes

- Close is irreversible. Do not proceed without key backups.
- The signed imx-boot is injected at 32KB in the WIC by the script.
- Use the same SRK values on both devices for the non-reuse test.
- Each device generates a unique CAAM key pair; the Verifier must register
  each device's trust anchor individually.
