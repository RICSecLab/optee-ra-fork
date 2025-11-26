# Build configuration for CAAM/non-CAAM dual-mode attester
# Platform selection: PLATFORM=qemu (default) or PLATFORM=imx

PLATFORM ?= qemu

# Common settings
CFG_TEE_CORE_LOG_LEVEL ?= 3
CFG_TEE_TA_LOG_LEVEL ?= 3
CFG_VERAISON_ATTESTATION_PTA ?= y

# Cross-compilation toolchain
CROSS_COMPILE ?= aarch64-linux-gnu-
CROSS_COMPILE_TA ?= aarch64-linux-gnu-

ifeq ($(PLATFORM),imx)
  # i.MX 8M Plus with CAAM support
  CFG_NXP_CAAM ?= y
  CFG_NXP_CAAM_ECC_DRV ?= y
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY ?= n
  PLATFORM_FLAVOR ?= mx8mpevk

  # i.MX specific flags
  CFG_IMX_CAAM ?= y
  CFG_DT ?= y

  $(info Building for i.MX 8M Plus with CAAM hardware crypto)
else
  # QEMU without CAAM
  CFG_NXP_CAAM ?= n
  CFG_NXP_CAAM_ECC_DRV ?= n
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY ?= y
  PLATFORM_FLAVOR ?= virt

  $(info Building for QEMU with software crypto and test keys)
endif

# Export all configuration
export CFG_NXP_CAAM
export CFG_NXP_CAAM_ECC_DRV
export CFG_VERAISON_ATTESTATION_PTA
export CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
export PLATFORM_FLAVOR
export CROSS_COMPILE
export CROSS_COMPILE_TA
export CFG_TEE_CORE_LOG_LEVEL
export CFG_TEE_TA_LOG_LEVEL