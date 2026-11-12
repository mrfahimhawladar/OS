#include "fahim/security/CredentialBackend.hpp"
#include "fahim/security/TeeCredentialBackend.hpp"
#include "fahim/security/KernelSessionControl.hpp"
#include "fahim/systemui/LockScreenView.hpp"
#include "fahim/input/InputDevice.hpp"
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


static void test_touchscreen_input_pipeline() {
    class QueueDevice final : public fahim::input::RawTouchDevice {
    public:
        std::vector<fahim::input::RawTouchSample> samples;
        std::size_t next{0};
        bool read_sample(fahim::input::RawTouchSample& sample) noexcept override {
            if (next >= samples.size()) return false;
            sample = samples[next++];
            return true;
        }
    } device;

    device.samples = {
        {PointerPhase::Down, 2048, 4000, 100},
        {PointerPhase::Up, 2048, 0, 140}
    };
    fahim::input::TouchscreenCalibrator calibration(
        fahim::input::TouchCalibration{0, 4095, 0, 4095, 480, 800,
                                       fahim::input::Rotation::Deg0});
    assert(calibration.valid());

    fahim::input::TouchInputPipeline pipeline(device, calibration);
    assert(pipeline.valid());
    assert(pipeline.pump_one());
    assert(pipeline.pump_one());
    assert(pipeline.queued_events() == 2u);

    LockScreenInputRouter router(72u);
    fahim::ui::InputDecision decision{};
    assert(pipeline.dispatch_one(router, decision));
    assert(decision.target == InputTarget::LockScreen);
    assert(pipeline.dispatch_one(router, decision));
    assert(decision.target == InputTarget::LockScreen);
    assert(decision.action == LockScreenAction::BeginUnlock);
    assert(!pipeline.dispatch_one(router, decision));

    fahim::input::TouchscreenCalibrator invalid(
        fahim::input::TouchCalibration{10, 10, 0, 4095, 480, 800,
                                       fahim::input::Rotation::Deg0});
    assert(!invalid.valid());
    fahim::ui::PointerEvent ignored{};
    assert(!invalid.normalize(device.samples[0], ignored));
}


static bool mock_kernel_session_locked = true;

static std::int64_t mock_kernel_session_transport(
    std::uint32_t number, std::uint64_t argument) noexcept {
    if (number == fahim::systemui::KernelSessionControl::kGetSessionLockedSyscall) {
        return mock_kernel_session_locked ? 1 : 0;
    }
    if (number == fahim::systemui::KernelSessionControl::kSetSessionLockedSyscall) {
        if (argument > 1u) return -22;
        mock_kernel_session_locked = argument != 0u;
        return 0;
    }
    return -38;
}

static void test_kernel_session_bridge() {
    TestProtectedBackend backend;
    ProtectedCredentialVerifier verifier(&backend);
    LockScreenCore core(verifier);
    fahim::systemui::KernelSessionControl control(&mock_kernel_session_transport);

    mock_kernel_session_locked = true;
    bool locked = false;
    assert(control.is_locked(locked) == 0);
    assert(locked);
    assert(control.synchronize(core) == 0);
    assert(mock_kernel_session_locked);

    enter_pin(core, "2468");
    assert(core.submit_pin() == fahim::systemui::SubmitResult::Accepted);
    assert(control.synchronize(core) == 0);
    assert(!mock_kernel_session_locked);
    assert(control.is_locked(locked) == 0);
    assert(!locked);

    core.lock();
    assert(control.synchronize(core) == 0);
    assert(mock_kernel_session_locked);
}


static void test_native_lock_screen_renderer_and_controller() {
    constexpr std::uint32_t width = 240u;
    constexpr std::uint32_t height = 480u;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<std::uint32_t> pixels(count, 0u);
    fahim::systemui::LockScreenRenderer renderer(
        pixels.data(), width, height, pixels.size());
    assert(renderer.valid());

    fahim::systemui::LockScreenFrameInfo frame{};
    fahim::systemui::LockScreenViewState view_state{};
    renderer.render(frame, view_state);
    assert((pixels[0] >> 24u) == 0xffu);
    assert(renderer.camera_shortcut_bounds().width > 0u);
    assert(renderer.flashlight_shortcut_bounds().width > 0u);
    assert(renderer.hit_test_pin(120, 231) == fahim::systemui::PinHit::Digit2);
    assert(renderer.hit_test_pin(120, 442) == fahim::systemui::PinHit::Submit);

    TestProtectedBackend backend;
    ProtectedCredentialVerifier verifier(&backend);
    LockScreenCore core(verifier);
    fahim::systemui::KernelSessionControl control(&mock_kernel_session_transport);
    mock_kernel_session_locked = true;
    fahim::systemui::LockScreenController controller(core, control, renderer);
    assert(mock_kernel_session_locked);

    // Register the actual native ARGB lock-screen bitmap with the software compositor.
    std::vector<std::uint8_t> framebuffer(count * sizeof(std::uint32_t), 0u);
    FramebufferDisplay display(
        FramebufferConfig{framebuffer.data(), framebuffer.size(), width, height,
                          width * 4u, PixelFormat::XRGB8888});
    std::vector<std::uint32_t> scratch(count, 0u);
    fahim::ui::LockScreenSessionGate gate(core);
    Compositor compositor(display, gate, scratch.data(), scratch.size());
    assert(controller.render_to_compositor(frame, compositor, 77u, 10));
    assert(compositor.surface_count() == 1u);
    assert(compositor.compose());
    const auto* framebuffer_pixels =
        reinterpret_cast<const std::uint32_t*>(framebuffer.data());
    assert(framebuffer_pixels[0] != 0u);

    // Raw touch samples are calibrated, queued and delivered to the controller.
    class QueueTouchDevice final : public fahim::input::RawTouchDevice {
    public:
        fahim::input::RawTouchSample samples[2]{
            {PointerPhase::Down, 2048, 4095, 10},
            {PointerPhase::Up, 2048, 1600, 40}
        };
        std::size_t index{0};
        bool read_sample(fahim::input::RawTouchSample& sample) noexcept override {
            if (index >= 2u) return false;
            sample = samples[index++];
            return true;
        }
    } touch_device;

    fahim::input::TouchscreenCalibrator calibration(
        fahim::input::TouchCalibration{0, 4095, 0, 4095, width, height,
                                       fahim::input::Rotation::Deg0});
    fahim::input::TouchInputPipeline input(touch_device, calibration);
    assert(input.pump_one());
    assert(input.pump_one());
    assert(controller.pump_input(input));
    assert(input.queued_events() == 0u);
    assert(controller.bouncer_open());

    assert(controller.render_to_compositor(frame, compositor, 77u, 10));
    assert(compositor.compose());

    // Enter the test-only credential 2468 using on-screen key hit targets.
    const auto tap = [&controller](std::int32_t x, std::int32_t y,
                                   std::uint64_t timestamp) {
        return controller.handle_event(
            PointerEvent{PointerPhase::Up, x, y, timestamp});
    };
    assert(tap(120, 231, 50)); // 2
    assert(tap(56, 281, 60));  // 4
    assert(tap(184, 281, 70)); // 6
    assert(tap(120, 331, 80)); // 8
    assert(core.input_length() == 4u);
    assert(!tap(120, 442, 90)); // Unlock hands control to the app session

    assert(core.state() == LockState::Unlocked);
    assert(!controller.bouncer_open());
    assert(!mock_kernel_session_locked);
    assert(compositor.surface_count() == 1u);
    assert(!controller.render_to_compositor(frame, compositor, 77u, 10));
    assert(compositor.surface_count() == 0u);

    controller.lock();
    assert(core.state() == LockState::Locked);
    assert(mock_kernel_session_locked);
    assert(controller.render_to_compositor(frame, compositor, 77u, 10));
    assert(compositor.surface_count() == 1u);
}


class FakeTeeCredentialTransport final : public fahim::security::TeeCredentialTransport {
public:
    bool connected{true};
    bool enrolled{false};
    bool locked{false};
    std::uint64_t retry_after_ms{0};
    std::uint32_t failures{0};
    std::uint8_t test_pin[4]{'2', '4', '6', '8'};

    bool ready() const noexcept override { return connected; }

    fahim::security::TeeCredentialResult query(
        fahim::security::TeeCredentialState& state) const noexcept override {
        state.enrolled = enrolled;
        state.failed_attempts = failures;
        state.retry_after_ms = retry_after_ms;
        if (!connected) return fahim::security::TeeCredentialResult::Unavailable;
        if (!enrolled) return fahim::security::TeeCredentialResult::NotEnrolled;
        if (locked) return fahim::security::TeeCredentialResult::LockedOut;
        return fahim::security::TeeCredentialResult::Ok;
    }

    fahim::security::TeeCredentialResult enroll(
        const std::uint8_t* pin, std::size_t length,
        fahim::security::TeeCredentialState& state) const noexcept override {
        if (!connected) return fahim::security::TeeCredentialResult::Unavailable;
        if (enrolled) return fahim::security::TeeCredentialResult::AlreadyEnrolled;
        if (pin == nullptr || length != sizeof(test_pin))
            return fahim::security::TeeCredentialResult::Invalid;
        auto* self = const_cast<FakeTeeCredentialTransport*>(this);
        std::memcpy(self->test_pin, pin, length);
        self->enrolled = true;
        state.enrolled = true;
        return fahim::security::TeeCredentialResult::Ok;
    }

    fahim::security::TeeCredentialResult verify(
        const std::uint8_t* pin, std::size_t length,
        fahim::security::TeeCredentialState& state) const noexcept override {
        auto* self = const_cast<FakeTeeCredentialTransport*>(this);
        if (!connected) return fahim::security::TeeCredentialResult::Unavailable;
        if (!enrolled) return fahim::security::TeeCredentialResult::NotEnrolled;
        if (locked) {
            state.enrolled = true;
            state.failed_attempts = failures;
            state.retry_after_ms = retry_after_ms;
            return fahim::security::TeeCredentialResult::LockedOut;
        }
        const bool accepted = pin != nullptr && length == sizeof(test_pin) &&
            std::memcmp(pin, test_pin, sizeof(test_pin)) == 0;
        if (accepted) {
            self->failures = 0u;
            self->retry_after_ms = 0u;
            state.enrolled = true;
            return fahim::security::TeeCredentialResult::Ok;
        }
        ++self->failures;
        if (self->failures >= 5u) {
            self->locked = true;
            self->retry_after_ms = 30000u;
            state.enrolled = true;
            state.failed_attempts = self->failures;
            state.retry_after_ms = self->retry_after_ms;
            return fahim::security::TeeCredentialResult::LockedOut;
        }
        state.enrolled = true;
        state.failed_attempts = self->failures;
        return fahim::security::TeeCredentialResult::Rejected;
    }
};

static void test_tee_credential_backend_adapter() {
    FakeTeeCredentialTransport transport;
    fahim::security::TeeProtectedCredentialBackend backend(&transport);
    assert(backend.available());
    fahim::security::TeeCredentialState state{};
    assert(backend.query_state(state) ==
           fahim::security::TeeCredentialResult::NotEnrolled);
    assert(!state.enrolled);

    const std::uint8_t pin[]{'2', '4', '6', '8'};
    assert(backend.enroll_pin(pin, sizeof(pin)) ==
           fahim::security::TeeCredentialResult::Ok);
    assert(backend.enroll_pin(pin, sizeof(pin)) ==
           fahim::security::TeeCredentialResult::AlreadyEnrolled);

    ProtectedCredentialVerifier verifier(&backend);
    LockScreenCore core(verifier);
    enter_pin(core, "2468");
    assert(core.submit_pin() == fahim::systemui::SubmitResult::Accepted);
    core.lock();

    // The mock transport models a lockout surviving destruction/recreation of
    // LockScreenCore. The real TA stores this deadline in RPMB secure storage.
    transport.locked = true;
    transport.retry_after_ms = 125000u;
    auto secure_time = LockScreenCore::TimePoint{};
    LockScreenCore rebooted_core(verifier, [&secure_time] { return secure_time; });
    enter_pin(rebooted_core, "2468");
    assert(rebooted_core.submit_pin() == fahim::systemui::SubmitResult::LockedOut);
    assert(rebooted_core.lockout_remaining() == std::chrono::milliseconds(125000));

    transport.connected = false;
    assert(!backend.available());
    ProtectedCredentialVerifier no_tee_verifier(&backend);
    std::uint8_t unavailable_pin[]{'2', '4', '6', '8'};
    assert(no_tee_verifier.verify_pin(unavailable_pin, sizeof(unavailable_pin)) ==
           VerificationResult::TemporarilyUnavailable);
}


static void test_optee_transport_fails_closed_without_client_support() {
    fahim::security::OpteeCredentialTransport transport;
    assert(!transport.ready());

    fahim::security::TeeProtectedCredentialBackend backend(&transport);
    assert(!backend.available());

    std::uint8_t pin[]{'2', '4', '6', '8'};
    assert(backend.verify_pin_and_update_policy(pin, sizeof(pin)) ==
           VerificationResult::TemporarilyUnavailable);
    assert(backend.enroll_pin(pin, sizeof(pin)) ==
           fahim::security::TeeCredentialResult::Unavailable);
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
    test_touchscreen_input_pipeline();
    test_gate_and_compositor();
    test_kernel_session_bridge();
    test_native_lock_screen_renderer_and_controller();
    test_tee_credential_backend_adapter();
    test_optee_transport_fails_closed_without_client_support();
    test_missing_credential_backend_fails_closed();
    std::cout << "Fahim SystemUI runtime tests: PASS\n";
    return 0;
}
