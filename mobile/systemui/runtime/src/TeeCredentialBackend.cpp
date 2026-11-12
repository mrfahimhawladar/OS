#include "fahim/security/TeeCredentialBackend.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <new>

#if defined(FAHIM_ENABLE_OPTEE_CLIENT)
#include <cstring>
#include <mutex>
#include <new>
#include <tee_client_api.h>
#endif

namespace fahim::security {

#if defined(FAHIM_ENABLE_OPTEE_CLIENT)
namespace {
constexpr TEEC_UUID kCredentialTaUuid = FAHIM_CREDENTIAL_TA_UUID;

void secure_zero(void* memory, std::size_t length) noexcept {
    auto* bytes = static_cast<volatile std::uint8_t*>(memory);
    while (length-- != 0u) *bytes++ = 0u;
}

TeeCredentialResult decode_response(const fahim_credential_response_t& response,
                                    TeeCredentialState& state) noexcept {
    if (response.protocol_version != FAHIM_CREDENTIAL_PROTOCOL_VERSION ||
        response.result > FAHIM_CRED_RESULT_UNAVAILABLE ||
        response.enrolled > 1u) {
        return TeeCredentialResult::Unavailable;
    }
    state.enrolled = response.enrolled != 0u;
    state.failed_attempts = response.failed_attempts;
    state.retry_after_ms = response.retry_after_ms;
    return static_cast<TeeCredentialResult>(response.result);
}
}  // namespace

struct OpteeCredentialTransport::Impl {
    mutable std::mutex mutex;
    mutable TEEC_Context context{};
    mutable TEEC_Session session{};
    mutable bool context_initialized{false};
    mutable bool session_open{false};

    ~Impl() {
        if (session_open) TEEC_CloseSession(&session);
        if (context_initialized) TEEC_FinalizeContext(&context);
    }

    bool ensure_open() const noexcept {
        if (session_open) return true;
        if (!context_initialized) {
            if (TEEC_InitializeContext(nullptr, &context) != TEEC_SUCCESS) return false;
            context_initialized = true;
        }
        std::uint32_t origin = 0;
        const TEEC_Result result = TEEC_OpenSession(
            &context, &session, &kCredentialTaUuid, TEEC_LOGIN_PUBLIC,
            nullptr, nullptr, &origin);
        if (result != TEEC_SUCCESS) return false;
        session_open = true;
        return true;
    }

    TeeCredentialResult invoke_status(std::uint32_t command,
                                      const std::uint8_t* pin,
                                      std::size_t length,
                                      TeeCredentialState& state) const noexcept {
        std::lock_guard<std::mutex> guard(mutex);
        if (!ensure_open()) return TeeCredentialResult::Unavailable;

        fahim_credential_response_t wire{};
        TEEC_Operation operation{};
        std::uint32_t origin = 0;
        TEEC_SharedMemory input_memory{};
        bool input_allocated = false;

        if (command == FAHIM_CRED_CMD_QUERY) {
            operation.paramTypes = TEEC_PARAM_TYPES(
                TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
            operation.params[0].tmpref.buffer = &wire;
            operation.params[0].tmpref.size = sizeof(wire);
        } else {
            if (pin == nullptr || length < FAHIM_CREDENTIAL_PIN_MIN ||
                length > FAHIM_CREDENTIAL_PIN_MAX) {
                return TeeCredentialResult::Invalid;
            }

            // A dedicated TEEC shared-memory allocation is scrubbed before release.
            input_memory.size = length;
            input_memory.flags = TEEC_MEM_INPUT;
            if (TEEC_AllocateSharedMemory(&context, &input_memory) != TEEC_SUCCESS) {
                return TeeCredentialResult::Unavailable;
            }
            input_allocated = true;
            if (input_memory.buffer == nullptr) {
                TEEC_ReleaseSharedMemory(&input_memory);
                return TeeCredentialResult::Unavailable;
            }
            std::memcpy(input_memory.buffer, pin, length);
            operation.paramTypes = TEEC_PARAM_TYPES(
                TEEC_MEMREF_PARTIAL_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
                TEEC_NONE, TEEC_NONE);
            operation.params[0].memref.parent = &input_memory;
            operation.params[0].memref.offset = 0;
            operation.params[0].memref.size = length;
            operation.params[1].tmpref.buffer = &wire;
            operation.params[1].tmpref.size = sizeof(wire);
        }

        const TEEC_Result result = TEEC_InvokeCommand(
            &session, command, &operation, &origin);

        if (input_allocated) {
            secure_zero(input_memory.buffer, input_memory.size);
            TEEC_ReleaseSharedMemory(&input_memory);
        }

        if (result != TEEC_SUCCESS) return TeeCredentialResult::Unavailable;
        return decode_response(wire, state);
    }
};
#else
struct OpteeCredentialTransport::Impl {};
#endif

OpteeCredentialTransport::OpteeCredentialTransport() noexcept
    : impl_(new (std::nothrow) Impl{}) {}

OpteeCredentialTransport::~OpteeCredentialTransport() {
    delete impl_;
}

bool OpteeCredentialTransport::ready() const noexcept {
#if defined(FAHIM_ENABLE_OPTEE_CLIENT)
    if (impl_ == nullptr) return false;
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->ensure_open();
#else
    return false;
#endif
}

TeeCredentialResult OpteeCredentialTransport::query(
    TeeCredentialState& state) const noexcept {
#if defined(FAHIM_ENABLE_OPTEE_CLIENT)
    if (impl_ == nullptr) return TeeCredentialResult::Unavailable;
    return impl_->invoke_status(FAHIM_CRED_CMD_QUERY, nullptr, 0u, state);
#else
    (void)state;
    return TeeCredentialResult::Unavailable;
#endif
}

TeeCredentialResult OpteeCredentialTransport::enroll(
    const std::uint8_t* pin, std::size_t length,
    TeeCredentialState& state) const noexcept {
#if defined(FAHIM_ENABLE_OPTEE_CLIENT)
    if (impl_ == nullptr) return TeeCredentialResult::Unavailable;
    return impl_->invoke_status(FAHIM_CRED_CMD_ENROLL, pin, length, state);
#else
    (void)pin; (void)length; (void)state;
    return TeeCredentialResult::Unavailable;
#endif
}

TeeCredentialResult OpteeCredentialTransport::verify(
    const std::uint8_t* pin, std::size_t length,
    TeeCredentialState& state) const noexcept {
#if defined(FAHIM_ENABLE_OPTEE_CLIENT)
    if (impl_ == nullptr) return TeeCredentialResult::Unavailable;
    return impl_->invoke_status(FAHIM_CRED_CMD_VERIFY, pin, length, state);
#else
    (void)pin; (void)length; (void)state;
    return TeeCredentialResult::Unavailable;
#endif
}

TeeProtectedCredentialBackend::TeeProtectedCredentialBackend(
    TeeCredentialTransport* transport) noexcept : transport_(transport) {}

TeeCredentialResult TeeProtectedCredentialBackend::query_state(
    TeeCredentialState& state) const noexcept {
    if (transport_ == nullptr || !transport_->ready()) {
        return TeeCredentialResult::Unavailable;
    }
    return transport_->query(state);
}

bool TeeProtectedCredentialBackend::available() const noexcept {
    TeeCredentialState state{};
    const TeeCredentialResult result = query_state(state);
    return result == TeeCredentialResult::Ok ||
           result == TeeCredentialResult::NotEnrolled ||
           result == TeeCredentialResult::LockedOut;
}

TeeCredentialResult TeeProtectedCredentialBackend::enroll_pin(
    const std::uint8_t* pin, std::size_t length) noexcept {
    if (transport_ == nullptr || !transport_->ready() || pin == nullptr ||
        length < FAHIM_CREDENTIAL_PIN_MIN || length > FAHIM_CREDENTIAL_PIN_MAX) {
        return TeeCredentialResult::Unavailable;
    }
    for (std::size_t i = 0; i < length; ++i) {
        if (pin[i] < static_cast<std::uint8_t>('0') ||
            pin[i] > static_cast<std::uint8_t>('9')) {
            return TeeCredentialResult::Invalid;
        }
    }
    TeeCredentialState state{};
    return transport_->enroll(pin, length, state);
}

fahim::systemui::VerificationResult
TeeProtectedCredentialBackend::verify_pin_and_update_policy(
    const std::uint8_t* pin, std::size_t length) noexcept {
    last_retry_after_ms_.store(0u, std::memory_order_relaxed);
    if (transport_ == nullptr || !transport_->ready() || pin == nullptr ||
        length < FAHIM_CREDENTIAL_PIN_MIN || length > FAHIM_CREDENTIAL_PIN_MAX) {
        return fahim::systemui::VerificationResult::TemporarilyUnavailable;
    }
    TeeCredentialState state{};
    const TeeCredentialResult result = transport_->verify(pin, length, state);
    last_retry_after_ms_.store(state.retry_after_ms, std::memory_order_relaxed);
    switch (result) {
        case TeeCredentialResult::Ok:
            return fahim::systemui::VerificationResult::Accepted;
        case TeeCredentialResult::Rejected:
            return fahim::systemui::VerificationResult::Rejected;
        case TeeCredentialResult::LockedOut:
            return fahim::systemui::VerificationResult::LockedOut;
        case TeeCredentialResult::NotEnrolled:
        case TeeCredentialResult::AlreadyEnrolled:
        case TeeCredentialResult::Invalid:
        case TeeCredentialResult::Unavailable:
        default:
            return fahim::systemui::VerificationResult::TemporarilyUnavailable;
    }
}

std::chrono::milliseconds TeeProtectedCredentialBackend::lockout_remaining() const noexcept {
    const auto maximum = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    const auto bounded = std::min(last_retry_after_ms_.load(std::memory_order_relaxed), maximum);
    return std::chrono::milliseconds(static_cast<std::int64_t>(bounded));
}

TeeCredentialService::TeeCredentialService() noexcept
    : transport_(), backend_(&transport_), verifier_(&backend_) {}

}  // namespace fahim::security
