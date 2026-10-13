#include <err.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/sha.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

/* For the UUID (found in the TA's h-file(s)) */
#include <remote_attestation_ta.h>

#include "client.h"
#include "perf.h"

/* Performance measurement logging (see perf.h) */
int ra_perf_enabled = 0;
const char *ra_perf_keymode = "pta";

uint64_t ra_perf_now_us(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000;
}

void ra_perf_log(const char *event, uint64_t duration_us) {
    if (ra_perf_enabled)
        printf("RA_PERF|host|%s|%" PRIu64 "|key=%s\n", event, duration_us,
               ra_perf_keymode);
}

static int get_random_nonce(uint8_t *nonce, size_t sz) {
    FILE *f = fopen("/dev/urandom", "rb");
    size_t n = 0;

    if (!f)
        return -1;
    n = fread(nonce, 1, sz, f);
    fclose(f);
    return n == sz ? 0 : -1;
}

void print_binary_in_hex(uint8_t *buf, size_t sz) {
    int i = 0;
    for (i = 0; i < sz; i++)
        fprintf(stdout, "%02x", buf[i]);

    printf("\n");

    return;
}

static int hex_value(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *hex, uint8_t **out, size_t *out_len) {
    size_t n = strlen(hex);
    size_t i = 0, j = 0;
    /* skip 0x prefix if present */
    if (n >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2; n -= 2;
    }
    if (n % 2 != 0) return -1;
    uint8_t *buf = (uint8_t *)malloc(n / 2);
    if (!buf) return -1;
    for (i = 0, j = 0; i < n; i += 2, j++) {
        int hi = hex_value(hex[i]);
        int lo = hex_value(hex[i+1]);
        if (hi < 0 || lo < 0) { free(buf); return -1; }
        buf[j] = (uint8_t)((hi << 4) | lo);
    }
    *out = buf;
    *out_len = j;
    return 0;
}

/* Parse a 32-byte hex value for an option, exit on error */
static uint8_t *parse_hex32(const char *opt, const char *hex) {
    uint8_t *buf = NULL;
    size_t len = 0;

    if (parse_hex(hex, &buf, &len) != 0)
        errx(1, "Invalid %s value", opt);
    if (len != PUBKEY_COORD_SIZE)
        errx(1, "%s must be exactly %d bytes", opt, PUBKEY_COORD_SIZE);
    return buf;
}

/*
 * Print a public key and the PSA instance-id the PTA derives from it:
 * 0x01 followed by the SHA-256 of the SEC 1 uncompressed point.
 */
static void print_public_key(const uint8_t *pub_x, const uint8_t *pub_y) {
    uint8_t point[1 + 2 * PUBKEY_COORD_SIZE] = {0x04};
    uint8_t instance_id[1 + SHA256_DIGEST_LENGTH] = {0x01};

    memcpy(point + 1, pub_x, PUBKEY_COORD_SIZE);
    memcpy(point + 1 + PUBKEY_COORD_SIZE, pub_y, PUBKEY_COORD_SIZE);
    SHA256(point, sizeof(point), instance_id + 1);

    printf("PubX(hex): ");
    print_binary_in_hex((uint8_t *)pub_x, PUBKEY_COORD_SIZE);
    printf("PubY(hex): ");
    print_binary_in_hex((uint8_t *)pub_y, PUBKEY_COORD_SIZE);
    printf("InstanceID(hex): ");
    print_binary_in_hex(instance_id, sizeof(instance_id));
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --help, -h                Show this help and exit\n");
    printf("  --import-key HEX          Install a P-256 private key (32 bytes hex) in the PTA;\n");
    printf("                            needs --pubx-hex and --puby-hex. Only possible\n");
    printf("                            before the PTA has a key\n");
    printf("  --get-pubkey              Print the public key of the PTA's signing key,\n");
    printf("                            generating the key if there is none yet\n");
    printf("  --pubx-hex HEX            Public key X coordinate (32 bytes hex)\n");
    printf("  --puby-hex HEX            Public key Y coordinate (32 bytes hex)\n");
    printf("  --perf                    Print RA_PERF timing logs (or RA_PERF=1)\n");
    printf("  --loop N                  Repeat the attestation N times\n");
    printf("  --no-server               Skip the verifier; use a local random nonce\n");
    printf("\n");
    printf("The signing key never leaves the PTA and is never replaced: the\n");
    printf("evidence is always signed with the key the PTA holds (or its\n");
    printf("built-in test key).\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    TEEC_Result res = TEEC_SUCCESS;
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_UUID ta_uuid = TA_REMOTE_ATTESTATION_UUID;
    uint32_t err_origin;

    uint8_t *host_pub_x = NULL;
    uint8_t *host_pub_y = NULL;
    uint8_t *host_priv = NULL;
    bool mode_import = false;
    bool mode_get_pubkey = false;
    bool no_server = false;
    long loop_count = 1;

    {
        const char *env_perf = getenv("RA_PERF");

        if (env_perf && strcmp(env_perf, "0") != 0)
            ra_perf_enabled = 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--generate-key") == 0 ||
                   strcmp(argv[i], "--generate-blackkey") == 0) {
            errx(1, "%s is no longer supported: the PTA generates its key "
                 "on first use, see --get-pubkey", argv[i]);
        } else if (strcmp(argv[i], "--import-key") == 0 ||
                   strcmp(argv[i], "--convert-key") == 0) {
            if (host_priv)
                errx(1, "--import-key specified multiple times");
            if (i + 1 >= argc)
                errx(1, "Missing value for --import-key");
            host_priv = parse_hex32("--import-key", argv[i + 1]);
            mode_import = true;
            i++;
        } else if (strcmp(argv[i], "--get-pubkey") == 0) {
            mode_get_pubkey = true;
        } else if (strcmp(argv[i], "--pubx-hex") == 0) {
            if (host_pub_x)
                errx(1, "--pubx-hex specified multiple times");
            if (i + 1 >= argc)
                errx(1, "Missing value for --pubx-hex");
            host_pub_x = parse_hex32("--pubx-hex", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--puby-hex") == 0) {
            if (host_pub_y)
                errx(1, "--puby-hex specified multiple times");
            if (i + 1 >= argc)
                errx(1, "Missing value for --puby-hex");
            host_pub_y = parse_hex32("--puby-hex", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--key-hex") == 0) {
            errx(1, "--key-hex is no longer supported: the signing key is "
                 "held by the PTA, see --get-pubkey and --import-key");
        } else if (strcmp(argv[i], "--perf") == 0) {
            ra_perf_enabled = 1;
        } else if (strcmp(argv[i], "--loop") == 0) {
            if (i + 1 >= argc)
                errx(1, "Missing value for --loop");
            loop_count = strtol(argv[i + 1], NULL, 10);
            if (loop_count < 1)
                errx(1, "Invalid --loop value");
            i++;
        } else if (strcmp(argv[i], "--no-server") == 0) {
            no_server = true;
        } else {
            errx(1, "Unknown argument: %s", argv[i]);
        }
    }

    if (mode_import && mode_get_pubkey)
        errx(1, "--import-key and --get-pubkey are exclusive");
    if (mode_import && (!host_pub_x || !host_pub_y))
        errx(1, "--import-key needs --pubx-hex and --puby-hex");
    if (!mode_import && (host_pub_x || host_pub_y))
        errx(1, "--pubx-hex/--puby-hex only go with --import-key");

    uint64_t t0 = 0;

    /* Initialize a context connecting us to the TEE */
    t0 = ra_perf_now_us();
    res = TEEC_InitializeContext(NULL, &ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitializeContext failed with code 0x%x", res);
    ra_perf_log("teec_init", ra_perf_now_us() - t0);

    t0 = ra_perf_now_us();
    res = TEEC_OpenSession(&ctx, &sess, &ta_uuid, TEEC_LOGIN_PUBLIC, NULL, NULL,
                           &err_origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x", res,
             err_origin);
    ra_perf_log("teec_open_session", ra_perf_now_us() - t0);

    /* Install a key generated elsewhere in the PTA */
    if (mode_import) {
        TEEC_Operation op = {0};

        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_INPUT, TEEC_NONE);
        op.params[0].tmpref.buffer = host_pub_x;
        op.params[0].tmpref.size = PUBKEY_COORD_SIZE;
        op.params[1].tmpref.buffer = host_pub_y;
        op.params[1].tmpref.size = PUBKEY_COORD_SIZE;
        op.params[2].tmpref.buffer = host_priv;
        op.params[2].tmpref.size = PUBKEY_COORD_SIZE;

        printf("\nImporting the signing key into the PTA...\n");
        t0 = ra_perf_now_us();
        res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATION_CMD_IMPORT_KEY,
                                 &op, &err_origin);
        if (res != TEEC_SUCCESS)
            errx(1, "Import key failed 0x%x origin 0x%x", res, err_origin);
        ra_perf_log("key_import", ra_perf_now_us() - t0);

        print_public_key(host_pub_x, host_pub_y);
        printf("Key import completed.\n");
        return 0;
    }

    /* Read back the public key, which generates the key on first use */
    if (mode_get_pubkey) {
        TEEC_Operation op = {0};
        uint8_t pub_x[PUBKEY_COORD_SIZE] = {0};
        uint8_t pub_y[PUBKEY_COORD_SIZE] = {0};

        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = pub_x;
        op.params[0].tmpref.size = sizeof(pub_x);
        op.params[1].tmpref.buffer = pub_y;
        op.params[1].tmpref.size = sizeof(pub_y);

        t0 = ra_perf_now_us();
        res = TEEC_InvokeCommand(&sess, TA_REMOTE_ATTESTATION_CMD_GET_PUBLIC_KEY,
                                 &op, &err_origin);
        if (res != TEEC_SUCCESS)
            errx(1, "Get public key failed 0x%x origin 0x%x", res,
                 err_origin);
        ra_perf_log("key_get_pubkey", ra_perf_now_us() - t0);

        print_public_key(pub_x, pub_y);
        return 0;
    }

    for (long iter = 0; iter < loop_count; iter++) {
        ChallengeResponseSession *session = NULL;
        uint8_t local_nonce[32] = {0};
        const uint8_t *nonce = NULL;
        size_t nonce_sz = 0;
        uint64_t t_total = ra_perf_now_us();

        if (no_server) {
            /* No verifier round trip: attest against a local random nonce */
            if (get_random_nonce(local_nonce, sizeof(local_nonce)) != 0)
                errx(1, "Failed to generate a local nonce");
            nonce = local_nonce;
            nonce_sz = sizeof(local_nonce);
        } else {
            /* Connect to the server and establish a session */
            session = open_session();
            if (session == NULL) {
                printf("Failed to open session.\n");
                return 1;
            }
            nonce = (const uint8_t *)session->nonce;
            nonce_sz = session->nonce_size;
        }

        /* Request TA to issue evidence based on a given nonce */
        /* The buffer allocated here must be large enough to hold the CBOR
         * evidece */
        uint8_t cbor_evidence[1024] = {0};
        TEEC_Operation op = {0};

        /* Params: nonce(in), output(out) */
        op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                         TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
                                         TEEC_NONE);
        op.params[0].tmpref.buffer = (uint8_t *)nonce;
        op.params[0].tmpref.size = nonce_sz;
        op.params[1].tmpref.buffer = cbor_evidence;
        op.params[1].tmpref.size = sizeof(cbor_evidence);

        printf("\nInvoke TA.\n");
        t0 = ra_perf_now_us();
        res = TEEC_InvokeCommand(&sess,
                                 TA_REMOTE_ATTESTATOIN_CMD_GEN_CBOR_EVIDENCE,
                                 &op, &err_origin);
        if (res != TEEC_SUCCESS)
            errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
                 res, err_origin);
        ra_perf_log("evidence_get", ra_perf_now_us() - t0);

        printf("Invoked TA successfully.\n\n\n");

        /* Receive CBOR(COSE) evidence from PTA */
        printf("Received evidence of CBOR (COSE) format from PTA.\n\n");

        t0 = ra_perf_now_us();
        printf("CBOR(COSE) size: %ld\n", op.params[1].tmpref.size);
        printf("CBOR(COSE): ");
        print_binary_in_hex(op.params[1].tmpref.buffer,
                            op.params[1].tmpref.size);
        printf("\n\n");
        ra_perf_log("print_evidence", ra_perf_now_us() - t0);

        /* Send the generated evidence to the session just established */
        if (!no_server)
            post_evidence(session, op.params[1].tmpref.buffer,
                          op.params[1].tmpref.size);

        ra_perf_log("total", ra_perf_now_us() - t_total);
    }

    free(host_priv);
    free(host_pub_x);
    free(host_pub_y);

    return 0;
}
