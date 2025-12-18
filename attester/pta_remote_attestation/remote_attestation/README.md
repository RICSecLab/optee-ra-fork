# Veraison Attestation PTA

This is a proof of concept for adding attestation capabilities to S-EL0 TAs and a demonstrator of an end-to-end remote attestation protocol [1] using the Veraison verifier [2].

For convenience, this PTA reuses the PSA token format [3]. However, note that PSA semantics do not fully apply, as many relevant properties required by the PSA SM [4] are not met.

Furthermore, the attestation evidence produced by the PTA attests to the memory contents of the calling TA, but there is no way to establish trust in the PTA in the first place.

For these reasons, the PTA should not be regarded as a best practice example for real-world attestation.

Instead, this PTA aims to demonstrate the integration of various libraries and tools to create a trusted application focused on attestation within the OP-TEE environment and to practically explore an end-to-end remote attestation flow using this approach.

## Known Limitations

1. **PSA Semantics Limitations:** Although this PTA reuses the PSA token format, many of the relevant properties required by the PSA Security Model (SM) are not met. This can impact the effectiveness and security assumptions typically expected from PSA-based attestation.

2. **Lack of Trust in the PTA:** The attestation evidence produced by the PTA attests to the memory contents of the calling TA, but there is no mechanism to establish trust in the PTA itself from a lower-level entity, such as the bootloader. Without such anchoring to a platform Root of Trust (RoT), the PTA lacks foundational trust, which weakens the overall chain of trust.

## Provisioning vs. Operation

Typical workflow:

1. Provisioning (one-time)
   - Host runs: `optee_remote_attestation --provision`
   - TA opens the PTA and triggers key generation.
     - CAAM enabled: PTA returns a CAAM black key blob and public X/Y; host prints the blob (hex) and public key.
     - CAAM disabled: not supported by PTA; provisioning is expected to have been handled already off-device.

2. Operation (per attestation)
   - Host runs without flags; optional `--key-hex` or `--key-file` can supply a key blob that the TA passes to PTA as `memref[3]`.
   - On CAAM devices the blob is a black key; on non-CAAM devices it can be a software private key `d`. If no key is supplied, PTA may fall back per its build-time policy.

## Dual-mode operation (CAAM vs. non-CAAM)

This PTA supports two signing modes:

1. CAAM-enabled platforms (CFG_NXP_CAAM_ECC_DRV=y)
   - `PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR` generates an ECC P-256 keypair via CAAM.
   - The private key is returned as a serialized CAAM black key (opaque blob bound to the device).
   - The TA passes this blob back to the PTA as `memref[3]` when calling `PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE` so the PTA signs using the CAAM-backed key.

2. Non-CAAM platforms
   - `PTA_VERAISON_ATTESTATION_GENERATE_ECC_KEYPAIR` returns `TEE_ERROR_NOT_SUPPORTED`.
   - During provisioning the TA generates a software key and stores the private `d`.
   - `PTA_VERAISON_ATTESTATION_GET_CBOR_EVIDENCE` accepts a provided plaintext `d` via `memref[3]` and uses it for signing. If no key is provisioned, and `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY=y`, PTA can fall back to the embedded test key; otherwise returns `TEE_ERROR_NOT_SUPPORTED`.

Build-time flags:

- `CFG_NXP_CAAM_ECC_DRV` (default: platform dependent): enables CAAM-backed ECC key generation and signing using a provided black key.
- `CFG_VERAISON_ATTESTATION_PTA_TEST_KEY` (default: n): enables an embedded P-256 keypair for non-CAAM environments and testing. Do not enable for production.

Security note: In non-CAAM mode the private key is either provisioned into TEE secure storage (preferred) or, if enabled, compiled into the PTA image for testing. Neither binds to hardware, thus weaker than CAAM black keys.

## References

[1] https://datatracker.ietf.org/doc/rfc9334
[2] https://github.com/veraison/services
[3] https://datatracker.ietf.org/doc/draft-tschofenig-rats-psa-token
[4] https://www.psacertified.org/app/uploads/2021/12/JSADEN014_PSA_Certified_SM_V1.1_BET0.pdf
