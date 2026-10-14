global-incdirs-y += include
srcs-y += remote_attestation_ta.c

# Performance measurement logging (pass CFG_REMOTE_ATTESTATION_PERF=y)
cflags-$(CFG_REMOTE_ATTESTATION_PERF) += -DCFG_REMOTE_ATTESTATION_PERF=1

# PSA implementation-id reported by the TA (32 bytes); see remote_attestation_ta.h
ifneq ($(CFG_RA_IMPLEMENTATION_ID),)
cflags-y += -DRA_IMPLEMENTATION_ID='"$(CFG_RA_IMPLEMENTATION_ID)"'
endif
