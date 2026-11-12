#include "fahim/systemui/LockScreenView.hpp"

namespace fahim::systemui {

LockScreenController::LockScreenController(
    LockScreenCore& core, KernelSessionControl& kernel_control,
    LockScreenRenderer& renderer) noexcept
    : core_(core), kernel_control_(kernel_control), renderer_(renderer),
      input_router_(72u) {
    input_router_.set_camera_bounds(renderer_.camera_shortcut_bounds());
    input_router_.set_flashlight_bounds(renderer_.flashlight_shortcut_bounds());
    apply_lock_state_to_kernel();
}

void LockScreenController::set_message(const char* message) noexcept {
    message_ = message;
}

void LockScreenController::apply_lock_state_to_kernel() noexcept {
    const std::int64_t result = kernel_control_.synchronize(core_);
    if (result != 0) set_message("SYSTEM LOCK SERVICE UNAVAILABLE");
}

void LockScreenController::submit_pin() noexcept {
    if (core_.state() == LockState::Lockout) {
        set_message("TOO MANY ATTEMPTS");
        return;
    }
    switch (core_.submit_pin()) {
        case SubmitResult::Accepted:
            bouncer_open_ = false;
            message_ = nullptr;
            apply_lock_state_to_kernel();
            break;
        case SubmitResult::Rejected:
            set_message("INCORRECT PIN");
            break;
        case SubmitResult::LockedOut:
            was_lockout_ = true;
            set_message("TRY AGAIN LATER");
            break;
        case SubmitResult::InvalidLength:
            set_message("ENTER AT LEAST 4 DIGITS");
            break;
        case SubmitResult::BackendUnavailable:
            set_message("CREDENTIAL SERVICE UNAVAILABLE");
            break;
        case SubmitResult::EmptyInput:
            set_message("ENTER YOUR PIN");
            break;
        case SubmitResult::UnavailableState:
            set_message("LOCK SCREEN NOT READY");
            break;
    }
}

bool LockScreenController::handle_event(
    const fahim::ui::PointerEvent& event) noexcept {
    if (core_.state() == LockState::Unlocked) return false;

    if (!bouncer_open_) {
        const fahim::ui::InputDecision decision = input_router_.route(event);
        if (decision.action == fahim::ui::LockScreenAction::BeginUnlock &&
            event.phase == fahim::ui::PointerPhase::Up) {
            bouncer_open_ = true;
            camera_preview_open_ = false;
            message_ = nullptr;
        } else if (decision.action == fahim::ui::LockScreenAction::ToggleFlashlight &&
                   event.phase == fahim::ui::PointerPhase::Up) {
            flashlight_on_ = !flashlight_on_;
        } else if (decision.action == fahim::ui::LockScreenAction::OpenCamera &&
                   event.phase == fahim::ui::PointerPhase::Up) {
            camera_preview_open_ = true;
            set_message("CAMERA SERVICE NOT CONNECTED");
        }
        return true;
    }

    // Bouncer consumes events exclusively. No application receives these touches.
    if (event.phase != fahim::ui::PointerPhase::Up) return true;
    switch (renderer_.hit_test_pin(event.x, event.y)) {
        case PinHit::Digit0: core_.append_digit('0'); break;
        case PinHit::Digit1: core_.append_digit('1'); break;
        case PinHit::Digit2: core_.append_digit('2'); break;
        case PinHit::Digit3: core_.append_digit('3'); break;
        case PinHit::Digit4: core_.append_digit('4'); break;
        case PinHit::Digit5: core_.append_digit('5'); break;
        case PinHit::Digit6: core_.append_digit('6'); break;
        case PinHit::Digit7: core_.append_digit('7'); break;
        case PinHit::Digit8: core_.append_digit('8'); break;
        case PinHit::Digit9: core_.append_digit('9'); break;
        case PinHit::Delete:
            core_.erase_last_digit();
            message_ = nullptr;
            break;
        case PinHit::Cancel:
            core_.clear_input();
            bouncer_open_ = false;
            message_ = nullptr;
            break;
        case PinHit::Submit:
            submit_pin();
            break;
        case PinHit::None:
        default:
            break;
    }
    return core_.state() != LockState::Unlocked;
}

bool LockScreenController::pump_input(
    fahim::input::TouchInputPipeline& input, std::size_t max_events) noexcept {
    if (max_events == 0u) return core_.state() != LockState::Unlocked;
    fahim::ui::PointerEvent event{};
    std::size_t processed = 0u;
    while (processed < max_events && input.next_event(event)) {
        ++processed;
        if (!handle_event(event)) return false;
    }
    return core_.state() != LockState::Unlocked;
}

void LockScreenController::tick() noexcept {
    core_.tick();
    if (was_lockout_ && core_.state() != LockState::Lockout) {
        was_lockout_ = false;
        message_ = nullptr;
    }
}

void LockScreenController::lock() noexcept {
    core_.lock();
    bouncer_open_ = false;
    camera_preview_open_ = false;
    message_ = nullptr;
    apply_lock_state_to_kernel();
}

void LockScreenController::render(const LockScreenFrameInfo& frame) noexcept {
    LockScreenViewState state{};
    state.pin_digits = core_.input_length();
    state.bouncer_open = bouncer_open_;
    state.flashlight_on = flashlight_on_;
    state.camera_preview = camera_preview_open_;
    state.locked_out = core_.state() == LockState::Lockout;
    const auto remaining = core_.lockout_remaining();
    state.lockout_seconds = static_cast<std::uint32_t>(
        (remaining.count() + 999) / 1000);
    state.message = message_;
    renderer_.render(frame, state);
}

bool LockScreenController::render_to_compositor(
    const LockScreenFrameInfo& frame, fahim::ui::Compositor& compositor,
    std::uint32_t surface_id, std::int32_t z_order) noexcept {
    if (core_.state() == LockState::Unlocked) {
        compositor.remove_surface(surface_id);
        return false;
    }
    render(frame);
    return renderer_.register_surface(compositor, surface_id, z_order);
}

}  // namespace fahim::systemui
