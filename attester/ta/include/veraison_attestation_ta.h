/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Veraison Attestation TA header file
 */

#ifndef __VERAISON_ATTESTATION_TA_H
#define __VERAISON_ATTESTATION_TA_H

/* TA UUID - must match Makefile BINARY setting */
#define TA_VERAISON_ATTESTATION_UUID \
	{ 0xc7e478c2, 0x89b3, 0x46eb, \
		{ 0xac, 0x19, 0x57, 0x1e, 0x66, 0xc3, 0x83, 0x0d } }

/* TA Command IDs */
#define TA_VERAISON_ATTESTATION_CMD_GENERATE_BLACKKEY    0
#define TA_VERAISON_ATTESTATION_CMD_CONVERT_TO_BLACKKEY  1
#define TA_VERAISON_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE    2

/* Implementation ID (32 bytes) */
#define IMPLEMENTATION_ID_LEN 32
#define IMPLEMENTATION_ID { \
    0x61, 0x63, 0x6d, 0x65, 0x2d, 0x69, 0x6d, 0x70, \
    0x6c, 0x65, 0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, \
    0x69, 0x6f, 0x6e, 0x2d, 0x69, 0x64, 0x2d, 0x30, \
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x31  \
}

#endif /* __VERAISON_ATTESTATION_TA_H */
