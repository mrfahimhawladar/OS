#pragma once

#include "fahim/ui/Display.hpp"
#include "fahim/ui/SessionGate.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fahim::ui {

struct Surface {
    std::uint32_t id{0};
    const std::uint32_t* argb_pixels{nullptr};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t stride_pixels{0};
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t z{0};
    SurfaceRole role{SurfaceRole::Application};
    bool visible{true};
};

// Fixed-capacity compositor; pixel scratch memory is supplied by the runtime
// allocator. Pixels use 0xAARRGGBB and are alpha-composited in ascending z order.
class Compositor final {
public:
    static constexpr std::size_t kMaxSurfaces = 16;

    Compositor(DisplayDevice& display, const LockScreenSessionGate& gate,
               std::uint32_t* scratch_argb, std::size_t scratch_pixels) noexcept;

    bool valid() const noexcept;
    bool upsert_surface(const Surface& surface) noexcept;
    bool remove_surface(std::uint32_t id) noexcept;
    void set_background(Color color) noexcept { background_ = color; }
    bool compose() noexcept;
    std::size_t surface_count() const noexcept { return count_; }

private:
    static std::uint32_t blend(std::uint32_t source,
                               std::uint32_t destination) noexcept;

    DisplayDevice& display_;
    const LockScreenSessionGate& gate_;
    std::uint32_t* scratch_{nullptr};
    std::size_t scratch_pixels_{0};
    std::array<Surface, kMaxSurfaces> surfaces_{};
    std::size_t count_{0};
    Color background_{12, 18, 30, 255};
};

}  // namespace fahim::ui
