#include "fahim/systemui/LockScreenView.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool write_ppm(const std::filesystem::path& path,
               const fahim::systemui::LockScreenRenderer& renderer) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;

    output << "P6\n" << renderer.width() << ' ' << renderer.height() << "\n255\n";
    const std::uint32_t* pixels = renderer.pixels();
    for (std::size_t i = 0;
         i < static_cast<std::size_t>(renderer.width()) * renderer.height(); ++i) {
        const std::uint32_t pixel = pixels[i];
        const char rgb[3] = {
            static_cast<char>((pixel >> 16u) & 0xffu),
            static_cast<char>((pixel >> 8u) & 0xffu),
            static_cast<char>(pixel & 0xffu)
        };
        output.write(rgb, sizeof(rgb));
    }
    return output.good();
}

}  // namespace

int main(int argc, char** argv) {
    constexpr std::uint32_t width = 440u;
    constexpr std::uint32_t height = 900u;
    const std::filesystem::path output_dir =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::path("native-lockscreen-output");

    std::error_code error;
    std::filesystem::create_directories(output_dir, error);
    if (error) {
        std::cerr << "Cannot create output directory: " << error.message() << '\n';
        return 1;
    }

    std::vector<std::uint32_t> framebuffer(
        static_cast<std::size_t>(width) * height, 0u);
    fahim::systemui::LockScreenRenderer renderer(
        framebuffer.data(), width, height, framebuffer.size());
    if (!renderer.valid()) {
        std::cerr << "Native Lock Screen renderer rejected framebuffer\n";
        return 2;
    }

    fahim::systemui::LockScreenFrameInfo frame{};
    frame.time = "7:12";
    frame.date = "THU, NOV 12";
    frame.network = "WIFI";
    frame.battery = "86%";
    frame.notification_title = "MESSAGES";
    frame.notification_body = "Your notifications appear here";

    // Screenshot 1: actual native renderer's default locked state.
    fahim::systemui::LockScreenViewState locked{};
    renderer.render(frame, locked);
    if (!write_ppm(output_dir / "native-lockscreen.ppm", renderer)) {
        std::cerr << "Failed to write native lock-screen frame\n";
        return 3;
    }

    // Screenshot 2: actual native renderer's PIN bouncer state. This is UI
    // rendering only: no credential is entered or accepted by this utility.
    fahim::systemui::LockScreenViewState bouncer{};
    bouncer.bouncer_open = true;
    bouncer.pin_digits = 0u;
    renderer.render(frame, bouncer);
    if (!write_ppm(output_dir / "native-pin-bouncer.ppm", renderer)) {
        std::cerr << "Failed to write native PIN bouncer frame\n";
        return 4;
    }

    std::cout << "Rendered both states using FahimOS LockScreenRenderer C++ source.\n";
    std::cout << "No HTML/CSS mockup and no credential verification were used.\n";
    return 0;
}
