#pragma once

#include <cstddef>
#include <cstdint>

namespace fahim::ui {

struct Color {
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
    std::uint8_t alpha{255};
};

struct Rect {
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

enum class PixelFormat : std::uint8_t {
    XRGB8888,
    RGB565
};

struct FramebufferConfig {
    volatile void* memory{nullptr};
    std::size_t memory_bytes{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t stride_bytes{0};
    PixelFormat format{PixelFormat::XRGB8888};
};

using FlushCallback = void (*)(void* context, const Rect& damage) noexcept;

class DisplayDevice {
public:
    virtual ~DisplayDevice() = default;
    virtual bool valid() const noexcept = 0;
    virtual std::uint32_t width() const noexcept = 0;
    virtual std::uint32_t height() const noexcept = 0;
    virtual void clear(Color color) noexcept = 0;
    virtual bool draw_pixel(std::int32_t x, std::int32_t y, Color color) noexcept = 0;
    virtual bool fill_rect(Rect rect, Color color) noexcept = 0;
    virtual bool present() noexcept = 0;
};

// A checked linear-framebuffer adapter. The platform driver must first map the
// framebuffer memory and pass its exact byte length and stride to this class.
class FramebufferDisplay final : public DisplayDevice {
public:
    explicit FramebufferDisplay(FramebufferConfig config,
                                FlushCallback flush = nullptr,
                                void* flush_context = nullptr) noexcept;

    bool valid() const noexcept override { return valid_; }
    std::uint32_t width() const noexcept override { return config_.width; }
    std::uint32_t height() const noexcept override { return config_.height; }
    void clear(Color color) noexcept override;
    bool draw_pixel(std::int32_t x, std::int32_t y, Color color) noexcept override;
    bool fill_rect(Rect rect, Color color) noexcept override;
    bool present() noexcept override;

private:
    std::uint32_t bytes_per_pixel() const noexcept;
    void write_pixel(std::uint32_t x, std::uint32_t y, Color color) noexcept;

    FramebufferConfig config_{};
    FlushCallback flush_{nullptr};
    void* flush_context_{nullptr};
    bool valid_{false};
    Rect damage_{};
    bool has_damage_{false};
};

}  // namespace fahim::ui
