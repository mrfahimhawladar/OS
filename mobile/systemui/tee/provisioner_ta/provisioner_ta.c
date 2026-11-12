#include <tee_internal_api.h>

#include "fahim_credential_protocol.h"
#include "fahim_tui_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const TEE_UUID tui_pta_uuid = FAHIM_TUI_PTA_UUID;
static const TEE_UUID credential_ta_uuid = FAHIM_CREDENTIAL_TA_UUID;

static void secure_clear(void *buffer, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)buffer;
    while (length-- != 0u)
        *p++ = 0u;
}

static bool constant_time_equal(const uint8_t *left,
                                const uint8_t *right,
                                size_t length)
{
    volatile uint8_t difference = 0u;
    for (size_t i = 0; i < length; ++i)
        difference |= (uint8_t)(left[i] ^ right[i]);
    return difference == 0u;
}

static bool valid_pin(const uint8_t *pin, size_t length)
{
    if (pin == NULL || length < FAHIM_TUI_PIN_MIN ||
        length > FAHIM_TUI_PIN_MAX)
        return false;

    for (size_t i = 0; i < length; ++i) {
        if (pin[i] < (uint8_t)'0' || pin[i] > (uint8_t)'9')
            return false;
    }
    return true;
}

static void set_response(TEE_Param params[4], uint32_t result)
{
    fahim_provisioner_response_t response = {
        FAHIM_TUI_PROTOCOL_VERSION, result
    };
    TEE_MemMove(params[0].memref.buffer, &response, sizeof(response));
    params[0].memref.size = sizeof(response);
    secure_clear(&response, sizeof(response));
}

static TEE_Result open_internal_session(const TEE_UUID *uuid,
                                        TEE_TASessionHandle *session)
{
    TEE_Param params[4] = { };
    uint32_t origin = 0u;
    return TEE_OpenTASession(uuid, 0xffffffffu,
        TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
                        TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE),
        params, session, &origin);
}

static TEE_Result invoke_tui(TEE_TASessionHandle session, uint32_t command,
                             uint32_t prompt, uint8_t *pin,
                             size_t *pin_length)
{
    TEE_Param params[4] = { };
    uint32_t origin = 0u;
    uint32_t types = TEE_PARAM_TYPES(
        TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
        TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

    if (command == FAHIM_TUI_CMD_READ_PIN) {
        if (pin == NULL || pin_length == NULL)
            return TEE_ERROR_BAD_PARAMETERS;
        params[0].value.a = prompt;
        params[1].memref.buffer = pin;
        params[1].memref.size = FAHIM_TUI_PIN_MAX;
        types = TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT,
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);
    } else if (command == FAHIM_TUI_CMD_SHOW_STATUS) {
        params[0].value.a = prompt;
        types = TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);
    }

    TEE_Result result = TEE_InvokeTACommand(session, 0xffffffffu, command,
                                             types, params, &origin);
    if (result == TEE_SUCCESS && command == FAHIM_TUI_CMD_READ_PIN)
        *pin_length = params[1].memref.size;
    return result;
}

static TEE_Result enroll_with_credential_ta(const uint8_t *pin,
                                            size_t pin_length)
{
    TEE_TASessionHandle session = TEE_HANDLE_NULL;
    TEE_Param params[4] = { };
    fahim_credential_response_t response = { };
    uint32_t origin = 0u;
    TEE_Result result = open_internal_session(&credential_ta_uuid, &session);

    if (result != TEE_SUCCESS)
        return result;

    params[0].memref.buffer = (void *)pin;
    params[0].memref.size = pin_length;
    params[1].memref.buffer = &response;
    params[1].memref.size = sizeof(response);

    result = TEE_InvokeTACommand(
        session, 0xffffffffu, FAHIM_CRED_CMD_ENROLL,
        TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                        TEE_PARAM_TYPE_MEMREF_OUTPUT,
                        TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE),
        params, &origin);

    TEE_CloseTASession(session);

    if (result != TEE_SUCCESS)
        return result;
    if (response.protocol_version != FAHIM_CREDENTIAL_PROTOCOL_VERSION) {
        secure_clear(&response, sizeof(response));
        return TEE_ERROR_SECURITY;
    }

    switch (response.result) {
    case FAHIM_CRED_RESULT_OK:
        result = TEE_SUCCESS;
        break;
    case FAHIM_CRED_RESULT_ALREADY_ENROLLED:
        result = TEE_ERROR_ACCESS_CONFLICT;
        break;
    case FAHIM_CRED_RESULT_INVALID:
        result = TEE_ERROR_BAD_PARAMETERS;
        break;
    default:
        result = TEE_ERROR_NOT_SUPPORTED;
        break;
    }
    secure_clear(&response, sizeof(response));
    return result;
}

static TEE_Result run_secure_enrollment(uint32_t *public_result)
{
    uint8_t first[FAHIM_TUI_PIN_MAX] = { 0 };
    uint8_t confirmation[FAHIM_TUI_PIN_MAX] = { 0 };
    size_t first_length = 0u;
    size_t confirmation_length = 0u;
    TEE_TASessionHandle tui_session = TEE_HANDLE_NULL;
    TEE_Result result = TEE_ERROR_GENERIC;
    bool tui_started = false;
    uint32_t display_status = FAHIM_TUI_PROMPT_UNAVAILABLE;

    *public_result = FAHIM_PROVISION_RESULT_UNAVAILABLE;

    result = open_internal_session(&tui_pta_uuid, &tui_session);
    if (result != TEE_SUCCESS)
        goto out;

    result = invoke_tui(tui_session, FAHIM_TUI_CMD_BEGIN, 0u, NULL, NULL);
    if (result != TEE_SUCCESS)
        goto out;
    tui_started = true;

    result = invoke_tui(tui_session, FAHIM_TUI_CMD_READ_PIN,
                        FAHIM_TUI_PROMPT_ENTER_PIN, first, &first_length);
    if (result != TEE_SUCCESS) {
        *public_result = result == TEE_ERROR_CANCEL
            ? FAHIM_PROVISION_RESULT_CANCELLED
            : FAHIM_PROVISION_RESULT_UNAVAILABLE;
        goto out;
    }
    if (!valid_pin(first, first_length)) {
        *public_result = FAHIM_PROVISION_RESULT_INVALID;
        display_status = FAHIM_TUI_PROMPT_MISMATCH;
        goto out;
    }

    result = invoke_tui(tui_session, FAHIM_TUI_CMD_READ_PIN,
                        FAHIM_TUI_PROMPT_CONFIRM_PIN,
                        confirmation, &confirmation_length);
    if (result != TEE_SUCCESS) {
        *public_result = result == TEE_ERROR_CANCEL
            ? FAHIM_PROVISION_RESULT_CANCELLED
            : FAHIM_PROVISION_RESULT_UNAVAILABLE;
        goto out;
    }
    if (!valid_pin(confirmation, confirmation_length) ||
        first_length != confirmation_length ||
        !constant_time_equal(first, confirmation, first_length)) {
        *public_result = FAHIM_PROVISION_RESULT_PIN_MISMATCH;
        display_status = FAHIM_TUI_PROMPT_MISMATCH;
        goto out;
    }

    result = enroll_with_credential_ta(first, first_length);
    if (result == TEE_SUCCESS) {
        *public_result = FAHIM_PROVISION_RESULT_OK;
        display_status = FAHIM_TUI_PROMPT_SUCCESS;
    } else if (result == TEE_ERROR_ACCESS_CONFLICT) {
        *public_result = FAHIM_PROVISION_RESULT_ALREADY_ENROLLED;
        display_status = FAHIM_TUI_PROMPT_UNAVAILABLE;
    } else {
        *public_result = FAHIM_PROVISION_RESULT_UNAVAILABLE;
        display_status = FAHIM_TUI_PROMPT_UNAVAILABLE;
    }

out:
    if (tui_started) {
        (void)invoke_tui(tui_session, FAHIM_TUI_CMD_SHOW_STATUS,
                         display_status, NULL, NULL);
        (void)invoke_tui(tui_session, FAHIM_TUI_CMD_END, 0u, NULL, NULL);
    }
    if (tui_session != TEE_HANDLE_NULL)
        TEE_CloseTASession(tui_session);
    secure_clear(first, sizeof(first));
    secure_clear(confirmation, sizeof(confirmation));
    return result;
}

TEE_Result TA_CreateEntryPoint(void)
{
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param params[4],
                                    void **session_context)
{
    TEE_Identity identity = { };
    TEE_Result result;

    (void)params;
    (void)session_context;
    if (param_types != TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
        return TEE_ERROR_BAD_PARAMETERS;

    result = TEE_GetPropertyAsIdentity(
        TEE_PROPSET_CURRENT_CLIENT, "gpd.client.identity", &identity);
    if (result != TEE_SUCCESS)
        return result;

    /* Only a normal-world client may request setup; it never provides the PIN. */
    if (identity.login != TEE_LOGIN_PUBLIC)
        return TEE_ERROR_ACCESS_DENIED;

    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *session_context)
{
    (void)session_context;
}

TEE_Result TA_InvokeCommandEntryPoint(void *session_context,
                                      uint32_t command_id,
                                      uint32_t param_types,
                                      TEE_Param params[4])
{
    uint32_t public_result = FAHIM_PROVISION_RESULT_UNAVAILABLE;

    (void)session_context;

    if (command_id != FAHIM_PROVISION_CMD_ENROLL ||
        param_types != TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE) ||
        params[0].memref.buffer == NULL ||
        params[0].memref.size < sizeof(fahim_provisioner_response_t))
        return TEE_ERROR_BAD_PARAMETERS;

    (void)run_secure_enrollment(&public_result);
    set_response(params, public_result);

    /* Detailed status is returned in the public response; PIN data never is. */
    return TEE_SUCCESS;
}
