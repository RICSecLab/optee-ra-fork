// SPDX-License-Identifier: BSD-3-Clause
#ifndef CLIENT_H
#define CLIENT_H

#include "veraison_client_wrapper.h"

// Default URL for Docker environment
// Override with VERAISON_URL environment variable for real hardware
// Example: export VERAISON_URL="http://192.168.1.100:8087"
#define SERVER_BASE_URL_DEFAULT "http://relying-party-service:8087"
#define PSA_TYPE_NAME           "application/psa-attestation-token"

const char *get_server_base_url(void);
ChallengeResponseSession *open_session();
VeraisonResult post_evidence(ChallengeResponseSession *session,
                             unsigned char *evidence, size_t evidence_len);

#endif // CLIENT_H
