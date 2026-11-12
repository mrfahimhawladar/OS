#pragma once

#include "fahim/ui/InputRouter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fahim::input {

struct RawTouchSample {
    fahim::ui::PointerPhase phase{fahim::ui::PointerPhase::Move};
    std::int32_t raw_x{0};
    std::int32_t raw_y{0};
    std::uint64_t timestamp_ms{0};
};

enum class Rotation : std::uint8_t { Deg0, Deg90, Deg180, Deg270 };

struct TouchCalibration {
    std::int32_t min_x{0};
    std::int32_t max_x{4095};
    std::int32_t min_y{0};
    std::int32_t max_y{4095};
    std::uint32_t screen_width{0};
    std::uint32_t screen_height{0};
    Rotation rotation{Rotation::Deg0};
};

class RawTouchDevice {
public:
    virtual ~RawTouchDevice() = default;
    // Return true when one sample is available; false means no event now.
    virtual bool read_sample(RawTouchSample& sample) noexcept = 0;
};

class TouchscreenCalibrator final {
public:
    explicit TouchscreenCalibrator(TouchCalibration calibration) noexcept;
    bool valid() const noexcept { return valid_; }
    bool normalize(const RawTouchSample& raw,
                   fahim::ui::PointerEvent& normalized) const noexcept;

private:
    TouchCalibration calibration_{};
    bool valid_{false};
};

class InputEventQueue final {
public:
    static constexpr std::size_t kCapacity = 32;

    bool push(const fahim::ui::PointerEvent& event) noexcept;
    bool pop(fahim::ui::PointerEvent& event) noexcept;
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0u; }

private:
    std::array<fahim::ui::PointerEvent, kCapacity> events_{};
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
};

// Platform controller drivers implement RawTouchDevice. This pipeline owns
// calibration/normalization and buffering before Lock Screen routing.
class TouchInputPipeline final {
public:
    TouchInputPipeline(RawTouchDevice& device,
                       TouchscreenCalibrator calibrator) noexcept;

    bool pump_one() noexcept;
    bool dispatch_one(fahim::ui::LockScreenInputRouter& router,
                      fahim::ui::InputDecision& decision) noexcept;
    std::size_t queued_events() const noexcept { return queue_.size(); }
    bool valid() const noexcept { return calibrator_.valid(); }

private:
    RawTouchDevice& device_;
    TouchscreenCalibrator calibrator_;
    InputEventQueue queue_{};
};

}  // namespace fahim::input
