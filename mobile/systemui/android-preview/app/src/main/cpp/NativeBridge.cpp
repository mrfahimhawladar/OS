#include <jni.h>

#include "fahim/systemui/LockScreenView.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr jint kFrameWidth = 440;
constexpr jint kFrameHeight = 900;

std::string from_java(JNIEnv* env, jstring value, const char* fallback) {
    if (value == nullptr) return fallback;
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return fallback;
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}
}  // namespace

extern "C" JNIEXPORT jintArray JNICALL
Java_com_fahimos_preview_NativeRenderer_renderFrame(
    JNIEnv* env, jobject /* receiver */, jint mode, jint pin_digits,
    jstring message, jstring time, jstring date,
    jstring network, jstring battery) {
    const std::string message_text = from_java(env, message, "");
    const std::string time_text = from_java(env, time, "7:12");
    const std::string date_text = from_java(env, date, "THU, NOV 12");
    const std::string network_text = from_java(env, network, "MOBILE");
    const std::string battery_text = from_java(env, battery, "86%");

    const std::size_t pixel_count =
        static_cast<std::size_t>(kFrameWidth) * kFrameHeight;
    std::vector<std::uint32_t> pixels(pixel_count, 0u);

    fahim::systemui::LockScreenRenderer renderer(
        pixels.data(), kFrameWidth, kFrameHeight, pixels.size());
    if (!renderer.valid()) return nullptr;

    fahim::systemui::LockScreenFrameInfo frame{};
    frame.time = time_text.c_str();
    frame.date = date_text.c_str();
    frame.network = network_text.c_str();
    frame.battery = battery_text.c_str();
    frame.notification_title = "MESSAGES";
    frame.notification_body = "YOUR NOTIFICATIONS APPEAR HERE";

    fahim::systemui::LockScreenViewState state{};
    state.bouncer_open = mode == 1 || mode == 2 || mode == 3;
    state.pin_digits = static_cast<std::size_t>(
        pin_digits < 0 ? 0 : pin_digits > 6 ? 6 : pin_digits);
    state.flashlight_on = mode == 4;
    state.locked_out = mode == 3;
    state.lockout_seconds = mode == 3 ? 30u : 0u;
    state.message = message_text.empty() ? nullptr : message_text.c_str();

    renderer.render(frame, state);

    jintArray output = env->NewIntArray(static_cast<jsize>(pixel_count));
    if (output == nullptr) return nullptr;
    env->SetIntArrayRegion(output, 0, static_cast<jsize>(pixel_count),
                           reinterpret_cast<const jint*>(pixels.data()));
    return output;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_fahimos_preview_NativeRenderer_hitTestPin(
    JNIEnv* /* env */, jobject /* receiver */, jint x, jint y) {
    constexpr std::size_t pixel_count =
        static_cast<std::size_t>(kFrameWidth) * kFrameHeight;
    std::vector<std::uint32_t> pixels(pixel_count, 0u);
    fahim::systemui::LockScreenRenderer renderer(
        pixels.data(), kFrameWidth, kFrameHeight, pixels.size());
    if (!renderer.valid()) return 0;
    return static_cast<jint>(renderer.hit_test_pin(x, y));
}
