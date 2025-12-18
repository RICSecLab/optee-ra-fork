#!/bin/bash
# Setup PTA build configuration if not already done
PTA_SUB_MK="/optee/optee_os/core/pta/sub.mk"
PTA_ENTRY="subdirs-y += remote_attestation"

if ! grep -q "remote_attestation" "$PTA_SUB_MK" 2>/dev/null; then
    echo "" >> "$PTA_SUB_MK"
    echo "$PTA_ENTRY" >> "$PTA_SUB_MK"
    echo "Added remote_attestation to PTA build configuration."
fi

exec "$@"
