// SPDX-License-Identifier: BSD-2-Clause
/*
 * Fahim OS secure Trusted UI bridge for OP-TEE core.
 *
 * The weak HAL hooks are deliberately unavailable. A board-specific secure
 * display/touch implementation must provide strong overrides before enrollment
 * can succeed. This PTA never accepts PIN bytes as input parameters.
 */
#include <compiler.h>
#include <kernel/pseudo_ta.h>
#include <kernel/tee_ta_manager.h>
#include <kernel/ts_manager.h>
#include <kernel/user_access.h>
#include <kernel/user_mode_ctx.h>
#include <string.h>
#include <tee_api_defines.h>
#include <tee_api_types.h>

#include "fahim_tui_protocol.h"
#include "fahim_tui_platform_hal.h"

static const TEE_UUID provisioner_uuid = FAHIM_PROVISIONER_TA_UUID;

static void secure_clear(void *buffer, size_t length)
{
	volatile uint8_t *p = buffer;

	while (length--)
		*p++ = 0;
}

static bool caller_is_provisioner(void)
{
	struct ts_session *s = ts_get_current_session();
	struct tee_ta_session *ta = NULL;

	if (!s)
		return false;

	ta = to_ta_session(s);
	if (ta->clnt_id.login != TEE_LOGIN_TRUSTED_APP)
		return false;

	return !memcmp(&ta->clnt_id.uuid, &provisioner_uuid,
		       sizeof(provisioner_uuid));
}

/* Safe defaults: no enrollment is possible until secure hardware is wired. */
__weak TEE_Result fahim_tui_platform_begin(void)
{
	return TEE_ERROR_NOT_SUPPORTED;
}

__weak TEE_Result fahim_tui_platform_read_pin(uint32_t prompt,
					      uint8_t *secure_pin_buffer,
					      size_t capacity,
					      size_t *actual_length)
{
	(void)prompt;
	(void)secure_pin_buffer;
	(void)capacity;
	if (actual_length)
		*actual_length = 0;
	return TEE_ERROR_NOT_SUPPORTED;
}

__weak TEE_Result fahim_tui_platform_show_status(uint32_t status)
{
	(void)status;
	return TEE_ERROR_NOT_SUPPORTED;
}

__weak TEE_Result fahim_tui_platform_end(void)
{
	return TEE_SUCCESS;
}

static TEE_Result open_session(uint32_t param_types __unused,
			       TEE_Param params[TEE_NUM_PARAMS] __unused,
			       void **session_context __unused)
{
	if (!caller_is_provisioner())
		return TEE_ERROR_ACCESS_DENIED;
	return TEE_SUCCESS;
}

static TEE_Result invoke_command(void *session_context __unused,
				 uint32_t command,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	struct ts_session *s = ts_get_current_session();
	struct user_mode_ctx *uctx = NULL;

	if (!caller_is_provisioner() || !s || !is_user_ta_ctx(s->ctx))
		return TEE_ERROR_ACCESS_DENIED;

	uctx = to_user_mode_ctx(s->ctx);

	switch (command) {
	case FAHIM_TUI_CMD_BEGIN:
		if (param_types != TEE_PARAM_TYPES(
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
			return TEE_ERROR_BAD_PARAMETERS;
		return fahim_tui_platform_begin();

	case FAHIM_TUI_CMD_READ_PIN: {
		uint8_t pin[FAHIM_TUI_PIN_MAX] = { 0 };
		size_t length = 0;
		uint32_t prompt = 0;
		TEE_Result res = TEE_ERROR_GENERIC;

		if (param_types != TEE_PARAM_TYPES(
				TEE_PARAM_TYPE_VALUE_INPUT,
				TEE_PARAM_TYPE_MEMREF_OUTPUT,
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE) ||
		    params[1].memref.size < FAHIM_TUI_PIN_MAX ||
		    !params[1].memref.buffer)
			return TEE_ERROR_BAD_PARAMETERS;

		prompt = params[0].value.a;
		if (prompt != FAHIM_TUI_PROMPT_ENTER_PIN &&
		    prompt != FAHIM_TUI_PROMPT_CONFIRM_PIN)
			return TEE_ERROR_BAD_PARAMETERS;

		/*
		 * The HAL obtains input directly from the secure input driver.
		 * The PIN is first written into this core-private buffer, never
		 * supplied by the REE caller or its shared-memory parameters.
		 */
		res = fahim_tui_platform_read_pin(prompt, pin, sizeof(pin), &length);
		if (res != TEE_SUCCESS)
			goto out;

		if (length < FAHIM_TUI_PIN_MIN || length > sizeof(pin)) {
			res = TEE_ERROR_BAD_PARAMETERS;
			goto out;
		}

		for (size_t i = 0; i < length; i++) {
			if (pin[i] < (uint8_t)'0' || pin[i] > (uint8_t)'9') {
				res = TEE_ERROR_BAD_PARAMETERS;
				goto out;
			}
		}

		res = copy_to_user(params[1].memref.buffer, pin, length);
		if (res == TEE_SUCCESS)
			params[1].memref.size = length;
out:
		secure_clear(pin, sizeof(pin));
		if (res != TEE_SUCCESS)
			params[1].memref.size = 0;
		return res;
	}

	case FAHIM_TUI_CMD_SHOW_STATUS:
		if (param_types != TEE_PARAM_TYPES(
				TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE) ||
		    params[0].value.a < FAHIM_TUI_PROMPT_SUCCESS ||
		    params[0].value.a > FAHIM_TUI_PROMPT_UNAVAILABLE)
			return TEE_ERROR_BAD_PARAMETERS;
		return fahim_tui_platform_show_status(params[0].value.a);

	case FAHIM_TUI_CMD_END:
		if (param_types != TEE_PARAM_TYPES(
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
			return TEE_ERROR_BAD_PARAMETERS;
		return fahim_tui_platform_end();

	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}

pseudo_ta_register(.uuid = FAHIM_TUI_PTA_UUID,
		   .name = "fahim.trusted-ui.pta",
		   .flags = PTA_DEFAULT_FLAGS,
		   .open_session_entry_point = open_session,
		   .invoke_command_entry_point = invoke_command);
