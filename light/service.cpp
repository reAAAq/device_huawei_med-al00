#define LOG_TAG "enjoy10e-light"

#include <android/hardware/light/2.0/ILight.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <utils/Errors.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

using android::OK;
using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::hidl_handle;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using android::hardware::joinRpcThreadpool;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::light::V2_0::Brightness;
using android::hardware::light::V2_0::ILight;
using android::hardware::light::V2_0::LightState;
using android::hardware::light::V2_0::Status;
using android::hardware::light::V2_0::Type;

namespace {

constexpr const char* kBacklightPath = "/sys/class/leds/lcd-backlight/brightness";
constexpr const char* kBacklightMaxPath = "/sys/class/leds/lcd-backlight/max_brightness";
constexpr const char* kRedPath = "/sys/class/leds/red/brightness";
constexpr const char* kGreenPath = "/sys/class/leds/green/brightness";
constexpr const char* kBluePath = "/sys/class/leds/blue/brightness";
constexpr int kAalSafeRawMax = 4095;
constexpr int kRampMinDelta = 700;
constexpr int kRampStep = 512;
constexpr useconds_t kRampDelayUs = 16000;

int readInt(const char* path, int fallback) {
    std::ifstream file(path);
    int value = fallback;
    if (!(file >> value)) {
        ALOGW("Failed to read %s, using %d", path, fallback);
    }
    return value;
}

bool writeInt(const char* path, int value) {
    std::ofstream file(path);
    if (!(file << value)) {
        ALOGE("Failed to write %s=%d: %s", path, value, strerror(errno));
        return false;
    }
    return true;
}

int colorToBrightness(uint32_t color) {
    const int red = (color >> 16) & 0xff;
    const int green = (color >> 8) & 0xff;
    const int blue = color & 0xff;
    return ((77 * red) + (150 * green) + (29 * blue)) >> 8;
}

bool writeBacklightSmooth(int target) {
    const int current = readInt(kBacklightPath, target);
    if (target <= current || target - current <= kRampMinDelta) {
        return writeInt(kBacklightPath, target);
    }

    bool ok = true;
    for (int value = current + kRampStep; value < target; value += kRampStep) {
        ok &= writeInt(kBacklightPath, value);
        usleep(kRampDelayUs);
    }
    ok &= writeInt(kBacklightPath, target);
    return ok;
}

Status setBacklight(const LightState& state) {
    if (state.brightnessMode == Brightness::LOW_PERSISTENCE) {
        return Status::BRIGHTNESS_NOT_SUPPORTED;
    }

    const int panelMaxBrightness = std::max(readInt(kBacklightMaxPath, 255), 1);
    const int frameworkBrightness = std::clamp(colorToBrightness(state.color), 0, 255);
    const int rawBrightness =
            frameworkBrightness == 0
                    ? 0
                    : std::clamp((frameworkBrightness * kAalSafeRawMax + 127) / 255, 1,
                                 kAalSafeRawMax);

    ALOGI("backlight framework=%d rawTarget=%d safeRawMax=%d panelMax=%d", frameworkBrightness,
          rawBrightness, kAalSafeRawMax, panelMaxBrightness);
    return writeBacklightSmooth(rawBrightness) ? Status::SUCCESS : Status::UNKNOWN;
}

Status setRgb(const LightState& state) {
    const int red = (state.color >> 16) & 0xff;
    const int green = (state.color >> 8) & 0xff;
    const int blue = state.color & 0xff;

    bool ok = true;
    ok &= writeInt(kRedPath, red);
    ok &= writeInt(kGreenPath, green);
    ok &= writeInt(kBluePath, blue);
    return ok ? Status::SUCCESS : Status::UNKNOWN;
}

class Enjoy10eLight : public ILight {
  public:
    Return<Status> setLight(Type type, const LightState& state) override {
        switch (type) {
            case Type::BACKLIGHT:
                return setBacklight(state);
            case Type::BATTERY:
            case Type::NOTIFICATIONS:
            case Type::ATTENTION:
                return setRgb(state);
            default:
                return Status::LIGHT_NOT_SUPPORTED;
        }
    }

    Return<void> getSupportedTypes(getSupportedTypes_cb cb) override {
        hidl_vec<Type> types;
        types.resize(4);
        types[0] = Type::BACKLIGHT;
        types[1] = Type::BATTERY;
        types[2] = Type::NOTIFICATIONS;
        types[3] = Type::ATTENTION;
        cb(types);
        return Void();
    }

    Return<void> debug(const hidl_handle& handle, const hidl_vec<hidl_string>&) override {
        if (handle == nullptr || handle->numFds < 1 || handle->data[0] < 0) {
            return Void();
        }

        const int fd = handle->data[0];
        const int current = readInt(kBacklightPath, -1);
        const int max = readInt(kBacklightMaxPath, -1);
        dprintf(fd, "enjoy10e light HAL\nbacklight=%d max=%d\n", current, max);
        fsync(fd);
        return Void();
    }
};

}  // namespace

int main() {
    configureRpcThreadpool(1, true);

    sp<ILight> service = new Enjoy10eLight();
    const android::status_t status = service->registerAsService();
    if (status != OK) {
        ALOGE("Failed to register android.hardware.light@2.0::ILight/default: %d", status);
        return 1;
    }

    ALOGI("Enjoy10e OSS light HAL is running");
    joinRpcThreadpool();
    return 1;
}
