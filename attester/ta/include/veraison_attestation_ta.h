/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Veraison Attestation TA header file
 */

#ifndef __VERAISON_ATTESTATION_TA_H
#define __VERAISON_ATTESTATION_TA_H

/* TA UUID - different from PTA UUID */
#define TA_VERAISON_ATTESTATION_UUID \
	{ 0x5dea4951, 0x718e, 0x43c5, \
		{ 0xb3, 0x58, 0xcb, 0xec, 0x66, 0xb9, 0xd3, 0x09 } }

/* TA Command IDs */
/* Note: typo preserved for backward compatibility with host code */
#define TA_VERAISON_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE    0
#define TA_VERAISON_ATTESTATION_CMD_GENERATE_BLACKKEY    1
#define TA_VERAISON_ATTESTATION_CMD_CONVERT_TO_BLACKKEY  2

/* Implementation ID (32 bytes) */
#define IMPLEMENTATION_ID_LEN 32
#define IMPLEMENTATION_ID { \
    0x61, 0x63, 0x6d, 0x65, 0x2d, 0x69, 0x6d, 0x70, \
    0x6c, 0x65, 0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, \
    0x69, 0x6f, 0x6e, 0x2d, 0x69, 0x64, 0x2d, 0x30, \
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31  }

#endif /* __VERAISON_ATTESTATION_TA_H */
