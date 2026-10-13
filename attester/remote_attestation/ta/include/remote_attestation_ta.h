#ifndef TA_REMOTE_ATTESTATION_H
#define TA_REMOTE_ATTESTATION_H

/*
 * This UUID is generated with uuidgen
 * the ITU-T UUID generator at http://www.itu.int/ITU-T/asn1/uuid.html
 */
#define TA_REMOTE_ATTESTATION_UUID                                             \
    {                                                                          \
        0xa6b53b34, 0x855f, 0x11ee, {                                          \
            0xb9, 0xd1, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02                     \
        }                                                                      \
    }

/*
 * The function ID(s) implemented in this TA. The signing key never leaves
 * the PTA: it is generated on first use, or installed once with IMPORT_KEY
 * before that, and never replaced. The TA forwards the key commands and
 * only ever sees the public key.
 *
 *   GEN_CBOR_EVIDENCE: [in] nonce, [out] evidence
 *   IMPORT_KEY:        [in] public key X, [in] public key Y, [in] private key
 *   GET_PUBLIC_KEY:    [out] public key X, [out] public key Y
 */
#define TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE 0
#define TA_REMOTE_ATTESTATION_CMD_IMPORT_KEY 1
#define TA_REMOTE_ATTESTATION_CMD_GET_PUBLIC_KEY 2

/* ECDSA P-256 coordinates and scalars */
#define PUBKEY_COORD_SIZE 32

/*
 * PSA implementation-id the TA reports: identifies the implementation (the
 * design), not the device. Override with CFG_RA_IMPLEMENTATION_ID at build
 * time; must be 32 bytes. The default is the QEMU value of the provisioning
 * data; the i.MX Yocto recipe passes imx8mp-optee-ra-0000000000000001.
 */
#ifndef RA_IMPLEMENTATION_ID
#define RA_IMPLEMENTATION_ID "qemu-optee-ra-000000000000000001"
#endif

/*
 * Commands of the veraison attestation PTA beyond GET_CBOR_EVIDENCE, as in
 * the upstream pta_veraison_attestation.h. Kept here until the TA dev kit
 * ships a header that has them.
 */
#ifndef PTA_VERAISON_ATTESTATION_UUID
#define PTA_VERAISON_ATTESTATION_UUID                                          \
    {                                                                          \
        0xa77955f9, 0xeea1, 0x44fd, {                                          \
            0xad, 0xd5, 0x4a, 0x9d, 0x96, 0x2a, 0xfc, 0xf5                     \
        }                                                                      \
    }
#endif
#ifndef PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE
#define PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE 0x0
#endif
#ifndef PTA_VERAISON_ATTESTATION_GET_PUBLIC_KEY
#define PTA_VERAISON_ATTESTATION_GET_PUBLIC_KEY 0x1
#endif
#ifndef PTA_VERAISON_ATTESTATION_IMPORT_KEY
#define PTA_VERAISON_ATTESTATION_IMPORT_KEY 0x2
#endif

#if defined(HOST_BUILD)
typedef TEEC_UUID UUID_TYPE;
#else
typedef TEE_UUID UUID_TYPE;
#endif

#endif /*TA_REMOTE_ATTESTATION_H*/
