#ifndef FAHIM_TUI_PROTOCOL_H
#define FAHIM_TUI_PROTOCOL_H

#include <stdint.h>

/* Provisioner TA: 28e3b2d4-4a20-4f91-9a11-464148494d50 ("FAHIMP"). */
#define FAHIM_PROVISIONER_TA_UUID \
    { 0x28e3b2d4, 0x4a20, 0x4f91, \
      { 0x9a, 0x11, 0x46, 0x41, 0x48, 0x49, 0x4d, 0x50 } }

/* OP-TEE core pseudo-TA: board secure-display/input provider contract. */
#define FAHIM_TUI_PTA_UUID \
    { 0x5d6f9a21, 0x8bc4, 0x4f22, \
      { 0x91, 0x10, 0x46, 0x41, 0x48, 0x49, 0x4d, 0x54 } }

#define FAHIM_TUI_PIN_MIN 6u
#define FAHIM_TUI_PIN_MAX 16u
#define FAHIM_TUI_PROTOCOL_VERSION 1u

enum fahim_tui_command {
    FAHIM_TUI_CMD_BEGIN = 1u,
    FAHIM_TUI_CMD_READ_PIN = 2u,
    FAHIM_TUI_CMD_END = 3u,
    FAHIM_TUI_CMD_SHOW_STATUS = 4u
};

enum fahim_tui_prompt {
    FAHIM_TUI_PROMPT_ENTER_PIN = 1u,
    FAHIM_TUI_PROMPT_CONFIRM_PIN = 2u,
    FAHIM_TUI_PROMPT_SUCCESS = 3u,
    FAHIM_TUI_PROMPT_MISMATCH = 4u,
    FAHIM_TUI_PROMPT_UNAVAILABLE = 5u
};

enum fahim_provisioner_command {
    FAHIM_PROVISION_CMD_ENROLL = 1u
};

enum fahim_provisioner_result {
    FAHIM_PROVISION_RESULT_OK = 0u,
    FAHIM_PROVISION_RESULT_NOT_ENROLLED = 1u,
    FAHIM_PROVISION_RESULT_ALREADY_ENROLLED = 2u,
    FAHIM_PROVISION_RESULT_PIN_MISMATCH = 3u,
    FAHIM_PROVISION_RESULT_CANCELLED = 4u,
    FAHIM_PROVISION_RESULT_UNAVAILABLE = 5u,
    FAHIM_PROVISION_RESULT_INVALID = 6u
};

typedef struct fahim_provisioner_response {
    uint32_t protocol_version;
    uint32_t result;
} fahim_provisioner_response_t;

#endif
