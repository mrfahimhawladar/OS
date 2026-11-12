#include "fahim/systemui/SystemShell.hpp"
#include "fahim/security/Authenticator.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using fahim::systemui::LockState;
using fahim::systemui::SystemScreen;
using fahim::systemui::SystemShellController;
using fahim::systemui::SystemShellRenderer;
using fahim::systemui::SystemStatus;
using fahim::systemui::SystemPlatformServices;
using fahim::systemui::SystemRadio;
using fahim::systemui::SystemTheme;
using fahim::security::AuthenticatorAccount;
using fahim::security::AuthenticatorVault;
using fahim::security::OneTimeCode;

namespace {
class TestCredential final : public fahim::systemui::CredentialVerifier {
public:
    fahim::systemui::VerificationResult verify_pin(
        const std::uint8_t* pin, std::size_t length) noexcept override {
        return pin != nullptr && length == 4u && std::memcmp(pin, "2468", 4u) == 0
            ? fahim::systemui::VerificationResult::Accepted
            : fahim::systemui::VerificationResult::Rejected;
    }
};

class TestPlatform final : public SystemPlatformServices {
public:
    SystemStatus status{};
    bool connected{true};
    unsigned radio_calls{0};
    SystemRadio last_radio{SystemRadio::Wifi};

    TestPlatform() {
        status.time_valid = true;
        std::memcpy(status.time, "12:34", 6);
        std::memcpy(status.date, "THU NOV 12", 11);
        status.unix_time_seconds = 59u;
        status.battery_valid = true;
        status.battery_percent = 90u;
        status.brightness_percent = 60u;
    }
    bool read_status(SystemStatus& out) noexcept override {
        if (!connected) return false;
        out = status; return true;
    }
    bool set_radio(SystemRadio radio, bool enabled) noexcept override {
        if (!connected) return false;
        ++radio_calls; last_radio = radio;
        switch (radio) {
            case SystemRadio::Wifi: status.wifi_enabled = enabled; break;
            case SystemRadio::Bluetooth: status.bluetooth_enabled = enabled; break;
            case SystemRadio::Airplane: status.airplane_enabled = enabled; break;
            case SystemRadio::Flashlight: status.flashlight_enabled = enabled; break;
        }
        return true;
    }
    bool set_brightness(std::uint8_t value) noexcept override {
        if (!connected) return false;
        status.brightness_percent = value; return true;
    }
    bool set_theme(SystemTheme value) noexcept override {
        if (!connected) return false;
        status.theme = value; return true;
    }
};

class TestVault final : public AuthenticatorVault {
public:
    bool connected{true};
    bool available() const noexcept override { return connected; }
    std::size_t account_count() const noexcept override { return connected ? 1u : 0u; }
    bool read_account(std::size_t index, AuthenticatorAccount& account) noexcept override {
        if (!connected || index != 0u) return false;
        const char issuer[] = "TEST";
        const char label[] = "RFC 6238";
        std::memcpy(account.issuer, issuer, sizeof(issuer));
        std::memcpy(account.label, label, sizeof(label));
        static constexpr std::uint8_t secret[] = "12345678901234567890";
        std::memcpy(account.secret, secret, sizeof(secret) - 1u);
        account.secret_length = sizeof(secret) - 1u;
        account.digits = 8u;
        account.period_seconds = 30u;
        return true;
    }
    bool begin_secure_enrollment() noexcept override { return connected; }
};

static void tap(SystemShellController& shell, int x, int y) {
    assert(shell.handle_event({fahim::ui::PointerPhase::Down, x, y, 1u}));
    assert(shell.handle_event({fahim::ui::PointerPhase::Up, x, y, 2u}));
}
}

static void test_rfc6238_totp_sha1_vector() {
    static constexpr std::uint8_t secret[] = "12345678901234567890";
    char output[9]{};
    assert(fahim::security::compute_totp(secret, sizeof(secret) - 1u,
                                         59u, 30u, 8u, output, sizeof(output)));
    assert(std::strcmp(output, "94287082") == 0);
    assert(fahim::security::compute_totp(secret, sizeof(secret) - 1u,
                                         59u, 30u, 6u, output, sizeof(output)));
    assert(std::strcmp(output, "287082") == 0);
    assert(!fahim::security::compute_totp(secret, sizeof(secret) - 1u,
                                          59u, 0u, 6u, output, sizeof(output)));
}

static void test_native_system_shell_navigation_and_services() {
    constexpr std::uint32_t width=440u, height=900u;
    const std::size_t count=static_cast<std::size_t>(width)*height;
    std::vector<std::uint32_t> pixels(count), scratch(count);
    std::vector<std::uint8_t> framebuffer(count*4u);
    SystemShellRenderer renderer(pixels.data(),width,height,count);
    assert(renderer.valid());
    fahim::ui::FramebufferDisplay display(
        {framebuffer.data(),framebuffer.size(),width,height,width*4u,
         fahim::ui::PixelFormat::XRGB8888});
    TestCredential verifier;
    auto now=fahim::systemui::LockScreenCore::TimePoint{};
    fahim::systemui::LockScreenCore pin_core(verifier,[&now]{return now;});
    // Start in unlocked state for this system-shell test.
    for(const char digit: "2468") { if(digit!='\0') pin_core.append_digit(digit); }
    assert(pin_core.submit_pin()==fahim::systemui::SubmitResult::Accepted);
    fahim::systemui::KernelSessionControl kernel_control(
        [](std::uint32_t number,std::uint64_t value) noexcept -> std::int64_t {
            (void)number; (void)value; return 0;
        });

    std::vector<std::uint32_t> lock_pixels(count);
    fahim::systemui::LockScreenRenderer lock_renderer(
        lock_pixels.data(),width,height,count);
    fahim::systemui::LockScreenController lock_screen(pin_core,kernel_control,lock_renderer);
    fahim::ui::LockScreenSessionGate gate(pin_core);
    fahim::ui::Compositor compositor(display,gate,scratch.data(),scratch.size());
    TestPlatform platform;
    TestVault vault;
    SystemShellController shell(pin_core,lock_screen,kernel_control,
                                &platform,&vault,renderer,compositor);
    assert(shell.screen()==SystemScreen::Home);
    assert(shell.render());
    assert(compositor.surface_count()==1u);

    tap(shell,160,420); // Authenticator home tile
    assert(shell.screen()==SystemScreen::Authenticator);
    assert(shell.state().code_count==1u);
    assert(std::strcmp(shell.state().codes[0].code,"94287082")==0);

    tap(shell,60,90); // back to home
    assert(shell.screen()==SystemScreen::Home);

    // Swipe from the top edge opens Control Center.
    shell.handle_event({fahim::ui::PointerPhase::Down,220,20,10});
    shell.handle_event({fahim::ui::PointerPhase::Up,220,180,20});
    assert(shell.screen()==SystemScreen::ControlCenter);
    tap(shell,80,250);
    assert(platform.radio_calls==1u && platform.last_radio==SystemRadio::Wifi);
    assert(platform.status.wifi_enabled);

    // Open Settings from Control Center.
    tap(shell,120,680);
    assert(shell.screen()==SystemScreen::Settings);
    tap(shell,150,410); // Theme row
    assert(platform.status.theme==SystemTheme::Light);
    tap(shell,120,700); // Lock device
    assert(pin_core.state()==LockState::Locked);
    assert(compositor.surface_count()==0u);
}

static void test_missing_services_never_claim_success() {
    constexpr std::uint32_t width=240u,height=480u;
    const std::size_t count=static_cast<std::size_t>(width)*height;
    std::vector<std::uint32_t> pixels(count),scratch(count);
    SystemShellRenderer renderer(pixels.data(),width,height,count);
    assert(renderer.hit_test(60,250,SystemScreen::Home)==SystemShellRenderer::Hit::None);
    assert(renderer.hit_test(160,260,SystemScreen::Home)==SystemShellRenderer::Hit::OpenAuthenticator);
}

int main() {
    test_rfc6238_totp_sha1_vector();
    test_native_system_shell_navigation_and_services();
    test_missing_services_never_claim_success();
    std::cout << "Fahim Native System Shell tests: PASS\n";
    return 0;
}
