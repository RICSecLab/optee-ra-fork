# The UUID for the Trusted Application
BINARY=5dea4951-718e-43c5-b358-cbec66b9d309

# Force debug logging (must be before include)
CFG_TEE_TA_LOG_LEVEL := 4

include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

# Also add CFLAGS to ensure logging is enabled
cflags-y += -DCFG_TEE_TA_LOG_LEVEL=4

# Ensure libgcc resolves to an absolute path to avoid "cannot find libgcc.a"
override libgcc$(sm) := $(shell $(CC$(sm)) -print-file-name=libgcc.a)
