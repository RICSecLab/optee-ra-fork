// SPDX-License-Identifier: BSD-3-Clause
#ifndef VERAISON_CLIENT_WRAPPER_H
#define VERAISON_CLIENT_WRAPPER_H

#include <stddef.h>
#include <stdint.h>

typedef enum VeraisonResult {
    Ok = 0,
    Error = 1,
} VeraisonResult;

typedef struct ChallengeResponseSession {
    char *session_url;
    size_t accept_type_count;
    char **accept_type_list;
    uint8_t *nonce;
    size_t nonce_size;
    char *message;
    char *attestation_result;
} ChallengeResponseSession;

VeraisonResult open_challenge_response_session(const char *endpoint,
                                               size_t desired_nonce_size,
                                               const char *accept_type,
                                               ChallengeResponseSession **out_session);

void free_challenge_response_session(ChallengeResponseSession *session);

VeraisonResult challenge_response(ChallengeResponseSession *session,
                                 size_t evidence_len,
                                 const unsigned char *evidence,
                                 const char *media_type);

#endif /* VERAISON_CLIENT_WRAPPER_H */


