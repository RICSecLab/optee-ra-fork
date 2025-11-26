srcs-$(CFG_VERAISON_ATTESTATION_PTA) += veraison_attestation.c
srcs-$(CFG_VERAISON_ATTESTATION_PTA) += cbor.c
srcs-$(CFG_VERAISON_ATTESTATION_PTA) += hash.c
srcs-$(CFG_VERAISON_ATTESTATION_PTA) += sign.c

incdirs-y += ../../drivers/crypto/caam/include

cflags-$(CFG_VERAISON_ATTESTATION_PTA) += -Wno-declaration-after-statement
cflags-$(CFG_VERAISON_ATTESTATION_PTA) += -Wno-redundant-decls
