#ifndef PTA_REMOTE_ATTESTATION_BASE64_H
#define PTA_REMOTE_ATTESTATION_BASE64_H

#include <stddef.h>
#include <stdint.h>

size_t pta_base64_enc_len(size_t size);

int pta_base64_encode(const unsigned char *in, unsigned long inlen, char *out,
                  unsigned long *outlen);

#endif /*PTA_REMOTE_ATTESTATION_BASE64_H*/
