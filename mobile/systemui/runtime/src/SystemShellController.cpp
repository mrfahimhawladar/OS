#include "fahim/systemui/SystemShell.hpp"

namespace fahim::systemui {

SystemShellController::SystemShellController(
    LockScreenCore& lock_core, LockScreenController& lock_screen,
    KernelSessionControl& kernel_control, SystemPlatformServices* platform,
    fahim::security::AuthenticatorVault* vault,
    SystemShellRenderer& renderer, fahim::ui::Compositor& compositor) noexcept
    : lock_core_(lock_core), lock_screen_(lock_screen),
      kernel_control_(kernel_control), platform_(platform), vault_(vault),
      renderer_(renderer), compositor_(compositor) {
    refresh();
}

void SystemShellController::set_notice(const char* notice) noexcept {
    state_.notice = notice;
}

void SystemShellController::refresh() noexcept {
    SystemStatus status{};
    state_.services_available = platform_ != nullptr && platform_->read_status(status);
    if (state_.services_available) state_.status = status;
    else {
        state_.status = {};
        state_.status.time[0] = '-'; state_.status.time[1] = '-';
        state_.status.time[2] = ':'; state_.status.time[3] = '-';
        state_.status.time[4] = '-'; state_.status.time[5] = '\0';
        const char unavailable[] = "TIME UNAVAILABLE";
        for (std::size_t i = 0; i < sizeof(unavailable); ++i)
            state_.status.date[i] = unavailable[i];
    }
    state_.vault_available = vault_ != nullptr && vault_->available();
    state_.enrollment_available = state_.vault_available;
    if (state_.screen == SystemScreen::Authenticator) refresh_authenticator();
}

void SystemShellController::navigate(SystemScreen screen) noexcept {
    state_.screen = screen;
    state_.notice = nullptr;
    if (screen == SystemScreen::Authenticator) refresh_authenticator();
    (void)render();
}

void SystemShellController::refresh_authenticator() noexcept {
    state_.code_count = 0u;
    state_.vault_available = vault_ != nullptr && vault_->available();
    state_.enrollment_available = state_.vault_available;
    if (!state_.vault_available) return;
    if (!state_.status.time_valid) {
        set_notice("SECURE TIME SERVICE UNAVAILABLE");
        return;
    }
    const std::size_t count = vault_->account_count() < state_.codes.size()
        ? vault_->account_count() : state_.codes.size();
    for (std::size_t i = 0; i < count; ++i) {
        fahim::security::OneTimeCode code{};
        if (vault_->generate_code(i, state_.status.unix_time_seconds, code)) {
            state_.codes[state_.code_count++] = code;
        }
        // The secure vault returns metadata and a short-lived code only;
        // no shared secret is ever copied into this Normal World process.
    }
}

bool SystemShellController::handle_event(const fahim::ui::PointerEvent& event) noexcept {
    if (lock_core_.state() != LockState::Unlocked) {
        (void)lock_screen_.handle_event(event);
        if (lock_core_.state() == LockState::Unlocked) {
            state_.screen = SystemScreen::Home;
            (void)kernel_control_.synchronize(lock_core_);
            refresh();
            return render();
        }
        return true;
    }
    if (event.phase == fahim::ui::PointerPhase::Down) {
        pointer_down_ = true; gesture_start_y_ = event.y; return true;
    }
    if (event.phase == fahim::ui::PointerPhase::Cancel) {
        pointer_down_ = false; return true;
    }
    if (event.phase != fahim::ui::PointerPhase::Up) return true;

    if (pointer_down_) {
        pointer_down_ = false;
        const std::int32_t delta = event.y - gesture_start_y_;
        if (gesture_start_y_ < static_cast<std::int32_t>(renderer_.height() / 8u) &&
            delta > 60) { navigate(SystemScreen::ControlCenter); return true; }
        if (state_.screen == SystemScreen::ControlCenter && delta < -60) {
            navigate(SystemScreen::Home); return true;
        }
    }
    activate(renderer_.hit_test(event.x, event.y, state_.screen));
    return true;
}

bool SystemShellController::pump_input(fahim::input::TouchInputPipeline& input,
                                       std::size_t max_events) noexcept {
    fahim::ui::PointerEvent event{};
    std::size_t processed = 0u;
    while (processed < max_events && input.next_event(event)) {
        ++processed;
        if (!handle_event(event)) return false;
    }
    return lock_core_.state() == LockState::Unlocked;
}

void SystemShellController::activate(SystemShellRenderer::Hit hit) noexcept {
    switch (hit) {
        case SystemShellRenderer::Hit::OpenSettings: navigate(SystemScreen::Settings); return;
        case SystemShellRenderer::Hit::OpenAuthenticator: navigate(SystemScreen::Authenticator); return;
        case SystemShellRenderer::Hit::OpenControlCenter: navigate(SystemScreen::ControlCenter); return;
        case SystemShellRenderer::Hit::OpenAbout: navigate(SystemScreen::About); return;
        case SystemShellRenderer::Hit::BackHome: navigate(SystemScreen::Home); return;
        case SystemShellRenderer::Hit::ToggleWifi:
        case SystemShellRenderer::Hit::ToggleBluetooth:
        case SystemShellRenderer::Hit::ToggleAirplane:
        case SystemShellRenderer::Hit::ToggleFlashlight: {
            if (!state_.services_available || platform_ == nullptr) {
                set_notice("HARDWARE CONTROL SERVICE OFFLINE"); (void)render(); return;
            }
            SystemRadio radio = SystemRadio::Wifi;
            bool current = state_.status.wifi_enabled;
            if (hit == SystemShellRenderer::Hit::ToggleBluetooth) {
                radio = SystemRadio::Bluetooth; current = state_.status.bluetooth_enabled;
            } else if (hit == SystemShellRenderer::Hit::ToggleAirplane) {
                radio = SystemRadio::Airplane; current = state_.status.airplane_enabled;
            } else if (hit == SystemShellRenderer::Hit::ToggleFlashlight) {
                radio = SystemRadio::Flashlight; current = state_.status.flashlight_enabled;
            }
            if (!platform_->set_radio(radio, !current))
                set_notice("HARDWARE DRIVER REJECTED REQUEST");
            else { state_.notice = nullptr; refresh(); }
            (void)render(); return;
        }
        case SystemShellRenderer::Hit::BrightnessDown:
        case SystemShellRenderer::Hit::BrightnessUp: {
            if (!state_.services_available || platform_ == nullptr) {
                set_notice("DISPLAY SERVICE OFFLINE"); (void)render(); return;
            }
            const int current = state_.status.brightness_percent;
            const int next = current + (hit == SystemShellRenderer::Hit::BrightnessUp ? 10 : -10);
            if (!platform_->set_brightness(static_cast<std::uint8_t>(next < 0 ? 0 : next > 100 ? 100 : next)))
                set_notice("DISPLAY DRIVER REJECTED REQUEST");
            else { state_.notice = nullptr; refresh(); }
            (void)render(); return;
        }
        case SystemShellRenderer::Hit::Theme: {
            if (!state_.services_available || platform_ == nullptr) {
                set_notice("SETTINGS SERVICE OFFLINE"); (void)render(); return;
            }
            SystemTheme next = SystemTheme::Dark;
            if (state_.status.theme == SystemTheme::Dark) next = SystemTheme::Light;
            else if (state_.status.theme == SystemTheme::Light) next = SystemTheme::Automatic;
            if (!platform_->set_theme(next)) set_notice("THEME SERVICE UNAVAILABLE");
            else { state_.notice = nullptr; refresh(); }
            (void)render(); return;
        }
        case SystemShellRenderer::Hit::LockDevice: lock_device(); return;
        case SystemShellRenderer::Hit::AddAuthenticator:
            if (vault_ == nullptr || !state_.vault_available ||
                !vault_->begin_secure_enrollment())
                set_notice("TRUSTED AUTHENTICATOR ENROLLMENT OFFLINE");
            else set_notice("FOLLOW SECURE ENROLLMENT PROMPT");
            (void)render(); return;
        case SystemShellRenderer::Hit::None:
        default: return;
    }
}

void SystemShellController::tick() noexcept {
    if (lock_core_.state() != LockState::Unlocked) return;
    refresh();
    (void)render();
}

void SystemShellController::lock_device() noexcept {
    compositor_.remove_surface(static_cast<std::uint32_t>(surface_id_));
    lock_screen_.lock();
    state_.screen = SystemScreen::Home;
    (void)kernel_control_.synchronize(lock_core_);
}

bool SystemShellController::render() noexcept {
    if (lock_core_.state() != LockState::Unlocked) {
        compositor_.remove_surface(static_cast<std::uint32_t>(surface_id_));
        return false;
    }
    refresh();
    renderer_.render(state_);
    return renderer_.register_surface(compositor_,
        static_cast<std::uint32_t>(surface_id_), 20) && compositor_.compose();
}

}  // namespace fahim::systemui
