#pragma once

#include <chrono>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Qt-free rule logic so it can be unit tested without a Qt build.
namespace sb::rules {

enum class Direction { Above, Below };
enum class Level { Info, Warning, Critical };
enum class Transition { None, Entered, Cleared };

// Hysteresis: a threshold is entered at `limit`, but only cleared after the
// value moves back past `limit` by `hysteresis`. This stops noisy readings
// from flapping between alert and recovery.
class ThresholdTracker {
public:
    constexpr ThresholdTracker(Direction dir, double limit, double hysteresis) noexcept
        : dir_(dir), limit_(limit), hysteresis_(hysteresis) {}

    Transition update(double value) noexcept {
        if (!active_) {
            if (breaches(value)) {
                active_ = true;
                return Transition::Entered;
            }
        } else if (recovers(value)) {
            active_ = false;
            return Transition::Cleared;
        }
        return Transition::None;
    }

    constexpr bool active() const noexcept { return active_; }

private:
    constexpr bool breaches(double v) const noexcept {
        return dir_ == Direction::Above ? v > limit_ : v < limit_;
    }
    constexpr bool recovers(double v) const noexcept {
        return dir_ == Direction::Above ? v < limit_ - hysteresis_ : v > limit_ + hysteresis_;
    }

    Direction dir_;
    double limit_;
    double hysteresis_;
    bool active_ = false;
};

struct Reading {
    std::string deviceId;
    std::string zone;
    std::string type;     // "temperature", "humidity", "co2", ...
    double value = 0.0;
};

struct ThresholdRule {
    std::string name;
    std::string sensorType;
    Direction direction = Direction::Above;
    double limit = 0.0;
    double hysteresis = 0.0;
    double criticalMargin = 4.0;   // beyond limit by this much -> Critical
};

struct AlertEvent {
    Level level = Level::Info;
    std::string rule;
    std::string deviceId;
    std::string zone;
    double value = 0.0;
    bool recovered = false;
};

class ThresholdEvaluator {
public:
    explicit ThresholdEvaluator(std::vector<ThresholdRule> rules) : rules_(std::move(rules)) {}

    std::vector<AlertEvent> evaluate(const Reading& r) {
        std::vector<AlertEvent> events;
        for (const auto& rule : rules_) {
            if (rule.sensorType != r.type) continue;

            const std::string key = r.deviceId + '|' + rule.name;
            auto it = trackers_.try_emplace(key, rule.direction, rule.limit, rule.hysteresis).first;

            switch (it->second.update(r.value)) {
            case Transition::Entered:
                events.push_back({levelFor(rule, r.value), rule.name, r.deviceId, r.zone, r.value, false});
                break;
            case Transition::Cleared:
                events.push_back({Level::Info, rule.name, r.deviceId, r.zone, r.value, true});
                break;
            case Transition::None:
                break;
            }
        }
        return events;
    }

private:
    static Level levelFor(const ThresholdRule& rule, double v) {
        const bool beyond = rule.direction == Direction::Above
                                ? v > rule.limit + rule.criticalMargin
                                : v < rule.limit - rule.criticalMargin;
        return beyond ? Level::Critical : Level::Warning;
    }

    std::vector<ThresholdRule> rules_;
    std::map<std::string, ThresholdTracker> trackers_;
};

// Automation: switch an actuator in the same zone when a sensor crosses a limit.
struct AutomationRule {
    std::string name;
    std::string sensorType;        // trigger, e.g. "co2"
    Direction direction = Direction::Above;
    double limit = 0.0;
    double hysteresis = 0.0;
    std::string targetType;        // actuator to switch, e.g. "fan"
};

struct ActuatorAction {
    std::string zone;
    std::string targetType;
    std::string rule;
    bool powerOn = false;
};

class AutomationEngine {
public:
    explicit AutomationEngine(std::vector<AutomationRule> rules) : rules_(std::move(rules)) {}

    std::vector<ActuatorAction> evaluate(const Reading& r) {
        std::vector<ActuatorAction> actions;
        for (const auto& rule : rules_) {
            if (rule.sensorType != r.type) continue;

            const std::string key = r.zone + '|' + rule.name;
            auto it = trackers_.try_emplace(key, rule.direction, rule.limit, rule.hysteresis).first;

            switch (it->second.update(r.value)) {
            case Transition::Entered:
                actions.push_back({r.zone, rule.targetType, rule.name, true});
                break;
            case Transition::Cleared:
                actions.push_back({r.zone, rule.targetType, rule.name, false});
                break;
            case Transition::None:
                break;
            }
        }
        return actions;
    }

private:
    std::vector<AutomationRule> rules_;
    std::map<std::string, ThresholdTracker> trackers_;
};

// Detects sensors that have stopped reporting. A device is reported once when it
// goes silent, and heard() returns true when it reappears.
class SilenceDetector {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit SilenceDetector(std::chrono::milliseconds timeout) : timeout_(timeout) {}

    // Returns true if the device was previously reported silent (i.e. it recovered).
    bool heard(const std::string& device, TimePoint when) {
        State& s = states_[device];
        const bool recovered = s.silent;
        s.last = when;
        s.silent = false;
        return recovered;
    }

    // Devices that crossed the timeout since the last call.
    std::vector<std::string> newlySilent(TimePoint now) {
        std::vector<std::string> out;
        for (auto& [id, s] : states_) {
            if (!s.silent && now - s.last > timeout_) {
                s.silent = true;
                out.push_back(id);
            }
        }
        return out;
    }

private:
    struct State {
        TimePoint last{};
        bool silent = false;
    };

    std::chrono::milliseconds timeout_;
    std::unordered_map<std::string, State> states_;
};

// Default thresholds matching the previous hard-coded server limits, plus CO2.
inline std::vector<ThresholdRule> defaultThresholds() {
    return {
        {"temperature-high", "temperature", Direction::Above, 26.0, 0.5, 4.0},
        {"temperature-low",  "temperature", Direction::Below, 18.0, 0.5, 4.0},
        {"humidity-high",    "humidity",    Direction::Above, 60.0, 2.0, 10.0},
        {"humidity-low",     "humidity",    Direction::Below, 30.0, 2.0, 10.0},
        {"co2-high",         "co2",         Direction::Above, 1000.0, 50.0, 500.0},
        {"battery-low",      "battery",     Direction::Below, 20.0, 3.0, 10.0},
        {"signal-weak",      "rssi",        Direction::Below, -85.0, 3.0, 10.0},
    };
}

// Default automation: run the ventilation fan while CO2 is high.
inline std::vector<AutomationRule> defaultAutomation() {
    return {
        {"co2-ventilation", "co2", Direction::Above, 900.0, 100.0, "fan"},
    };
}

}  // namespace sb::rules
