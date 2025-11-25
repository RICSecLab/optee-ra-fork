# CAAM Support in Attester

This attester now supports both QEMU (software) and i.MX 8M Plus (CAAM hardware) environments.

## Build Modes

### QEMU Mode (Development)
```bash
./build.sh qemu
# or just
./build.sh
```
- CAAM: Disabled
- Uses embedded test keys
- For development and testing

### i.MX Mode (Production)
```bash
./build.sh imx
```
- CAAM: Enabled
- Hardware security module for key generation and signing
- For i.MX 8MPLUSLPD4-EVK and similar platforms

## Configuration

Edit `build-config.mk` to change default settings:

| Setting | QEMU | i.MX | Description |
|---------|------|------|-------------|
| `CFG_NXP_CAAM_ECC_DRV` | n | y | Enable CAAM hardware crypto |
| `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY` | y | n | Use embedded test keys |

## Directory Structure
```
attester/
├── pta/                 # PTA with CAAM support
│   ├── veraison_attestation.c  # Main PTA with #ifdef CFG_NXP_CAAM_ECC_DRV
│   ├── sign.c          # Signing (CAAM or software)
│   ├── cbor.c          # CBOR encoding
│   └── hash.c          # Hash operations
├── ta/                  # Trusted Application
├── host/                # Normal World client
├── container/           # Docker environment (QEMU)
├── build-config.mk      # Platform configuration
├── build.sh            # Unified build script
└── out/
    ├── qemu/           # QEMU build outputs
    └── imx/            # i.MX build outputs
```

## CAAM Operation

When CAAM is enabled (i.MX mode):

1. **Key Generation**:
   - PTA generates ECC P-256 keypair via CAAM
   - Private key returned as CAAM black blob (hardware-bound)

2. **Signing**:
   - TA provides black blob to PTA
   - PTA uses CAAM to sign with hardware-protected key

3. **Security**:
   - Keys cannot be extracted from device
   - Keys are unique per device
   - Hardware-level protection

## Building in Docker (QEMU)

```bash
# In OP-TEE 4.6.0 Docker container
cd /optee-ra/attester
./build.sh qemu

# Run in QEMU
cd /optee/build
make run-only

# In Normal World terminal
optee_example_veraison_attestation
```

## Building for i.MX 8M Plus

```bash
# Set up i.MX environment
export TA_DEV_KIT_DIR=/path/to/imx-optee-os/out/arm/export-ta_arm64
export OPTEE_CLIENT_EXPORT=/path/to/imx-optee-client/out/export/usr

# Build
cd attester
./build.sh imx

# Deploy to device
scp out/imx/*.ta root@<device-ip>:/lib/optee_armtz/
scp out/imx/optee_example_veraison_attestation root@<device-ip>:/usr/bin/

# On device
optee_example_veraison_attestation --provision  # Generate CAAM keys
optee_example_veraison_attestation              # Run attestation
```

## Key Differences

| Feature | QEMU | i.MX with CAAM |
|---------|------|----------------|
| Key Storage | Software (memory) | Hardware (CAAM black blob) |
| Key Generation | Software PRNG | Hardware RNG |
| Signing | Software ECC | Hardware accelerated |
| Security Level | Development only | Production ready |
| Key Migration | Possible (insecure) | Impossible (hardware-bound) |

## Troubleshooting

### CAAM Not Available
- Ensure running on actual i.MX hardware
- Check kernel has CAAM driver enabled
- Verify OP-TEE OS built with CAAM support

### Build Errors
```bash
# Clean everything
rm -rf out/
cd ta && make clean && cd ..
cd host && make clean && cd ..
```

### Test Keys Warning
Never use `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y` in production!