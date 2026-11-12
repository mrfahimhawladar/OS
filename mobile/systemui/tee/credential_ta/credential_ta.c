#include <stdbool.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include "fahim_credential_protocol.h"

#define CREDENTIAL_STORAGE_ID TEE_STORAGE_PRIVATE_RPMB
#define CREDENTIAL_RECORD_MAGIC 0x46435244u /* FCRD */
#define CREDENTIAL_RECORD_VERSION 1u
#define CREDENTIAL_FAILURE_LIMIT 5u
#define CREDENTIAL_LOCKOUT_BASE_MS 30000ull
#define CREDENTIAL_LOCKOUT_MAX_MS 3600000ull

static const uint8_t credential_record_id[] = "fahim-credential-v1";
static const uint8_t credential_pepper_id[] = "fahim-credential-pepper-v1";

typedef struct credential_record {
    uint32_t magic;
    uint32_t version;
    uint32_t pin_length;
    uint32_t failed_attempts;
    uint32_t lockout_level;
    uint32_t reserved;
    uint64_t retry_until_ms;
    uint64_t last_seen_ms;
    uint8_t salt[FAHIM_CREDENTIAL_SALT_BYTES];
    uint8_t verifier[FAHIM_CREDENTIAL_DIGEST_BYTES];
} credential_record_t;

static void secure_clear(void *buffer, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)buffer;
    while (length-- != 0u)
        *p++ = 0u;
}

static void set_response(TEE_Param params[4], uint32_t result,
                         uint32_t enrolled, uint32_t attempts,
                         uint64_t retry_after_ms)
{
    fahim_credential_response_t response;

    TEE_MemFill(&response, 0, sizeof(response));
    response.protocol_version = FAHIM_CREDENTIAL_PROTOCOL_VERSION;
    response.result = result;
    response.enrolled = enrolled;
    response.failed_attempts = attempts;
    response.retry_after_ms = retry_after_ms;

    TEE_MemMove(params[1].memref.buffer, &response, sizeof(response));
    params[1].memref.size = sizeof(response);
    secure_clear(&response, sizeof(response));
}


/*
 * PIN enrollment is not exposed to a normal-world/public client. It must be
 * initiated by a separately signed Secure World provisioning TA which owns
 * the trusted setup UI/physical-presence path. That TA's UUID is deliberately
 * pinned here; a normal Android/Linux process cannot claim TEE_LOGIN_TRUSTED_APP.
 */
static const TEE_UUID credential_provisioner_uuid = {
    0x28e3b2d4, 0x4a20, 0x4f91,
    { 0x9a, 0x11, 0x46, 0x41, 0x48, 0x49, 0x4d, 0x50 }
};

static bool is_trusted_provisioner(void)
{
    TEE_Identity identity;
    TEE_Result result = TEE_GetPropertyAsIdentity(
        TEE_PROPSET_CURRENT_CLIENT, "gpd.client.identity", &identity);

    if (result != TEE_SUCCESS)
        return false;

    return identity.login == TEE_LOGIN_TRUSTED_APP &&
        identity.uuid.timeLow == credential_provisioner_uuid.timeLow &&
        identity.uuid.timeMid == credential_provisioner_uuid.timeMid &&
        identity.uuid.timeHiAndVersion == credential_provisioner_uuid.timeHiAndVersion &&
        TEE_MemCompare(identity.uuid.clockSeqAndNode,
                       credential_provisioner_uuid.clockSeqAndNode,
                       sizeof(credential_provisioner_uuid.clockSeqAndNode)) == 0;
}

static TEE_Result verify_secure_platform(void)
{
    uint32_t rollback_level = 0u;
    uint32_t system_time_level = 0u;
    TEE_Result result;

    /*
     * A REE-filesystem-only OP-TEE build reports rollback level 0.
     * Do not silently fall back to it for PIN lockout state.
     */
    result = TEE_GetPropertyAsU32(
        TEE_PROPSET_TEE_IMPLEMENTATION,
        "gpd.tee.trustedStorage.antiRollback.protectionLevel",
        &rollback_level);
    if (result != TEE_SUCCESS || rollback_level < 1000u)
        return TEE_ERROR_NOT_SUPPORTED;

    /*
     * The TEE system clock must be hardware protected. A REE-controlled
     * clock can be rolled back to bypass a timed lockout.
     */
    result = TEE_GetPropertyAsU32(
        TEE_PROPSET_TEE_IMPLEMENTATION,
        "gpd.tee.systemTime.protectionLevel",
        &system_time_level);
    if (result != TEE_SUCCESS || system_time_level < 1000u)
        return TEE_ERROR_NOT_SUPPORTED;

    return TEE_SUCCESS;
}

static TEE_Result get_secure_time_ms(uint64_t *time_ms)
{
    TEE_Time time;

    if (time_ms == NULL)
        return TEE_ERROR_BAD_PARAMETERS;

    /*
     * Use the TEE system clock rather than TA persistent time. Upstream
     * OP-TEE derives TA persistent time from system time plus an offset;
     * its declared protection level may therefore be only 100. The
     * systemTime protection-level property above is the relevant platform
     * declaration for this secure deadline.
     */
    TEE_GetSystemTime(&time);
    if (time.millis >= 1000u) {
        secure_clear(&time, sizeof(time));
        return TEE_ERROR_SECURITY;
    }

    *time_ms = ((uint64_t)time.seconds * 1000ull) + time.millis;
    secure_clear(&time, sizeof(time));
    return TEE_SUCCESS;
}

static TEE_Result open_pepper_key(TEE_ObjectHandle *key, bool create_if_missing)
{
    TEE_Result result;
    TEE_ObjectHandle transient = TEE_HANDLE_NULL;

    result = TEE_OpenPersistentObject(
        CREDENTIAL_STORAGE_ID,
        credential_pepper_id, sizeof(credential_pepper_id) - 1u,
        TEE_DATA_FLAG_ACCESS_READ, key);
    if (result == TEE_SUCCESS || !create_if_missing ||
        result != TEE_ERROR_ITEM_NOT_FOUND)
        return result;

    result = TEE_AllocateTransientObject(
        TEE_TYPE_HMAC_SHA256, 256u, &transient);
    if (result != TEE_SUCCESS)
        return result;

    result = TEE_GenerateKey(transient, 256u, NULL, 0u);
    if (result == TEE_SUCCESS) {
        result = TEE_CreatePersistentObject(
            CREDENTIAL_STORAGE_ID,
            credential_pepper_id, sizeof(credential_pepper_id) - 1u,
            TEE_DATA_FLAG_ACCESS_READ | TEE_DATA_FLAG_ACCESS_WRITE_META,
            transient, NULL, 0u, key);
    }

    TEE_FreeTransientObject(transient);
    return result;
}

static TEE_Result calculate_pin_verifier(
    const uint8_t *pin, size_t pin_length,
    const uint8_t salt[FAHIM_CREDENTIAL_SALT_BYTES],
    bool create_pepper_if_missing,
    uint8_t digest[FAHIM_CREDENTIAL_DIGEST_BYTES])
{
    TEE_ObjectHandle key = TEE_HANDLE_NULL;
    TEE_OperationHandle operation = TEE_HANDLE_NULL;
    uint8_t message[FAHIM_CREDENTIAL_SALT_BYTES + 1u +
                    FAHIM_CREDENTIAL_PIN_MAX];
    size_t message_length = FAHIM_CREDENTIAL_SALT_BYTES + 1u + pin_length;
    size_t digest_length = FAHIM_CREDENTIAL_DIGEST_BYTES;
    TEE_Result result;

    if (pin == NULL || salt == NULL || digest == NULL ||
        pin_length < FAHIM_CREDENTIAL_PIN_MIN ||
        pin_length > FAHIM_CREDENTIAL_PIN_MAX)
        return TEE_ERROR_BAD_PARAMETERS;

    TEE_MemFill(message, 0, sizeof(message));
    TEE_MemMove(message, salt, FAHIM_CREDENTIAL_SALT_BYTES);
    message[FAHIM_CREDENTIAL_SALT_BYTES] = (uint8_t)pin_length;
    TEE_MemMove(message + FAHIM_CREDENTIAL_SALT_BYTES + 1u,
                pin, pin_length);

    result = open_pepper_key(&key, create_pepper_if_missing);
    if (result != TEE_SUCCESS)
        goto cleanup;

    result = TEE_AllocateOperation(
        &operation, TEE_ALG_HMAC_SHA256, TEE_MODE_MAC, 256u);
    if (result != TEE_SUCCESS)
        goto cleanup;

    result = TEE_SetOperationKey(operation, key);
    if (result != TEE_SUCCESS)
        goto cleanup;

    result = TEE_MACComputeFinal(
        operation, message, message_length, digest, &digest_length);
    if (result == TEE_SUCCESS &&
        digest_length != FAHIM_CREDENTIAL_DIGEST_BYTES)
        result = TEE_ERROR_SECURITY;

cleanup:
    if (operation != TEE_HANDLE_NULL)
        TEE_FreeOperation(operation);
    if (key != TEE_HANDLE_NULL)
        TEE_CloseObject(key);
    secure_clear(message, sizeof(message));
    if (result != TEE_SUCCESS)
        secure_clear(digest, FAHIM_CREDENTIAL_DIGEST_BYTES);
    return result;
}

static TEE_Result load_record(TEE_ObjectHandle *handle,
                              credential_record_t *record)
{
    TEE_ObjectInfo info;
    size_t read_length = 0u;
    TEE_Result result;

    result = TEE_OpenPersistentObject(
        CREDENTIAL_STORAGE_ID,
        credential_record_id, sizeof(credential_record_id) - 1u,
        TEE_DATA_FLAG_ACCESS_READ | TEE_DATA_FLAG_ACCESS_WRITE,
        handle);
    if (result != TEE_SUCCESS)
        return result;

    result = TEE_GetObjectInfo1(*handle, &info);
    if (result != TEE_SUCCESS)
        goto fail;

    if (info.dataSize != sizeof(*record)) {
        result = TEE_ERROR_CORRUPT_OBJECT;
        goto fail;
    }

    result = TEE_ReadObjectData(*handle, record, sizeof(*record), &read_length);
    if (result != TEE_SUCCESS)
        goto fail;
    if (read_length != sizeof(*record) ||
        record->magic != CREDENTIAL_RECORD_MAGIC ||
        record->version != CREDENTIAL_RECORD_VERSION ||
        record->pin_length < FAHIM_CREDENTIAL_PIN_MIN ||
        record->pin_length > FAHIM_CREDENTIAL_PIN_MAX ||
        record->lockout_level > 1000u) {
        result = TEE_ERROR_CORRUPT_OBJECT;
        goto fail;
    }
    return TEE_SUCCESS;

fail:
    TEE_CloseObject(*handle);
    *handle = TEE_HANDLE_NULL;
    return result;
}

static TEE_Result save_record(TEE_ObjectHandle handle,
                              const credential_record_t *record)
{
    TEE_Result result;

    /* A storage failure is fail-closed; a malformed record is rejected on reload. */
    result = TEE_SeekObjectData(handle, 0, TEE_DATA_SEEK_SET);
    if (result != TEE_SUCCESS)
        return result;

    return TEE_WriteObjectData(handle, record, sizeof(*record));
}

static TEE_Result create_record(const credential_record_t *record)
{
    TEE_ObjectHandle handle = TEE_HANDLE_NULL;
    TEE_Result result = TEE_CreatePersistentObject(
        CREDENTIAL_STORAGE_ID,
        credential_record_id, sizeof(credential_record_id) - 1u,
        TEE_DATA_FLAG_ACCESS_READ | TEE_DATA_FLAG_ACCESS_WRITE |
            TEE_DATA_FLAG_ACCESS_WRITE_META,
        TEE_HANDLE_NULL, record, sizeof(*record), &handle);

    if (handle != TEE_HANDLE_NULL)
        TEE_CloseObject(handle);
    return result;
}

static uint64_t next_lockout_duration_ms(uint32_t level)
{
    uint64_t duration = CREDENTIAL_LOCKOUT_BASE_MS;
    uint32_t shift = level > 7u ? 7u : level;

    duration <<= shift;
    if (duration > CREDENTIAL_LOCKOUT_MAX_MS)
        duration = CREDENTIAL_LOCKOUT_MAX_MS;
    return duration;
}

static void fill_record_response(TEE_Param params[4],
                                 uint32_t result,
                                 const credential_record_t *record,
                                 uint64_t now_ms)
{
    uint64_t retry_after = 0u;

    if (record != NULL && record->retry_until_ms > now_ms)
        retry_after = record->retry_until_ms - now_ms;

    set_response(params, result, record != NULL ? 1u : 0u,
                 record != NULL ? record->failed_attempts : 0u,
                 retry_after);
}

static TEE_Result query_credential(TEE_Param params[4])
{
    credential_record_t record;
    TEE_ObjectHandle handle = TEE_HANDLE_NULL;
    uint64_t now_ms = 0u;
    TEE_Result result;

    TEE_MemFill(&record, 0, sizeof(record));
    result = get_secure_time_ms(&now_ms);
    if (result != TEE_SUCCESS)
        goto unavailable;

    result = load_record(&handle, &record);
    if (result == TEE_ERROR_ITEM_NOT_FOUND) {
        set_response(params, FAHIM_CRED_RESULT_NOT_ENROLLED, 0u, 0u, 0u);
        return TEE_SUCCESS;
    }
    if (result != TEE_SUCCESS)
        goto unavailable;

    if (now_ms < record.last_seen_ms) {
        result = TEE_ERROR_SECURITY;
        goto unavailable;
    }

    fill_record_response(
        params,
        record.retry_until_ms > now_ms
            ? FAHIM_CRED_RESULT_LOCKED_OUT : FAHIM_CRED_RESULT_OK,
        &record, now_ms);
    TEE_CloseObject(handle);
    secure_clear(&record, sizeof(record));
    return TEE_SUCCESS;

unavailable:
    if (handle != TEE_HANDLE_NULL)
        TEE_CloseObject(handle);
    secure_clear(&record, sizeof(record));
    set_response(params, FAHIM_CRED_RESULT_UNAVAILABLE, 0u, 0u, 0u);
    return TEE_SUCCESS;
}

static TEE_Result enroll_credential(TEE_Param params[4])
{
    uint8_t pin[FAHIM_CREDENTIAL_PIN_MAX];
    credential_record_t record;
    TEE_ObjectHandle existing = TEE_HANDLE_NULL;
    uint64_t now_ms = 0u;
    size_t length = params[0].memref.size;
    TEE_Result result;
    size_t i;

    TEE_MemFill(pin, 0, sizeof(pin));
    TEE_MemFill(&record, 0, sizeof(record));

    if (length < FAHIM_CREDENTIAL_PIN_MIN ||
        length > FAHIM_CREDENTIAL_PIN_MAX) {
        set_response(params, FAHIM_CRED_RESULT_INVALID, 0u, 0u, 0u);
        return TEE_SUCCESS;
    }

    TEE_MemMove(pin, params[0].memref.buffer, length);
    for (i = 0u; i < length; ++i) {
        if (pin[i] < (uint8_t)'0' || pin[i] > (uint8_t)'9') {
            goto invalid;
        }
    }

    result = get_secure_time_ms(&now_ms);
    if (result != TEE_SUCCESS)
        goto unavailable;

    result = TEE_OpenPersistentObject(
        CREDENTIAL_STORAGE_ID,
        credential_record_id, sizeof(credential_record_id) - 1u,
        TEE_DATA_FLAG_ACCESS_READ, &existing);
    if (result == TEE_SUCCESS) {
        TEE_CloseObject(existing);
        set_response(params, FAHIM_CRED_RESULT_ALREADY_ENROLLED, 1u, 0u, 0u);
        secure_clear(pin, sizeof(pin));
        secure_clear(&record, sizeof(record));
        return TEE_SUCCESS;
    }
    if (result != TEE_ERROR_ITEM_NOT_FOUND)
        goto unavailable;

    record.magic = CREDENTIAL_RECORD_MAGIC;
    record.version = CREDENTIAL_RECORD_VERSION;
    record.pin_length = (uint32_t)length;
    record.last_seen_ms = now_ms;
    TEE_GenerateRandom(record.salt, sizeof(record.salt));

    result = calculate_pin_verifier(
        pin, length, record.salt, true, record.verifier);
    if (result != TEE_SUCCESS)
        goto unavailable;

    result = create_record(&record);
    if (result == TEE_ERROR_ACCESS_CONFLICT) {
        set_response(params, FAHIM_CRED_RESULT_ALREADY_ENROLLED, 1u, 0u, 0u);
    } else if (result != TEE_SUCCESS) {
        goto unavailable;
    } else {
        set_response(params, FAHIM_CRED_RESULT_OK, 1u, 0u, 0u);
    }

    secure_clear(pin, sizeof(pin));
    secure_clear(&record, sizeof(record));
    return TEE_SUCCESS;

invalid:
    secure_clear(pin, sizeof(pin));
    secure_clear(&record, sizeof(record));
    set_response(params, FAHIM_CRED_RESULT_INVALID, 0u, 0u, 0u);
    return TEE_SUCCESS;

unavailable:
    secure_clear(pin, sizeof(pin));
    secure_clear(&record, sizeof(record));
    set_response(params, FAHIM_CRED_RESULT_UNAVAILABLE, 0u, 0u, 0u);
    return TEE_SUCCESS;
}

static TEE_Result verify_credential(TEE_Param params[4])
{
    uint8_t pin[FAHIM_CREDENTIAL_PIN_MAX];
    uint8_t candidate[FAHIM_CREDENTIAL_DIGEST_BYTES];
    credential_record_t record;
    TEE_ObjectHandle handle = TEE_HANDLE_NULL;
    size_t length = params[0].memref.size;
    uint64_t now_ms = 0u;
    uint64_t retry_after = 0u;
    size_t i;
    int matches = 0;
    TEE_Result result;

    TEE_MemFill(pin, 0, sizeof(pin));
    TEE_MemFill(candidate, 0, sizeof(candidate));
    TEE_MemFill(&record, 0, sizeof(record));

    if (length < FAHIM_CREDENTIAL_PIN_MIN ||
        length > FAHIM_CREDENTIAL_PIN_MAX) {
        set_response(params, FAHIM_CRED_RESULT_INVALID, 0u, 0u, 0u);
        return TEE_SUCCESS;
    }

    TEE_MemMove(pin, params[0].memref.buffer, length);
    for (i = 0u; i < length; ++i) {
        if (pin[i] < (uint8_t)'0' || pin[i] > (uint8_t)'9') {
            goto invalid;
        }
    }

    result = get_secure_time_ms(&now_ms);
    if (result != TEE_SUCCESS)
        goto unavailable;

    result = load_record(&handle, &record);
    if (result == TEE_ERROR_ITEM_NOT_FOUND) {
        set_response(params, FAHIM_CRED_RESULT_NOT_ENROLLED, 0u, 0u, 0u);
        goto done;
    }
    if (result != TEE_SUCCESS)
        goto unavailable;

    if (now_ms < record.last_seen_ms)
        goto unavailable;

    if (record.retry_until_ms > now_ms) {
        fill_record_response(params, FAHIM_CRED_RESULT_LOCKED_OUT,
                             &record, now_ms);
        goto done;
    }

    /* Start a fresh five-attempt window after an expired secure lockout. */
    if (record.retry_until_ms != 0u) {
        record.retry_until_ms = 0u;
        record.failed_attempts = 0u;
    }

    result = calculate_pin_verifier(pin, length, record.salt, false, candidate);
    if (result != TEE_SUCCESS)
        goto unavailable;

    matches = length == record.pin_length &&
        TEE_MemCompare(candidate, record.verifier, sizeof(candidate)) == 0;

    record.last_seen_ms = now_ms;
    if (matches) {
        record.failed_attempts = 0u;
        record.lockout_level = 0u;
        record.retry_until_ms = 0u;
        result = save_record(handle, &record);
        if (result != TEE_SUCCESS)
            goto unavailable;
        set_response(params, FAHIM_CRED_RESULT_OK, 1u, 0u, 0u);
    } else {
        if (record.failed_attempts < UINT32_MAX)
            ++record.failed_attempts;

        if (record.failed_attempts >= CREDENTIAL_FAILURE_LIMIT) {
            retry_after = next_lockout_duration_ms(record.lockout_level);
            if (now_ms > UINT64_MAX - retry_after)
                goto unavailable;
            record.retry_until_ms = now_ms + retry_after;
            if (record.lockout_level < 1000u)
                ++record.lockout_level;
        }

        result = save_record(handle, &record);
        if (result != TEE_SUCCESS)
            goto unavailable;

        fill_record_response(
            params,
            record.retry_until_ms > now_ms
                ? FAHIM_CRED_RESULT_LOCKED_OUT : FAHIM_CRED_RESULT_REJECTED,
            &record, now_ms);
    }
    goto done;

invalid:
    result = TEE_SUCCESS;
    set_response(params, FAHIM_CRED_RESULT_INVALID, 0u, 0u, 0u);
    goto cleanup;

unavailable:
    set_response(params, FAHIM_CRED_RESULT_UNAVAILABLE, 0u, 0u, 0u);
    goto cleanup;

done:
    result = TEE_SUCCESS;
cleanup:
    if (handle != TEE_HANDLE_NULL)
        TEE_CloseObject(handle);
    secure_clear(pin, sizeof(pin));
    secure_clear(candidate, sizeof(candidate));
    secure_clear(&record, sizeof(record));
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
    (void)session_context;
    if (param_types != TEE_PARAM_TYPES(
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
        return TEE_ERROR_BAD_PARAMETERS;
    (void)params;
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
    TEE_Result result;

    (void)session_context;

    if (command_id == FAHIM_CRED_CMD_QUERY) {
        if (param_types != TEE_PARAM_TYPES(
                TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,
                TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE) ||
            params[0].memref.size < sizeof(fahim_credential_response_t))
            return TEE_ERROR_BAD_PARAMETERS;

        /* Query uses output slot 0 in the secure-world command ABI. */
        {
            TEE_Param adapted[4];
            TEE_MemFill(adapted, 0, sizeof(adapted));
            adapted[1] = params[0];
            result = verify_secure_platform();
            if (result != TEE_SUCCESS) {
                set_response(adapted, FAHIM_CRED_RESULT_UNAVAILABLE, 0u, 0u, 0u);
            } else {
                query_credential(adapted);
            }
            params[0] = adapted[1];
            return TEE_SUCCESS;
        }
    }

    if (command_id == FAHIM_CRED_CMD_ENROLL ||
        command_id == FAHIM_CRED_CMD_VERIFY) {
        if (param_types != TEE_PARAM_TYPES(
                TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT,
                TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE) ||
            params[0].memref.size < FAHIM_CREDENTIAL_PIN_MIN ||
            params[0].memref.size > FAHIM_CREDENTIAL_PIN_MAX ||
            params[1].memref.size < sizeof(fahim_credential_response_t))
            return TEE_ERROR_BAD_PARAMETERS;

        if (command_id == FAHIM_CRED_CMD_ENROLL &&
            !is_trusted_provisioner()) {
            set_response(params, FAHIM_CRED_RESULT_UNAVAILABLE, 0u, 0u, 0u);
            return TEE_SUCCESS;
        }

        result = verify_secure_platform();
        if (result != TEE_SUCCESS) {
            TEE_Param adapted[4];
            TEE_MemFill(adapted, 0, sizeof(adapted));
            adapted[1] = params[1];
            set_response(adapted, FAHIM_CRED_RESULT_UNAVAILABLE, 0u, 0u, 0u);
            params[1] = adapted[1];
            return TEE_SUCCESS;
        }

        return command_id == FAHIM_CRED_CMD_ENROLL
            ? enroll_credential(params) : verify_credential(params);
    }

    return TEE_ERROR_NOT_SUPPORTED;
}
