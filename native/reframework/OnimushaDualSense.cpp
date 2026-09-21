#include "reframework_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#if defined(_MSC_VER)
#define REF_EXPORT extern "C" __declspec(dllexport)
#else
#define REF_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
constexpr unsigned short kHidRelayPort = 28766;
#if defined(_WIN32)
struct NativeWave { std::vector<float> samples; };

struct NativeRoute {
    std::string id, family, source;
    std::vector<std::string> variants, left, right;
};

class NativeCatalog {
    std::wstring data_dir;
    std::unordered_map<std::string, std::string> files;
    std::unordered_map<std::uint32_t, NativeRoute> routes;
    std::unordered_map<std::string, std::shared_ptr<NativeWave>> waves;

    static std::string read_string(std::ifstream& input)
    {
        std::uint32_t length = 0; unsigned shift = 0; std::uint8_t byte = 0;
        do {
            if (!input.read(reinterpret_cast<char*>(&byte), 1) || shift > 28) return {};
            length |= static_cast<std::uint32_t>(byte & 0x7f) << shift; shift += 7;
        } while ((byte & 0x80) != 0);
        if (length > 1u << 20) return {};
        std::string value(length, '\0');
        return input.read(value.data(), length) ? value : std::string{};
    }
    static std::vector<std::string> read_variants(std::ifstream& input)
    {
        std::int32_t count = 0;
        if (!input.read(reinterpret_cast<char*>(&count), 4) || count < 0 || count > 4096) return {};
        std::vector<std::string> result; result.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) result.push_back(read_string(input));
        return result;
    }
    std::shared_ptr<NativeWave> load_wave(const std::string& id)
    {
        auto existing = waves.find(id); if (existing != waves.end()) return existing->second;
        auto file = files.find(id); if (file == files.end()) return {};
        std::wstring path = data_dir + L"\\waves\\" + std::wstring(file->second.begin(), file->second.end());
        std::ifstream input(path.c_str(), std::ios::binary); if (!input) return {};
        char header[56]{};
        if (!input.read(header, sizeof header) || std::memcmp(header, "RIFF", 4) != 0 ||
            std::memcmp(header + 8, "WAVEfmt ", 8) != 0 || std::memcmp(header + 36, "fact", 4) != 0 ||
            std::memcmp(header + 48, "data", 4) != 0) return {};
        std::uint32_t bytes = 0;
        std::memcpy(&bytes, header + 52, sizeof bytes);
        if (bytes == 0 || bytes > 48000u * 2u * 10u || (bytes % 4) != 0) return {};
        auto wave = std::make_shared<NativeWave>();
        wave->samples.resize(bytes / 4);
        input.seekg(56, std::ios::beg);
        if (!input.read(reinterpret_cast<char*>(wave->samples.data()), bytes)) return {};
        waves.emplace(id, wave); return wave;
    }
public:
    bool load()
    {
        HMODULE self = GetModuleHandleW(L"OnimushaDualSense.dll"); if (!self) return false;
        wchar_t module[MAX_PATH]{};
        if (!GetModuleFileNameW(self, module, MAX_PATH)) return false;
        std::wstring path(module); auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return false;
        path.resize(slash); slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return false;
        data_dir = path.substr(0, slash) + L"\\data";
        // ONDS v1: magic[4], version u32, then file_count and
        // (id, wave_filename, source_length) records; route_count and
        // (event_id, id, family, source, variants, left, right) records;
        // finally catalog_count and (id, variants) records. Strings use
        // unsigned-LEB128 byte lengths; variant lists use i32 counts.
        std::ifstream input((data_dir + L"\\onimusha_dualsense_native.bin").c_str(), std::ios::binary);
        char magic[4]{}; std::int32_t version = 0, count = 0;
        if (!input.read(magic, 4) || std::memcmp(magic, "ONDS", 4) != 0 ||
            !input.read(reinterpret_cast<char*>(&version), 4) || version != 1 ||
            !input.read(reinterpret_cast<char*>(&count), 4) || count < 0 || count > 100000) return false;
        for (int i = 0; i < count; ++i) {
            std::string id = read_string(input), file = read_string(input); std::int32_t length = 0;
            if (id.empty() || file.empty() || !input.read(reinterpret_cast<char*>(&length), 4) || length < 0) return false;
            files.emplace(std::move(id), std::move(file));
        }
        if (!input.read(reinterpret_cast<char*>(&count), 4) || count < 0 || count > 100000) return false;
        for (int i = 0; i < count; ++i) {
            std::uint32_t event = 0; if (!input.read(reinterpret_cast<char*>(&event), 4)) return false;
            NativeRoute route{read_string(input), read_string(input), read_string(input),
                read_variants(input), read_variants(input), read_variants(input)};
            if (route.id.empty() || !input) return false;
            routes.emplace(event, std::move(route));
        }
        if (!input.read(reinterpret_cast<char*>(&count), 4) || count < 0 || count > 100000) return false;
        for (int i = 0; i < count; ++i) { if (read_string(input).empty()) return false; read_variants(input); }
        return static_cast<bool>(input);
    }
    const NativeRoute* route(std::uint32_t event) const
    {
        auto found = routes.find(event); return found == routes.end() ? nullptr : &found->second;
    }
    std::shared_ptr<NativeWave> wave(const std::string& id) { return load_wave(id); }
    std::string sample_for(const NativeRoute& route, bool right) const
    {
        const auto& values = route.family == "footsteps" ? (right ? route.right : route.left) : route.variants;
        return values.empty() ? std::string{} : values.front();
    }
    std::string sample_for(const std::string& id) const
    {
        auto found = files.find(id); return found == files.end() ? std::string{} : id;
    }
};

struct WaveLevels { std::uint8_t low = 0, high = 0; };

class WaveMixer {
    struct Voice { std::shared_ptr<NativeWave> wave; std::size_t frame = 0; float level = 1; };
    std::vector<Voice> voices;
    std::mutex gate;
public:
    bool play(const std::shared_ptr<NativeWave>& wave, float level = 1)
    {
        if (!wave || wave->samples.size() < 2) return false;
        std::lock_guard lock(gate);
        if (voices.size() >= 16) voices.erase(voices.begin());
        voices.push_back({wave, 0, level});
        return true;
    }
    void fill(float* output, std::size_t frames)
    {
        std::fill(output, output + frames * 4, 0.0f);
        std::lock_guard lock(gate);
        for (std::size_t i = voices.size(); i-- > 0;) {
            auto& voice = voices[i];
            const std::size_t available = voice.wave->samples.size() / 2 - voice.frame;
            const std::size_t count = std::min(frames, available);
            for (std::size_t frame = 0; frame < count; ++frame) {
                output[frame * 4 + 2] += voice.wave->samples[(voice.frame + frame) * 2] * voice.level;
                output[frame * 4 + 3] += voice.wave->samples[(voice.frame + frame) * 2 + 1] * voice.level;
            }
            voice.frame += count;
            if (voice.frame >= voice.wave->samples.size() / 2)
                voices.erase(voices.begin() + static_cast<std::ptrdiff_t>(i));
        }
        for (std::size_t i = 0; i < frames * 4; ++i)
            output[i] = std::clamp(output[i], -0.85f, 0.85f);
    }
    WaveLevels tick()
    {
        std::lock_guard lock(gate);
        double low = 0, high = 0; constexpr std::size_t window = 800;
        for (std::size_t i = voices.size(); i-- > 0;) {
            auto& voice = voices[i]; std::size_t end = std::min(voice.frame + window, voice.wave->samples.size() / 2);
            double left = 0, right = 0; std::size_t count = end - voice.frame;
            for (std::size_t frame = voice.frame; frame < end; ++frame) {
                left += std::abs(voice.wave->samples[frame * 2]); right += std::abs(voice.wave->samples[frame * 2 + 1]);
            }
            if (count) { low += left / count * voice.level; high += right / count * voice.level; }
            voice.frame = end; if (voice.frame >= voice.wave->samples.size() / 2) voices.erase(voices.begin() + static_cast<std::ptrdiff_t>(i));
        }
        return {static_cast<std::uint8_t>(std::clamp(low * 700.0, 0.0, 255.0)),
            static_cast<std::uint8_t>(std::clamp(high * 700.0, 0.0, 255.0))};
    }
};
extern WaveMixer wave_mixer;
#endif

const REFrameworkPluginFunctions* functions = nullptr;
const REFrameworkSDKData* sdk_data = nullptr;
std::uint64_t presents = 0;
bool hooks_installed = false;
WaveMixer wave_mixer;

class PortAudioOutput {
    using Stream = void;
    struct DeviceInfo {
        int version; const char* name; int host_api; int inputs; int outputs;
        double low_input, low_output, high_input, high_output, sample_rate;
    };
    struct HostInfo { int version, type, device_count, default_input, default_output; const char* name; };
    struct Parameters { int device, channels; unsigned long format; double latency; void* specific; };
    struct TimeInfo { double input, current, output; };
    using Callback = int (*)(const void*, void*, unsigned long, const TimeInfo*, unsigned long, void*);
    using Initialize = int (*)();
    using Terminate = int (*)();
    using GetDeviceCount = int (*)();
    using GetDeviceInfo = const DeviceInfo* (*)(int);
    using GetHostInfo = const HostInfo* (*)(int);
    using OpenStream = int (*)(Stream**, const Parameters*, const Parameters*, double, unsigned long, unsigned long, Callback, void*);
    using StartStream = int (*)(Stream*);
    using AbortStream = int (*)(Stream*);
    using CloseStream = int (*)(Stream*);
    using ErrorText = const char* (*)(int);

    HMODULE module = nullptr;
    Stream* stream = nullptr;
    Initialize initialize = nullptr;
    Terminate terminate = nullptr;
    GetDeviceCount get_device_count = nullptr;
    GetDeviceInfo get_device_info = nullptr;
    GetHostInfo get_host_info = nullptr;
    OpenStream open_stream = nullptr;
    StartStream start_stream = nullptr;
    AbortStream abort_stream = nullptr;
    CloseStream close_stream = nullptr;
    ErrorText error_text = nullptr;

    static int callback(const void*, void* output, unsigned long frames, const TimeInfo*, unsigned long, void*)
    {
        if (output != nullptr) wave_mixer.fill(static_cast<float*>(output), frames);
        return 0;
    }
    template <typename T> T symbol(const char* name) { return reinterpret_cast<T>(GetProcAddress(module, name)); }
    std::wstring plugin_directory()
    {
        HMODULE self = GetModuleHandleW(L"OnimushaDualSense.dll"); if (!self) return {};
        wchar_t path[MAX_PATH]{}; if (!GetModuleFileNameW(self, path, MAX_PATH)) return {};
        std::wstring result(path); auto slash = result.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring{} : result.substr(0, slash);
    }
    const char* error_message(int code) const { return error_text ? error_text(code) : "PortAudio error"; }
public:
    ~PortAudioOutput()
    {
        if (stream != nullptr) { abort_stream(stream); close_stream(stream); }
        if (terminate != nullptr) terminate();
        if (module != nullptr) FreeLibrary(module);
    }
    bool ready() const { return stream != nullptr; }
    bool open(std::string& error)
    {
        if (stream != nullptr) return true;
        auto directory = plugin_directory();
        if (!directory.empty()) module = LoadLibraryW((directory + L"\\libportaudio64bit.dll").c_str());
        if (module == nullptr) module = LoadLibraryW(L"libportaudio64bit.dll");
        if (module == nullptr) { error = "libportaudio64bit.dll is unavailable"; return false; }
        initialize = symbol<Initialize>("Pa_Initialize");
        terminate = symbol<Terminate>("Pa_Terminate");
        get_device_count = symbol<GetDeviceCount>("Pa_GetDeviceCount");
        get_device_info = symbol<GetDeviceInfo>("Pa_GetDeviceInfo");
        get_host_info = symbol<GetHostInfo>("Pa_GetHostApiInfo");
        open_stream = symbol<OpenStream>("Pa_OpenStream");
        start_stream = symbol<StartStream>("Pa_StartStream");
        abort_stream = symbol<AbortStream>("Pa_AbortStream");
        close_stream = symbol<CloseStream>("Pa_CloseStream");
        error_text = symbol<ErrorText>("Pa_GetErrorText");
        if (!initialize || !terminate || !get_device_count || !get_device_info || !get_host_info ||
            !open_stream || !start_stream || !abort_stream || !close_stream) {
            error = "PortAudio exports are incomplete"; return false;
        }
        int result = initialize();
        if (result < 0) { error = error_message(result); return false; }
        const int count = get_device_count();
        int selected = -1;
        const DeviceInfo* selected_info = nullptr;
        for (int index = 0; index < count; ++index) {
            const auto* info = get_device_info(index);
            const auto* host = info ? get_host_info(info->host_api) : nullptr;
            if (!info || !host || host->type != 13 || info->outputs < 4 || !info->name) continue;
            std::string name(info->name);
            if (name.find("DualSense") == std::string::npos && name.find("Wireless Controller") == std::string::npos) continue;
            selected = index; selected_info = info; break;
        }
        if (selected < 0) { error = "no four-channel DualSense WASAPI output"; return false; }
        Parameters parameters{selected, 4, 1, selected_info->low_output, nullptr};
        result = open_stream(&stream, nullptr, &parameters, 48000, 256, 0, callback, nullptr);
        if (result < 0) { stream = nullptr; error = error_message(result); return false; }
        result = start_stream(stream);
        if (result < 0) { close_stream(stream); stream = nullptr; error = error_message(result); return false; }
        functions->log_info("OnimushaDualSense DualSense PCM haptic output ready: %s", selected_info->name);
        return true;
    }
};
PortAudioOutput audio_output;

enum class Feedback : std::uint8_t {
    FootLeft, FootRight, RunLeft, RunRight, Attack, Hit, Damage, Guard, Dodge, PerfectDodge, Land, Heal,
    Soul, Pickup, LockOn, Power, Finisher, UiSelect, UiDecide, UiCancel, BowStart
};

std::mutex event_gate;
std::vector<Feedback> pending_events;
int base_trigger_profile = -1;
int trigger_override = -1;
std::uint64_t trigger_override_until = 0;
REFrameworkManagedObjectHandle player_entity = nullptr;
REFrameworkManagedObjectHandle player_character = nullptr;
std::uint64_t previous_action_state = 0;
bool have_previous_action_state = false;
bool sound_side = false;

void queue_feedback(Feedback event)
{
    std::lock_guard lock(event_gate);
    if (pending_events.size() < 128) pending_events.push_back(event);
}

void set_trigger_profile(int profile)
{
    std::lock_guard lock(event_gate);
    base_trigger_profile = profile;
}

void set_trigger_override(int profile, std::uint64_t until)
{
    std::lock_guard lock(event_gate);
    trigger_override = profile;
    trigger_override_until = until;
}

std::vector<Feedback> take_feedback()
{
    std::lock_guard lock(event_gate);
    std::vector<Feedback> result;
    result.swap(pending_events);
    return result;
}

int current_trigger_profile()
{
    std::lock_guard lock(event_gate);
#if defined(_WIN32)
    if (trigger_override >= 0 && GetTickCount64() < trigger_override_until) return trigger_override;
    if (trigger_override >= 0) trigger_override = -1;
#endif
    return base_trigger_profile;
}

bool same_object(void* left, void* right)
{
    return left != nullptr && left == right;
}

REFrameworkManagedObjectHandle invoke_object(void* object, const char* method_name)
{
    if (object == nullptr || sdk_data == nullptr || sdk_data->managed_object == nullptr ||
        sdk_data->method == nullptr || sdk_data->functions == nullptr || sdk_data->tdb == nullptr) return nullptr;
    auto type = sdk_data->managed_object->get_type_definition(
        reinterpret_cast<REFrameworkManagedObjectHandle>(object));
    if (type == nullptr) return nullptr;
    auto method = sdk_data->type_definition->find_method(type, method_name);
    if (method == nullptr) return nullptr;
    REFrameworkInvokeRet result{};
    if (sdk_data->method->invoke(method, object, nullptr, 0, &result, sizeof result) != 0 ||
        result.exception_thrown) return nullptr;
    REFrameworkManagedObjectHandle value = nullptr;
    std::memcpy(&value, result.bytes, sizeof value);
    return value;
}
bool invoke_void(void* object, const char* method_name)
{
    if (object == nullptr || sdk_data == nullptr || sdk_data->managed_object == nullptr ||
        sdk_data->method == nullptr || sdk_data->functions == nullptr || sdk_data->tdb == nullptr) return false;
    auto type = sdk_data->managed_object->get_type_definition(
        reinterpret_cast<REFrameworkManagedObjectHandle>(object));
    if (type == nullptr) return false;
    auto method = sdk_data->type_definition->find_method(type, method_name);
    if (method == nullptr) return false;
    REFrameworkInvokeRet result{};
    return sdk_data->method->invoke(method, object, nullptr, 0, &result, sizeof result) == 0 &&
        !result.exception_thrown;
}


std::uint64_t invoke_integer(void* object, const char* method_name, bool& ok)
{
    ok = false;
    if (object == nullptr || sdk_data == nullptr || sdk_data->managed_object == nullptr ||
        sdk_data->method == nullptr || sdk_data->functions == nullptr || sdk_data->tdb == nullptr) return 0;
    auto type = sdk_data->managed_object->get_type_definition(
        reinterpret_cast<REFrameworkManagedObjectHandle>(object));
    if (type == nullptr) return 0;
    auto method = sdk_data->type_definition->find_method(type, method_name);
    if (method == nullptr) return 0;
    REFrameworkInvokeRet result{};
    if (sdk_data->method->invoke(method, object, nullptr, 0, &result, sizeof result) != 0 ||
        result.exception_thrown) return 0;
    std::uint64_t value = 0;
    std::memcpy(&value, result.bytes, sizeof value);

    ok = true;
    return value;
}
std::string invoke_string(void* object, const char* getter)
{
    auto value = invoke_object(object, getter); if (value == nullptr) return {};
    bool ok = false; auto length = invoke_integer(value, "get_Length", ok);
    if (!ok || length > 256) return {};
    auto type = sdk_data->managed_object->get_type_definition(value);
    auto method = sdk_data->type_definition->find_method(type, "get_Chars");
    if (method == nullptr) return {};
    std::string result; result.reserve(static_cast<std::size_t>(length));
    for (std::uint64_t index = 0; index < length; ++index) {
        int argument = static_cast<int>(index); void* args[] = {&argument}; REFrameworkInvokeRet ret{};
        if (sdk_data->method->invoke(method, value, args, 1, &ret, sizeof ret) != 0 || ret.exception_thrown) return {};
        std::uint16_t character = 0; std::memcpy(&character, ret.bytes, sizeof character);
        if (character > 0x7f) return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
}

void refresh_player()
{
    player_entity = nullptr;
    player_character = nullptr;
    if (sdk_data == nullptr || sdk_data->functions == nullptr ||
        sdk_data->functions->get_managed_singleton == nullptr) return;
    auto manager = sdk_data->functions->get_managed_singleton("app.PlayerManager");
    auto info = invoke_object(manager, "getControllingPlayer");
    player_entity = invoke_object(info, "get_CharacterEntity");
    player_character = invoke_object(info, "get_Character");
}

bool instance_is_player(void* instance, bool entity)
{
    return same_object(instance, entity ? player_entity : player_character);
}

bool module_is_player(void* instance)
{
    auto owner = invoke_object(instance, "get_OwnerCharacter");
    return same_object(owner, player_character);
}

#if defined(_WIN32)
struct Pulse {
    std::uint8_t low;
    std::uint8_t high;
    DWORD milliseconds;
};

Pulse pulse_for(Feedback event)
{
    switch (event) {
    case Feedback::FootLeft: return {40, 18, 35};
    case Feedback::FootRight: return {40, 18, 35};
    case Feedback::RunLeft: return {58, 28, 45};
    case Feedback::RunRight: return {58, 28, 45};
    case Feedback::Attack: return {70, 24, 65};
    case Feedback::Hit: return {180, 90, 90};
    case Feedback::Damage: return {155, 70, 130};
    case Feedback::Guard: return {130, 50, 70};
    case Feedback::Dodge: return {80, 30, 100};
    case Feedback::PerfectDodge: return {180, 80, 150};
    case Feedback::Land: return {105, 40, 110};
    case Feedback::Heal: return {75, 45, 220};
    case Feedback::Soul: return {90, 55, 120};
    case Feedback::Pickup: return {65, 35, 110};
    case Feedback::LockOn: return {45, 20, 45};
    case Feedback::Power: return {145, 65, 260};
    case Feedback::Finisher: return {190, 100, 220};
    case Feedback::UiSelect: return {38, 18, 25};
    case Feedback::UiDecide: return {75, 35, 65};
    case Feedback::UiCancel: return {55, 25, 50};
    case Feedback::BowStart: return {25, 12, 35};
    }
    return {0, 0, 0};
}

void trigger_payload(std::array<std::uint8_t, 64>& report, std::size_t offset, int profile)
{
    report[offset] = 5;
    if (profile < 0) return;
    std::array<float, 10> powers{};
    // Preserve the original PR trigger modes: profile 0 is resistance;
    // profiles 1-3 are positional trigger vibration.
    std::uint8_t mode = 0x21;
    std::uint8_t frequency = 0;
    if (profile == 1) { mode = 0x26; frequency = 32; powers.fill(.9f); }
    if (profile == 2) { mode = 0x26; frequency = 16; powers.fill(.12f); }
    if (profile == 3) { mode = 0x26; frequency = 34; powers = {.5f, .6f, .7f, .8f, .8f, .8f, .7f, .6f, .5f, .45f}; }
    std::uint16_t mask = 0;
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < powers.size(); ++i) {
        auto strength = static_cast<unsigned>(std::min(8.0f, powers[i] * 8.0f + .5f));
        if (strength > 0) {
            mask |= static_cast<std::uint16_t>(1u << i);
            packed |= static_cast<std::uint32_t>(strength - 1) << (i * 3);
        }
    }
    report[offset] = mode;
    std::memcpy(report.data() + offset + 1, &mask, sizeof mask);
    std::memcpy(report.data() + offset + 3, &packed, sizeof packed);
    report[offset + 9] = frequency;
}
std::array<std::uint8_t, 64> make_report(std::uint8_t low, std::uint8_t high, int profile)
{
    std::array<std::uint8_t, 64> report{};
    report[0] = 2; report[1] = 0x0c; report[3] = low; report[4] = high;
    trigger_payload(report, 11, profile == 0 ? profile : -1);
    trigger_payload(report, 22, profile == 1 || profile == 3 ? profile : -1);
    return report;
}

class DualSenseOutput {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE write_event = nullptr;
    OVERLAPPED write_ol{};
    DWORD report_length = 0;
    ULONGLONG pulse_until = 0;
    int trigger_profile = -2;
    std::mutex gate;

    bool write(const std::array<std::uint8_t, 64>& report, std::string& error)
    {
        DWORD written = 0;
        ResetEvent(write_event);
        BOOL ok = WriteFile(handle, report.data(), report_length, &written, &write_ol);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) {
                error = "DualSense HID write failed (" + std::to_string(GetLastError()) + ")";
                return false;
            }
            if (WaitForSingleObject(write_event, 1000) != WAIT_OBJECT_0 ||
                !GetOverlappedResult(handle, &write_ol, &written, FALSE)) {
                error = "DualSense HID write completion failed (" + std::to_string(GetLastError()) + ")";
                return false;
            }
        }
        if (written != report_length) {
            error = "DualSense HID short write (" + std::to_string(written) + "/" +
                std::to_string(report_length) + ")";
            return false;
        }
        return true;
    }
    bool report_now(std::uint8_t low, std::uint8_t high, int profile, std::string& error)
    {
        auto report = make_report(low, high, profile);
        return write(report, error);
    }

public:
    ~DualSenseOutput()
    {
        if (handle != INVALID_HANDLE_VALUE) {
            std::string ignored;
            std::lock_guard lock(gate);
            report_now(0, 0, -1, ignored);
        }
        if (write_event) CloseHandle(write_event);
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }

    bool open(std::string& error)
    {
        std::lock_guard lock(gate);
        if (handle != INVALID_HANDLE_VALUE) return true;
        GUID guid{};
        HidD_GetHidGuid(&guid);
        HDEVINFO devices = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE) { error = "SetupDiGetClassDevs failed"; return false; }
        bool found = false; unsigned int matching_devices = 0; DWORD last_error = ERROR_SUCCESS;
        for (DWORD index = 0; ; ++index) {
            SP_DEVICE_INTERFACE_DATA interface_data{sizeof interface_data};
            if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &guid, index, &interface_data)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
                continue;
            }
            DWORD required = 0;
            SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, nullptr, 0, &required, nullptr);
            if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
            std::vector<std::uint8_t> detail(required);
            auto* path = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail.data());
            path->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, path, required, nullptr, nullptr)) continue;
            HANDLE probe = CreateFileW(path->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (probe == INVALID_HANDLE_VALUE) { last_error = GetLastError(); continue; }
            HIDD_ATTRIBUTES attributes{sizeof attributes};
            bool matching = HidD_GetAttributes(probe, &attributes) && attributes.VendorID == 0x054c &&
                (attributes.ProductID == 0x0ce6 || attributes.ProductID == 0x0df2);
            if (!matching) { CloseHandle(probe); continue; }
            ++matching_devices;
            PHIDP_PREPARSED_DATA preparsed = nullptr; HIDP_CAPS caps{};
            bool caps_ok = HidD_GetPreparsedData(probe, &preparsed) && HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
            if (preparsed) HidD_FreePreparsedData(preparsed);
            CloseHandle(probe);
            if (!caps_ok || caps.OutputReportByteLength < 48 || caps.OutputReportByteLength > 64) continue;
            HANDLE candidate = CreateFileW(path->DevicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (candidate == INVALID_HANDLE_VALUE) { last_error = GetLastError(); continue; }
            HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event) { last_error = GetLastError(); CloseHandle(candidate); continue; }
            handle = candidate; write_event = event; write_ol.hEvent = event;
            report_length = caps.OutputReportByteLength; found = true; break;
        }
        SetupDiDestroyDeviceInfoList(devices);
        if (!found) error = "no writable DualSense output collection (matching=" +
            std::to_string(matching_devices) + ", last_error=" + std::to_string(last_error) + ")";
        return found;
    }

    bool release(std::string& error)
    {
        std::lock_guard lock(gate);
        if (handle == INVALID_HANDLE_VALUE) { error = "DualSense output is not open"; return false; }
        pulse_until = 0; trigger_profile = -2; return report_now(0, 0, -1, error);
    }


    bool play(Pulse pulse, int profile, std::string& error)
    {
        std::lock_guard lock(gate);
        if (handle == INVALID_HANDLE_VALUE) { error = "DualSense output is not open"; return false; }
        trigger_profile = profile; pulse_until = GetTickCount64() + pulse.milliseconds;
        return report_now(pulse.low, pulse.high, profile, error);
    }
    bool play_levels(WaveLevels levels, int profile, std::string& error)
    {
        std::lock_guard lock(gate);
        if (handle == INVALID_HANDLE_VALUE) { error = "DualSense output is not open"; return false; }
        trigger_profile = profile; pulse_until = 0; return report_now(levels.low, levels.high, profile, error);
    }


    void tick(int profile)
    {
        std::lock_guard lock(gate);
        if (handle == INVALID_HANDLE_VALUE) return;
        if (profile != trigger_profile) {
            std::string ignored; trigger_profile = profile; report_now(0, 0, profile, ignored);
        }
        if (pulse_until != 0 && GetTickCount64() >= pulse_until) {
            std::string ignored; report_now(0, 0, profile, ignored); pulse_until = 0;
        }
    }
};

class HidRelayOutput {
    SOCKET connection = INVALID_SOCKET;
    ULONGLONG pulse_until = 0;
    int trigger_profile = -2;
    std::mutex gate;

    bool transmit(const std::array<std::uint8_t, 64>& report, std::string& error)
    {
        for (int offset = 0; offset < 48;) {
            const int written = send(connection, reinterpret_cast<const char*>(report.data()) + offset,
                48 - offset, 0);
            if (written <= 0) {
                error = "HID relay write failed";
                closesocket(connection); connection = INVALID_SOCKET;
                return false;
            }
            offset += written;
        }
        return true;
    }

public:
    ~HidRelayOutput()
    {
        std::lock_guard lock(gate);
        if (connection != INVALID_SOCKET) closesocket(connection);
    }
    bool open(std::string& error)
    {
        std::lock_guard lock(gate);
        if (connection != INVALID_SOCKET) return true;
        static bool winsock_ready = false;
        if (!winsock_ready) {
            WSADATA data{};
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                error = "Winsock initialization failed"; return false;
            }
            winsock_ready = true;
        }
        connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connection == INVALID_SOCKET) { error = "HID relay socket failed"; return false; }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kHidRelayPort);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
            error = "HID relay unavailable";
            closesocket(connection); connection = INVALID_SOCKET; return false;
        }
        functions->log_info("OnimushaDualSense HID relay output ready");
        return true;
    }
    bool release(std::string& error)
    {
        std::lock_guard lock(gate);
        if (connection == INVALID_SOCKET) { error = "HID relay output is not open"; return false; }
        auto report = make_report(0, 0, -1);
        const bool ok = transmit(report, error);
        pulse_until = 0; trigger_profile = -2; return ok;
    }
    bool play(Pulse pulse, int profile, std::string& error)
    {
        std::lock_guard lock(gate);
        if (connection == INVALID_SOCKET) { error = "HID relay output is not open"; return false; }
        auto report = make_report(pulse.low, pulse.high, profile);
        if (!transmit(report, error)) return false;
        trigger_profile = profile; pulse_until = GetTickCount64() + pulse.milliseconds; return true;
    }
    bool play_levels(WaveLevels levels, int profile, std::string& error)
    {
        std::lock_guard lock(gate);
        if (connection == INVALID_SOCKET) { error = "HID relay output is not open"; return false; }
        auto report = make_report(levels.low, levels.high, profile);
        if (!transmit(report, error)) return false;
        trigger_profile = profile; pulse_until = 0; return true;
    }
    void tick(int profile)
    {
        std::lock_guard lock(gate);
        if (connection == INVALID_SOCKET) return;
        const auto now = GetTickCount64();
        if (profile != trigger_profile) {
            std::string ignored;
            auto report = make_report(0, 0, profile);
            transmit(report, ignored); trigger_profile = profile;
        }
        if (pulse_until != 0 && now >= pulse_until) {
            std::string ignored;
            auto report = make_report(0, 0, profile);
            transmit(report, ignored); pulse_until = 0;
        }
    }
};


DualSenseOutput output;
HidRelayOutput hid_relay_output;
NativeCatalog native_catalog;
bool native_catalog_loaded = false;
#endif

const char* feedback_name(Feedback event)
{
    switch (event) {
    case Feedback::FootLeft: return "foot_left";
    case Feedback::FootRight: return "foot_right";
    case Feedback::RunLeft: return "run_left";
    case Feedback::RunRight: return "run_right";
    case Feedback::Attack: return "attack";
    case Feedback::Hit: return "hit";
    case Feedback::Damage: return "damage";
    case Feedback::Guard: return "guard";
    case Feedback::Dodge: return "dodge";
    case Feedback::PerfectDodge: return "perfect_dodge";
    case Feedback::Land: return "land";
    case Feedback::Heal: return "heal";
    case Feedback::Soul: return "soul";
    case Feedback::Pickup: return "pickup";
    case Feedback::LockOn: return "lock_on";
    case Feedback::Power: return "power";
    case Feedback::Finisher: return "finisher";
    case Feedback::UiSelect: return "ui_select";
    case Feedback::UiDecide: return "ui_decide";
    case Feedback::UiCancel: return "ui_cancel";
    case Feedback::BowStart: return "bow_start";
    }
    return "unknown";
}

#if defined(_WIN32)
bool source_matches(const NativeRoute& route, void* info)
{
    auto source = invoke_object(info, "get_SrcGameObj");
    if (source == nullptr) return false;
    std::string name = invoke_string(source, "get_Name");
    if (route.source == "GUI") return name == "GUI";
    if (route.source == "player") return name == "Player_00";
    if (route.source == "player_effect") return name == "effect_Player_00";
    if (route.source == "TrgPos") return name == "TrgPos";
    if (route.source == "parry_pos") return name == "parry_pos";
    return false;
}

int sound_request(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (!native_catalog_loaded || argc < 2 || argv[1] == nullptr) return 0;
    bool ok = false; auto event = invoke_integer(argv[1], "get_EventId", ok);
    if (!ok) return 0;
    auto route = native_catalog.route(static_cast<std::uint32_t>(event));
    if (route == nullptr || !source_matches(*route, argv[1])) return 0;
    const bool right = route->family == "footsteps" ? (sound_side = !sound_side) : false;
    auto sample = native_catalog.sample_for(*route, right);
    if (!sample.empty()) wave_mixer.play(native_catalog.wave(sample));
    return 0;
}
#endif

int bow_start(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    set_trigger_profile(0);
    queue_feedback(Feedback::BowStart);
    return 0;
}

int footstep(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (argc < 4) return 0;
    auto step = reinterpret_cast<std::uintptr_t>(argv[2]);
    auto foot = reinterpret_cast<std::intptr_t>(argv[3]);
    if ((step != 0 && step != 2) || foot < 0 || foot > 5) return 0;
    bool ok = false;
    auto move = invoke_integer(player_entity, "getCurrentActionMoveType", ok);
    bool left = (foot % 2) == 0;
    if (ok && move != 0) queue_feedback(left ? Feedback::RunLeft : Feedback::RunRight);
    else queue_feedback(left ? Feedback::FootLeft : Feedback::FootRight);
    return 0;
}

int action_enter(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (argc > 2 && instance_is_player(argv[1], false)) {
        bool ok = false;
        auto bits = invoke_integer(argv[2], "get_ActStateBit", ok);
        if (ok && (bits & 536870912u) != 0) queue_feedback(Feedback::Dodge);
    }
    return 0;
}

int attack_collision(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (!instance_is_player(argc > 1 ? argv[1] : nullptr, true) || argc < 3) return 0;
    bool ok = false;
    auto active = invoke_integer(argv[2], "get_IsOn", ok);
    if (ok && active != 0) queue_feedback(Feedback::Attack);
    return 0;
}

int player_hit(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (instance_is_player(argc > 1 ? argv[1] : nullptr, true)) queue_feedback(Feedback::Hit);
    return 0;
}

int guard_contact(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (module_is_player(argc > 1 ? argv[1] : nullptr)) queue_feedback(Feedback::Guard);
    return 0;
}

int perfect_dodge(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (module_is_player(argc > 1 ? argv[1] : nullptr)) queue_feedback(Feedback::PerfectDodge);
    return 0;
}

int health_change(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (!instance_is_player(argc > 1 ? argv[1] : nullptr, true) || argc < 4) return 0;
    auto kind = reinterpret_cast<std::uintptr_t>(argv[2]);
    auto amount = reinterpret_cast<std::intptr_t>(argv[3]);
    if ((kind == 0 || kind == 2) && amount > 0) queue_feedback(Feedback::Heal);
    if (kind == 1 && amount != 0) queue_feedback(Feedback::Damage);
    return 0;
}

int soul_absorption(int, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (module_is_player(argv[1])) {
        queue_feedback(Feedback::Soul);
#if defined(_WIN32)
        set_trigger_override(3, GetTickCount64() + 220);
#endif
    }
    return 0;
}

int pickup(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    queue_feedback(Feedback::Pickup);
    return 0;
}

int lock_on(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (argc > 2 && module_is_player(argv[1]) && argv[2] != nullptr) queue_feedback(Feedback::LockOn);
    return 0;
}

int power_change(int, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (instance_is_player(argv[1], true)) queue_feedback(Feedback::Power);
    return 0;
}

int finisher(int, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (instance_is_player(argv[1], true)) queue_feedback(Feedback::Finisher);
    return 0;
}

int adaptive_trigger(int argc, void** argv, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    if (argc < 3) return 0;
    auto value = reinterpret_cast<std::uintptr_t>(argv[2]);
    functions->log_info("OnimushaDualSense adaptive trigger callback argc=%d value=%llu",
        argc, static_cast<unsigned long long>(value));
    if (value == 0 || value == 1) set_trigger_profile(static_cast<int>(value));
    return 0;
}

int ui_select(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    queue_feedback(Feedback::UiSelect);
    return 0;
}

int ui_decide(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    queue_feedback(Feedback::UiDecide);
    return 0;
}

int ui_cancel(int, void**, REFrameworkTypeDefinitionHandle*, unsigned long long)
{
    queue_feedback(Feedback::UiCancel);
    return 0;
}

void install_hook(const char* type_name, const char* method_name, REFPreHookFn callback)
{
    if (sdk_data == nullptr || sdk_data->functions == nullptr || sdk_data->tdb == nullptr ||
        sdk_data->type_definition == nullptr) return;
    auto tdb = sdk_data->functions->get_tdb();
    auto type = sdk_data->tdb->find_type(tdb, type_name);
    if (type == nullptr) {
        functions->log_warn("OnimushaDualSense type not found: %s", type_name);
        return;
    }
    auto method = sdk_data->type_definition->find_method(type, method_name);
    if (method == nullptr) {
        functions->log_warn("OnimushaDualSense method not found: %s.%s", type_name, method_name);
        return;
    }
    auto hook_id = sdk_data->functions->add_hook(method, callback, nullptr, false);
    functions->log_info("OnimushaDualSense hook installed: %s.%s id=%u",
        type_name, method_name, hook_id);
    hooks_installed = true;
}

void install_hooks()
{
    if (hooks_installed) return;
    install_hook("app.cPlayerSubWeaponSupporter", "startStrongShot", bow_start);
    install_hook("app.cPlayerSubWeaponSupporter", "requestEffectStrongShot", bow_start);
    install_hook("app.EPVExpertFootLandingCustom", "play", footstep);
    install_hook("app.CharacterBase", "evBaseActionEnter", action_enter);
    install_hook("app.cPlayerCharacterEntity", "evAttackCollision", attack_collision);
    install_hook("app.cPlayerCharacterEntity", "onHitAttackPostProcess", player_hit);
    install_hook("app.cPlayerGuardController", "addDamage", guard_contact);
    install_hook("app.cPlayerJustDodgeSupporter", "executeSuccessJustDodgeAction", perfect_dodge);
    install_hook("app.cPlayerCharacterEntity", "onAddHealth", health_change);
    install_hook("app.cPlayerSoulAbsorptionSupporter", "executeSoulAbsorptionSuccess", soul_absorption);
    install_hook("app.SoundGetItemEventHandler", "onGetItem", pickup);
    install_hook("app.cPlayerLockOnSupporter", "requestLockOnCameraStart", lock_on);
    install_hook("app.cPlayerCharacterEntity", "evOniChangeStartEvent", power_change);
    install_hook("app.cPlayerCharacterEntity", "deadHeatActionImpactNotice", finisher);
    install_hook("app.cPlayerCharacterEntity", "attackBreakImpactNotice", finisher);
    install_hook("soundlib.SoundManager", "postRequestInfo", sound_request);
    install_hook("app.AdaptiveTriggerManager", "onAdaptiveTrigger", adaptive_trigger);
    const char* gui = "ace.GUIBase`2<app.GUIID.ID,app.UIKey.TYPE>";
    install_hook(gui, "triggerSoundSelectionChanged", ui_select);
    install_hook(gui, "triggerSoundScroll", ui_select);
    install_hook(gui, "triggerSoundScrollFlsBar", ui_select);
    install_hook(gui, "triggerSoundDecide", ui_decide);
    install_hook(gui, "triggerSoundDecideLong", ui_decide);
    install_hook(gui, "triggerSoundMouseDecide", ui_decide);
    install_hook(gui, "triggerSoundCancel", ui_cancel);
    install_hook(gui, "triggerSoundCancelLong", ui_cancel);
    if (hooks_installed) functions->log_info("OnimushaDualSense native feedback map ready");
}

void on_present()
{
    ++presents;
#if defined(_WIN32)
    if (presents == 1) {
        native_catalog_loaded = native_catalog.load();
        functions->log_info("OnimushaDualSense native waveform catalog %s",
            native_catalog_loaded ? "ready" : "absent");
        std::string audio_error;
        if (!audio_output.open(audio_error))
            functions->log_warn("OnimushaDualSense PCM haptic output unavailable: %s", audio_error.c_str());
    }
    if (presents == 1 || presents % 600 == 0) {
        refresh_player();
        std::string hid_error;
        if (output.open(hid_error)) {
            std::string error;
            if (presents == 1 && output.release(error))
                functions->log_info("OnimushaDualSense native HID output ready");
        } else {
            std::string relay_error;
            if (hid_relay_output.open(relay_error)) {
                std::string error;
                if (presents == 1 && hid_relay_output.release(error))
                    functions->log_info("OnimushaDualSense HID relay output ready");
            } else if (presents == 1) {
                functions->log_warn("OnimushaDualSense HID outputs unavailable: native=%s relay=%s",
                    hid_error.c_str(), relay_error.c_str());
            }
        }
    }
    if (presents % 60 == 0) refresh_player();
    if (auto manager = sdk_data->functions->get_managed_singleton("app.AdaptiveTriggerManager")) {
        invoke_void(manager, "checkOnAdaptiveTrigger");
    }
    bool state_ok = false;
    auto action_state = invoke_integer(player_character, "getActionState", state_ok);
    if (state_ok) {
        if (have_previous_action_state && (previous_action_state & 4) != 0 &&
            (action_state & 4) == 0 && (action_state & 1) != 0)
            queue_feedback(Feedback::Land);
        previous_action_state = action_state;
        have_previous_action_state = true;
    } else {
        have_previous_action_state = false;
    }
    const int profile = current_trigger_profile();
    for (auto event : take_feedback()) {
        std::string error;
        std::string sample = native_catalog_loaded ? native_catalog.sample_for("ext:" + std::string(feedback_name(event))) : "";
        if (audio_output.ready() && !sample.empty()) {
            if (wave_mixer.play(native_catalog.wave(sample))) continue;
            functions->log_warn("OnimushaDualSense waveform unavailable for event=%s; using pulse fallback",
                feedback_name(event));
        }
        auto pulse = pulse_for(event);
        bool sent = output.play(pulse, profile, error);
        if (!sent && hid_relay_output.open(error)) sent = hid_relay_output.play(pulse, profile, error);
        if (sent) functions->log_info("OnimushaDualSense native event=%s profile=%d",
            feedback_name(event), profile);
        else functions->log_warn("OnimushaDualSense event output unavailable: %s", error.c_str());
    }
    WaveLevels levels = !audio_output.ready() && native_catalog_loaded ? wave_mixer.tick() : WaveLevels{};
    if (levels.low != 0 || levels.high != 0) {
        std::string error;
        bool sent = output.play_levels(levels, profile, error);
        if (!sent && hid_relay_output.open(error)) sent = hid_relay_output.play_levels(levels, profile, error);
        if (!sent) functions->log_warn("OnimushaDualSense waveform output unavailable: %s", error.c_str());
    } else {
        output.tick(profile);
        hid_relay_output.tick(profile);
    }
#else
    if (presents == 1) functions->log_warn("OnimushaDualSense native HID is only implemented for Windows");
#endif
    if (presents == 30) install_hooks();
    if (presents % 600 == 0) {
        functions->log_info("OnimushaDualSense native plugin present=%llu hooks=%s",
            static_cast<unsigned long long>(presents), hooks_installed ? "ready" : "missing");
    }
}

}

REF_EXPORT void reframework_plugin_required_version(REFrameworkPluginVersion* version)
{
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

REF_EXPORT bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param)
{
    if (param == nullptr || param->functions == nullptr || param->functions->on_present == nullptr ||
        param->sdk == nullptr) return false;
    functions = param->functions;
    sdk_data = param->sdk;
    functions->log_info("OnimushaDualSense native plugin loaded; REFramework ABI %d.%d.%d",
        REFRAMEWORK_PLUGIN_VERSION_MAJOR, REFRAMEWORK_PLUGIN_VERSION_MINOR, REFRAMEWORK_PLUGIN_VERSION_PATCH);
    return functions->on_present(on_present);
}
