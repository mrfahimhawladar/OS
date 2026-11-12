#ifndef FAHIM_CREDENTIAL_PROTOCOL_H
#define FAHIM_CREDENTIAL_PROTOCOL_H

#include <stdint.h>

#define FAHIM_CREDENTIAL_PROTOCOL_VERSION 1u
#define FAHIM_CREDENTIAL_PIN_MIN 4u
#define FAHIM_CREDENTIAL_PIN_MAX 16u
#define FAHIM_CREDENTIAL_SALT_BYTES 32u
#define FAHIM_CREDENTIAL_DIGEST_BYTES 32u

/* UUID: 9f4d1e72-6a0b-4a31-9b60-464148494d43 ("FAHIMC"). */
#define FAHIM_CREDENTIAL_TA_UUID \
    { 0x9f4d1e72, 0x6a0b, 0x4a31, \
      { 0x9b, 0x60, 0x46, 0x41, 0x48, 0x49, 0x4d, 0x43 } }

enum fahim_credential_command {
    FAHIM_CRED_CMD_QUERY = 1u,
    FAHIM_CRED_CMD_ENROLL = 2u,
    FAHIM_CRED_CMD_VERIFY = 3u
};

enum fahim_credential_result {
    FAHIM_CRED_RESULT_OK = 0u,
    FAHIM_CRED_RESULT_REJECTED = 1u,
    FAHIM_CRED_RESULT_LOCKED_OUT = 2u,
    FAHIM_CRED_RESULT_NOT_ENROLLED = 3u,
    FAHIM_CRED_RESULT_ALREADY_ENROLLED = 4u,
    FAHIM_CRED_RESULT_INVALID = 5u,
    FAHIM_CRED_RESULT_UNAVAILABLE = 6u
};

/* This structure is a public status message, never a PIN or verifier record. */
typedef struct fahim_credential_response {
    uint32_t protocol_version;
    uint32_t result;
    uint32_t enrolled;
    uint32_t failed_attempts;
    uint64_t retry_after_ms;
} fahim_credential_response_t;

#endif
