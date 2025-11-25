# Build configuration for CAAM/non-CAAM dual-mode attester
# Platform selection: PLATFORM=qemu (default) or PLATFORM=imx

PLATFORM ?= qemu

# Common settings
CFG_TEE_CORE_LOG_LEVEL ?= 3
CFG_TEE_TA_LOG_LEVEL ?= 3

ifeq ($(PLATFORM),imx)
  # i.MX 8M Plus with CAAM support
  CFG_NXP_CAAM_ECC_DRV ?= y
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY ?= n
  PLATFORM_FLAVOR ?= mx8mpevk
  $(info Building for i.MX 8M Plus with CAAM hardware crypto)
else
  # QEMU without CAAM
  CFG_NXP_CAAM_ECC_DRV ?= n
  CFG_VERAISON_ATTESTATION_PTA_TEST_KEY ?= y
  PLATFORM_FLAVOR ?= virt
  $(info Building for QEMU with software crypto and test keys)
endif

# Export for sub-makes
export CFG_NXP_CAAM_ECC_DRV
export CFG_VERAISON_ATTESTATION_PTA_TEST_KEY
export PLATFORM_FLAVOR