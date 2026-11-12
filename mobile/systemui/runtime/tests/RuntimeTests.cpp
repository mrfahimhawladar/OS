#include "fahim/security/CredentialBackend.hpp"
#include "fahim/ui/Compositor.hpp"
#include "fahim/ui/InputRouter.hpp"
#include "fahim/ui/SessionGate.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using fahim::security::ProtectedCredentialBackend;
using fahim::security::ProtectedCredentialVerifier;
using fahim::systemui::LockScreenCore;
using fahim::systemui::LockState;
using fahim::systemui::VerificationResult;
using fahim::ui::Color;
using fahim::ui::Compositor;
using fahim::ui::FramebufferConfig;
using fahim::ui::FramebufferDisplay;
using fahim::ui::InputTarget;
using fahim::ui::LockScreenAction;
using fahim::ui::LockScreenInputRouter;
using fahim::ui::PixelFormat;
using fahim::ui::PointerEvent;
using fahim::ui::PointerPhase;
using fahim::ui::Rect;
using fahim::ui::Surface;
using fahim::ui::SurfaceRole;

class TestProtectedBackend final : public ProtectedCredentialBackend {
public:
    bool enabled{true};

    bool available() const noexcept override { return enabled; }

    VerificationResult verify_pin_and_update_policy(
        const std::uint8_t* pin, std::size_t length) noexcept override {
        return enabled && length == 4 &&
               std::memcmp(pin, "2468", 4) == 0
            ? VerificationResult::Accepted
            : VerificationResult::Rejected;
    }
};

static void enter_pin(LockScreenCore& core, const char* pin) {
    for (const char* p = pin; *p != '\0'; ++p) assert(core.append_digit(*p));
}

static void test_framebuffer() {
    std::vector<std::uint8_t> memory(4u * 3u * 4u, 0u);
    FramebufferDisplay display(
        FramebufferConfig{memory.data(), memory.size(), 4u, 3u, 16u,
                          PixelFormat::XRGB8888});
    assert(display.valid());
    assert(display.width() == 4u && display.height() == 3u);
    assert(display.draw_pixel(3, 2, Color{255, 0, 0, 255}));
    assert(!display.draw_pixel(-1, 1, Color{0, 0, 0, 255}));
    assert(display.fill_rect(Rect{-1, 0, 3u, 2u}, Color{0, 255, 0, 255}));
    assert(display.present());

    auto* pixels = reinterpret_cast<const std::uint32_t*>(memory.data());
    assert(pixels[0] == 0xff00ff00u);
    assert(pixels[1] == 0xff00ff00u);
    assert(pixels[5] == 0xff00ff00u);
    assert(pixels[11] == 0xffff0000u);

    FramebufferDisplay invalid(
        FramebufferConfig{memory.data(), memory.size(), 10u, 10u, 4u,
                          PixelFormat::XRGB8888});
    assert(!invalid.valid());

    std::vector<std::uint8_t> rgb565_memory(2u * 2u * 2u, 0u);
    FramebufferDisplay rgb565(
        FramebufferConfig{rgb565_memory.data(), rgb565_memory.size(), 2u, 2u, 4u,
                          PixelFormat::RGB565});
    assert(rgb565.valid());
    assert(rgb565.draw_pixel(0, 0, Color{255, 0, 0, 255}));
    const auto* rgb565_pixel =
        reinterpret_cast<const std::uint16_t*>(rgb565_memory.data());
    assert(rgb565_pixel[0] == 0xf800u);
}

static void test_input_router() {
    LockScreenInputRouter router(72u);
    router.set_camera_bounds(Rect{0, 0, 20u, 20u});
    router.set_flashlight_bounds(Rect{80, 0, 20u, 20u});

    auto decision = router.route(PointerEvent{PointerPhase::Down, 100, 180, 10});
    assert(decision.target == InputTarget::LockScreen);
    decision = router.route(PointerEvent{PointerPhase::Up, 104, 90, 40});
    assert(decision.target == InputTarget::LockScreen);
    assert(decision.action == LockScreenAction::BeginUnlock);

    router.route(PointerEvent{PointerPhase::Down, 10, 10, 50});
    decision = router.route(PointerEvent{PointerPhase::Up, 10, 10, 80});
    assert(decision.action == LockScreenAction::OpenCamera);
    assert(decision.target == InputTarget::LockScreen);

    router.route(PointerEvent{PointerPhase::Down, 90, 10, 90});
    decision = router.route(PointerEvent{PointerPhase::Up, 90, 10, 120});
    assert(decision.action == LockScreenAction::ToggleFlashlight);
    assert(decision.target == InputTarget::LockScreen);

    router.set_locked(false);
    decision = router.route(PointerEvent{PointerPhase::Down, 5, 5, 130});
    assert(decision.target == InputTarget::Application);
}

static void test_gate_and_compositor() {
    TestProtectedBackend backend;
    ProtectedCredentialVerifier verifier(&backend);
    LockScreenCore core(verifier);
    fahim::ui::LockScreenSessionGate gate(core);

    assert(core.state() == LockState::Locked);
    assert(gate.can_present(SurfaceRole::LockScreen));
    assert(gate.can_present(SurfaceRole::PublicSystemOverlay));
    assert(!gate.can_present(SurfaceRole::Application));
    assert(!gate.can_present(SurfaceRole::EmergencyDialer));
    assert(!gate.can_dispatch_application_input());

    gate.set_emergency_active(true);
    assert(gate.can_present(SurfaceRole::EmergencyDialer));

    std::vector<std::uint8_t> framebuffer(4u * 4u * 4u, 0u);
    FramebufferDisplay display(
        FramebufferConfig{framebuffer.data(), framebuffer.size(), 4u, 4u, 16u,
                          PixelFormat::XRGB8888});
    std::vector<std::uint32_t> scratch(16u, 0u);
    Compositor compositor(display, gate, scratch.data(), scratch.size());
    assert(compositor.valid());

    std::vector<std::uint32_t> lockscreen_pixels(16u, 0xffff0000u);
    std::vector<std::uint32_t> application_pixels(16u, 0xff0000ffu);
    std::vector<std::uint32_t> overlay_pixels(16u, 0x8000ff00u);

    assert(compositor.upsert_surface(Surface{1u, lockscreen_pixels.data(), 4u, 4u, 4u,
                                             0, 0, 0, SurfaceRole::LockScreen, true}));
    assert(compositor.upsert_surface(Surface{2u, overlay_pixels.data(), 4u, 4u, 4u,
                                             0, 0, 2, SurfaceRole::PublicSystemOverlay, true}));
    assert(compositor.upsert_surface(Surface{3u, application_pixels.data(), 4u, 4u, 4u,
                                             0, 0, 3, SurfaceRole::Application, true}));
    assert(compositor.surface_count() == 3u);
    assert(compositor.compose());

    auto* rendered = reinterpret_cast<const std::uint32_t*>(framebuffer.data());
    // The app's blue surface is excluded. The public green overlay blends over red.
    assert(rendered[0] == 0xff7f8000u);

    enter_pin(core, "2468");
    assert(core.submit_pin() == fahim::systemui::SubmitResult::Accepted);
    assert(core.state() == LockState::Unlocked);
    assert(gate.can_present(SurfaceRole::Application));
    assert(gate.can_dispatch_application_input());

    assert(compositor.compose());
    rendered = reinterpret_cast<const std::uint32_t*>(framebuffer.data());
    assert(rendered[0] == 0xff0000ffu);

    assert(compositor.remove_surface(2u));
    assert(compositor.remove_surface(1u));
    assert(compositor.remove_surface(3u));
    assert(compositor.surface_count() == 0u);
    assert(!compositor.remove_surface(99u));
}

static void test_missing_credential_backend_fails_closed() {
    ProtectedCredentialVerifier verifier(nullptr);
    std::uint8_t pin[]{'2', '4', '6', '8'};
    assert(verifier.verify_pin(pin, sizeof(pin)) ==
           VerificationResult::TemporarilyUnavailable);
}

int main() {
    test_framebuffer();
    test_input_router();
    test_gate_and_compositor();
    test_missing_credential_backend_fails_closed();
    std::cout << "Fahim SystemUI runtime tests: PASS\n";
    return 0;
}
