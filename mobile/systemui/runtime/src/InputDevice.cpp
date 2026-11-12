#include "fahim/input/InputDevice.hpp"

#include <algorithm>
#include <cstdint>

namespace fahim::input {

TouchscreenCalibrator::TouchscreenCalibrator(
    TouchCalibration calibration) noexcept : calibration_(calibration) {
    valid_ = calibration_.max_x > calibration_.min_x &&
             calibration_.max_y > calibration_.min_y &&
             calibration_.screen_width > 0u &&
             calibration_.screen_height > 0u &&
             calibration_.screen_width <= 16384u &&
             calibration_.screen_height <= 16384u;
}

bool TouchscreenCalibrator::normalize(
    const RawTouchSample& raw,
    fahim::ui::PointerEvent& normalized) const noexcept {
    if (!valid_) return false;
    constexpr std::uint64_t kAxisScale = 65535u;
    const std::int64_t x_range =
        static_cast<std::int64_t>(calibration_.max_x) - calibration_.min_x;
    const std::int64_t y_range =
        static_cast<std::int64_t>(calibration_.max_y) - calibration_.min_y;
    const std::int64_t raw_x = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(raw.raw_x) - calibration_.min_x, 0, x_range);
    const std::int64_t raw_y = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(raw.raw_y) - calibration_.min_y, 0, y_range);

    std::uint64_t u = static_cast<std::uint64_t>(raw_x) * kAxisScale /
                      static_cast<std::uint64_t>(x_range);
    std::uint64_t v = static_cast<std::uint64_t>(raw_y) * kAxisScale /
                      static_cast<std::uint64_t>(y_range);

    switch (calibration_.rotation) {
        case Rotation::Deg90: {
            const std::uint64_t old_u = u;
            u = kAxisScale - v;
            v = old_u;
            break;
        }
        case Rotation::Deg180:
            u = kAxisScale - u;
            v = kAxisScale - v;
            break;
        case Rotation::Deg270: {
            const std::uint64_t old_u = u;
            u = v;
            v = kAxisScale - old_u;
            break;
        }
        case Rotation::Deg0:
        default:
            break;
    }

    const auto x = static_cast<std::int32_t>(
        u * (calibration_.screen_width - 1u) / kAxisScale);
    const auto y = static_cast<std::int32_t>(
        v * (calibration_.screen_height - 1u) / kAxisScale);
    normalized = fahim::ui::PointerEvent{
        raw.phase, x, y, raw.timestamp_ms
    };
    return true;
}

bool InputEventQueue::push(const fahim::ui::PointerEvent& event) noexcept {
    if (size_ >= events_.size()) return false;
    events_[tail_] = event;
    tail_ = (tail_ + 1u) % events_.size();
    ++size_;
    return true;
}

bool InputEventQueue::pop(fahim::ui::PointerEvent& event) noexcept {
    if (size_ == 0u) return false;
    event = events_[head_];
    head_ = (head_ + 1u) % events_.size();
    --size_;
    return true;
}

TouchInputPipeline::TouchInputPipeline(
    RawTouchDevice& device, TouchscreenCalibrator calibrator) noexcept
    : device_(device), calibrator_(calibrator) {}

bool TouchInputPipeline::pump_one() noexcept {
    if (!calibrator_.valid() || queue_.size() >= InputEventQueue::kCapacity) return false;
    RawTouchSample raw{};
    if (!device_.read_sample(raw)) return false;
    fahim::ui::PointerEvent normalized{};
    if (!calibrator_.normalize(raw, normalized)) return false;
    return queue_.push(normalized);
}

bool TouchInputPipeline::next_event(fahim::ui::PointerEvent& event) noexcept {
    return queue_.pop(event);
}

bool TouchInputPipeline::dispatch_one(
    fahim::ui::LockScreenInputRouter& router,
    fahim::ui::InputDecision& decision) noexcept {
    fahim::ui::PointerEvent event{};
    if (!queue_.pop(event)) return false;
    decision = router.route(event);
    return true;
}

}  // namespace fahim::input
