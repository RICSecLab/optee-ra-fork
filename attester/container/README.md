# QEMU Test Environment for Remote Attestation

This directory contains the Docker-based QEMU environment for testing the remote attestation implementation.

## Overview

This environment builds OP-TEE 4.6.0 for QEMU (ARM64) and allows testing of:
- **PTA (Pseudo Trusted Application)**: Remote attestation PTA with CAAM-disabled mode
- **TA (Trusted Application)**: Veraison attestation TA
- **Host Application**: `optee_remote_attestation`

The remote attestation PTA is mounted over OP-TEE's built-in `veraison_attestation` PTA, replacing it with our enhanced implementation.

## Directory Structure

```
container/
├── Dockerfile      # Docker image definition (OP-TEE 4.6.0 + toolchains)
├── start.sh        # Build and run script
└── README.md       # This file
```

## Quick Start

### 1. Start the Container

```bash
./start.sh
```

This will:
- Build the Docker image with OP-TEE 4.6.0
- Mount the source directories:
  - `../` → `/optee/optee_examples/remote_attestation` (TA + Host)
  - `../pta_remote_attestation/remote_attestation` → `/optee/optee_os/core/pta/veraison_attestation` (PTA)
- Start an interactive bash shell

### 2. Build OP-TEE (Inside Container)

```bash
make -C /optee/build optee-os-clean
make -C /optee/build \
  CFG_VERAISON_ATTESTATION_PTA=y \
  CFG_NXP_CAAM_ECC_DRV=n \
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y \
  -j$(nproc)
```

Build flags:
- `CFG_VERAISON_ATTESTATION_PTA=y`: Enable the attestation PTA
- `CFG_NXP_CAAM_ECC_DRV=n`: Disable CAAM (not available in QEMU)
- `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y`: Use embedded test key for signing

### 3. Run QEMU

```bash
make -C /optee/build run \
  CFG_VERAISON_ATTESTATION_PTA=y \
  CFG_NXP_CAAM_ECC_DRV=n \
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y
```

This opens three xterm windows:
- **QEMU**: The emulator
- **Normal World**: Linux shell (run tests here)
- **Secure World**: OP-TEE OS logs

### 4. Test (In Normal World Terminal)

Login as `root` (no password), then:

```bash
optee_remote_attestation --mock
```

Expected output:
```
[veraison-host] startup (built ...)
[veraison-host] MOCK_ONLY build: forcing --mock mode
...
Invoke TA.
Invoked TA successfully.
CBOR(COSE) size: XXX
CBOR(COSE): d28443a10126...
```

## Build Configurations

| Flag | Description |
|------|-------------|
| `CFG_VERAISON_ATTESTATION_PTA=y` | Enable attestation PTA |
| `CFG_NXP_CAAM_ECC_DRV=n` | Disable CAAM (for QEMU) |
| `CFG_NXP_CAAM_ECC_DRV=y` | Enable CAAM (for i.MX) |
| `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y` | Use embedded test key |

## Veraison Server Integration

To test with the actual Veraison verification server:

### 1. Start Veraison Services (on Host)

```bash
cd ../../../services/deployments/docker
make
source env.bash
veraison start
veraison status  # Should show all services "running"
```

### 2. Start Container with Network

The `start.sh` script automatically detects and connects to `veraison-net`:

```bash
./start.sh
# Output: "Connecting to veraison-net network for Veraison server integration..."
```

### 3. Build Without MOCK_ONLY (Inside Container)

```bash
# Clean previous build
rm -rf /optee/out-br/build/optee_examples_ext-1.0

# Rebuild with Veraison client
make -C /optee/build \
  CFG_VERAISON_ATTESTATION_PTA=y \
  CFG_NXP_CAAM_ECC_DRV=n \
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y \
  -j$(nproc)
```

### 4. Test with Veraison Server (In QEMU Normal World)

```bash
optee_remote_attestation
# (without --mock flag)
```

The application will:
1. Connect to `verification-service:8080`
2. Open a challenge-response session
3. Send the generated COSE evidence to Veraison
4. Receive and display the attestation result (JWT/EAR)

## Notes

- By default, the host application is built with `MOCK_ONLY=1` for quick testing
- To use Veraison server, ensure:
  - Veraison services are running (`veraison status`)
  - The Rust FFI library is available at `host/rust-ffi/target/aarch64-unknown-linux-gnu/release/libveraison_client_ffi.a`
- The PTA overwrites OP-TEE 4.6.0's built-in `veraison_attestation` with our enhanced `remote_attestation` implementation
- For real hardware testing with CAAM, use `container-imx/` instead

## Troubleshooting

### "Segmentation fault" on host

Check the Secure World terminal for TA/PTA errors. Common issues:
- PTA not built into OP-TEE OS (run `optee-os-clean` and rebuild)
- UUID mismatch between TA and host application

### PTA not found (TEE_ERROR_ITEM_NOT_FOUND)

Ensure the PTA is mounted correctly:
```bash
ls -la /optee/optee_os/core/pta/veraison_attestation/
```

### Build errors about multiple definitions

Only one of `veraison_attestation` or `remote_attestation` should be in the build. The mount setup handles this automatically.
