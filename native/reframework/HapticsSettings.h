#pragma once

#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <string>
#include <string_view>
#include <utility>

namespace onimusha {
enum class Feedback : std::uint8_t {
    FootLeft, FootRight, RunLeft, RunRight, Attack, Hit, Damage, Guard, Dodge, PerfectDodge, Land, Heal,
    Soul, Pickup, LockOn, Power, Finisher, UiSelect, UiDecide, UiCancel, BowStart, Count
};

enum class SoundFamily : std::size_t { Ui, Attack, Cut, Guard, Deflect, Parry, Other, Count };

struct HapticsSettings {
    bool enabled = true;
    float master_strength = 1;
    float footsteps = .5f;
    float running_footsteps = .5f;
    bool sound_haptics = true;
    std::array<float, static_cast<std::size_t>(SoundFamily::Count)> sound_strength{1, 1, 1, 1, 1, 1, 1};
    bool event_haptics = true;
    std::array<float, static_cast<std::size_t>(Feedback::Count)> event_strength{
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    bool adaptive_triggers = true;
    bool bow_enabled = true;
    bool rift_enabled = true;
    bool gauntlet_enabled = true;
    bool soul_enabled = true;
    float trigger_strength = 1;
    float rift_strength = 1;
    float soul_strength = 1;

    bool trigger_profile_enabled(int profile) const noexcept {
        switch (profile) {
        case 0: return bow_enabled;
        case 1: return rift_enabled;
        case 2: return gauntlet_enabled;
        case 3: return soul_enabled;
        default: return false;
        }
    }

    float event_gain(Feedback event) const noexcept {
        const float walking = event == Feedback::FootLeft || event == Feedback::FootRight ? footsteps :
            event == Feedback::RunLeft || event == Feedback::RunRight ? running_footsteps : 1.0f;
        return walking * event_strength[static_cast<std::size_t>(event)];
    }
    float trigger_gain(int profile) const noexcept {
        return master_strength * trigger_strength *
            (profile == 1 ? rift_strength : profile == 3 ? soul_strength : 1.0f);
    }
    float sound_gain(std::string_view family) const noexcept {
        if (family == "footsteps") return footsteps;
        SoundFamily key = SoundFamily::Other;
        if (family == "ui") key = SoundFamily::Ui;
        else if (family == "attack") key = SoundFamily::Attack;
        else if (family == "cut") key = SoundFamily::Cut;
        else if (family == "guard") key = SoundFamily::Guard;
        else if (family == "deflect") key = SoundFamily::Deflect;
        else if (family == "parry") key = SoundFamily::Parry;
        return sound_strength[static_cast<std::size_t>(key)];
    }
};

namespace detail {
inline std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
inline std::string lower(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
inline bool strength(const std::string& text, float& result) {
    errno = 0;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(value) || value < 0 || value > 1) return false;
    result = value;
    return true;
}
inline bool toggle(const std::string& text, bool& result) {
    const std::string value = lower(text);
    if (value == "true" || value == "yes" || value == "on" || value == "1") { result = true; return true; }
    if (value == "false" || value == "no" || value == "off" || value == "0") { result = false; return true; }
    return false;
}
}

inline HapticsSettings ReadHapticsSettings(std::istream& input) {
    HapticsSettings settings;
    constexpr std::pair<std::string_view, SoundFamily> sound_keys[] = {
        {"ui", SoundFamily::Ui}, {"attack", SoundFamily::Attack}, {"cut", SoundFamily::Cut},
        {"guard", SoundFamily::Guard}, {"deflect", SoundFamily::Deflect}, {"parry", SoundFamily::Parry},
        {"other", SoundFamily::Other}
    };
    constexpr std::pair<std::string_view, Feedback> event_keys[] = {
        {"footleft", Feedback::FootLeft}, {"footright", Feedback::FootRight},
        {"runleft", Feedback::RunLeft}, {"runright", Feedback::RunRight}, {"attack", Feedback::Attack},
        {"hit", Feedback::Hit}, {"damage", Feedback::Damage}, {"guard", Feedback::Guard},
        {"dodge", Feedback::Dodge}, {"perfectdodge", Feedback::PerfectDodge}, {"land", Feedback::Land},
        {"heal", Feedback::Heal}, {"soul", Feedback::Soul}, {"pickup", Feedback::Pickup},
        {"lockon", Feedback::LockOn}, {"power", Feedback::Power}, {"finisher", Feedback::Finisher},
        {"uiselect", Feedback::UiSelect}, {"uidecide", Feedback::UiDecide},
        {"uicancel", Feedback::UiCancel}, {"bowstart", Feedback::BowStart}
    };

    std::string line, section;
    bool first_line = true;
    while (std::getline(input, line)) {
        if (first_line) {
            first_line = false;
            if (line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        }
        line = detail::trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = detail::lower(detail::trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = detail::lower(detail::trim(line.substr(0, equals)));
        std::string value = detail::trim(line.substr(equals + 1));
        const auto comment = value.find_first_of(";#");
        if (comment != std::string::npos) value = detail::trim(value.substr(0, comment));
        if (section == "general") {
            if (key == "enabled") detail::toggle(value, settings.enabled);
            else if (key == "masterstrength") detail::strength(value, settings.master_strength);
            else if (key == "footsteps") detail::strength(value, settings.footsteps);
            else if (key == "runningfootsteps") detail::strength(value, settings.running_footsteps);
        } else if (section == "soundhaptics") {
            if (key == "enabled") detail::toggle(value, settings.sound_haptics);
            else for (const auto& [name, family] : sound_keys)
                if (key == name) detail::strength(value, settings.sound_strength[static_cast<std::size_t>(family)]);
        } else if (section == "eventhaptics") {
            if (key == "enabled") detail::toggle(value, settings.event_haptics);
            else for (const auto& [name, event] : event_keys)
                if (key == name) detail::strength(value, settings.event_strength[static_cast<std::size_t>(event)]);
        } else if (section == "adaptivetriggers") {
            if (key == "enabled") detail::toggle(value, settings.adaptive_triggers);
            else if (key == "bowenabled") detail::toggle(value, settings.bow_enabled);
            else if (key == "riftenabled") detail::toggle(value, settings.rift_enabled);
            else if (key == "gauntletenabled") detail::toggle(value, settings.gauntlet_enabled);
            else if (key == "soulenabled") detail::toggle(value, settings.soul_enabled);
            else if (key == "strength") detail::strength(value, settings.trigger_strength);
            else if (key == "riftstrength") detail::strength(value, settings.rift_strength);
            else if (key == "soulstrength") detail::strength(value, settings.soul_strength);
        }
    }
    return settings;
}
}
