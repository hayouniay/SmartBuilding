// Dependency-free unit tests (no Qt needed). Build target: sb_unit_tests.

#include "common/Backoff.h"
#include "common/Rules.h"

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

using namespace std::chrono_literals;
using namespace sb;
using namespace sb::rules;

void testBackoff() {
    Backoff b(1000ms, 5000ms);
    CHECK(b.nextDelay() == 1000ms);
    CHECK(b.nextDelay() == 2000ms);
    CHECK(b.nextDelay() == 4000ms);
    CHECK(b.nextDelay() == 5000ms);   // capped
    CHECK(b.nextDelay() == 5000ms);
    CHECK(b.attempts() == 5);
    b.reset();
    CHECK(b.attempts() == 0);
    CHECK(b.nextDelay() == 1000ms);   // starts over after a successful connect
}

void testHysteresisAbove() {
    ThresholdTracker t(Direction::Above, 26.0, 0.5);
    CHECK(t.update(25.9) == Transition::None);
    CHECK(t.update(26.5) == Transition::Entered);
    CHECK(t.update(27.0) == Transition::None);  // still active, no repeat
    CHECK(t.update(26.0) == Transition::None);  // inside hysteresis band
    CHECK(t.update(25.6) == Transition::None);  // still above limit - hysteresis (25.5)
    CHECK(t.update(25.4) == Transition::Cleared);
    CHECK(!t.active());
}

void testHysteresisBelow() {
    ThresholdTracker t(Direction::Below, 18.0, 0.5);
    CHECK(t.update(18.5) == Transition::None);
    CHECK(t.update(17.9) == Transition::Entered);
    CHECK(t.update(18.2) == Transition::None);
    CHECK(t.update(18.6) == Transition::Cleared);
}

void testEvaluatorLevelsAndRecovery() {
    ThresholdEvaluator ev(defaultThresholds());

    auto warn = ev.evaluate({"TS-A", "F2", "temperature", 27.0});
    CHECK(warn.size() == 1);
    CHECK(warn[0].rule == "temperature-high");
    CHECK(warn[0].level == Level::Warning);
    CHECK(!warn[0].recovered);

    CHECK(ev.evaluate({"TS-A", "F2", "temperature", 28.0}).empty());   // no repeat alert

    auto crit = ev.evaluate({"TS-B", "F3", "temperature", 31.0});       // different device
    CHECK(crit.size() == 1);
    CHECK(crit[0].level == Level::Critical);

    auto back = ev.evaluate({"TS-A", "F2", "temperature", 25.0});
    CHECK(back.size() == 1);
    CHECK(back[0].recovered);
}

void testEvaluatorIgnoresOtherSensorTypes() {
    ThresholdEvaluator ev(defaultThresholds());
    CHECK(ev.evaluate({"HS-A", "F2", "humidity", 50.0}).empty());
    CHECK(ev.evaluate({"CO2-A", "F2", "co2", 1200.0}).size() == 1);
}

void testAutomationFanFollowsCo2() {
    AutomationEngine engine(defaultAutomation());

    auto on = engine.evaluate({"CO2-F2", "F2", "co2", 950.0});
    CHECK(on.size() == 1);
    CHECK(on[0].targetType == "fan");
    CHECK(on[0].zone == "F2");
    CHECK(on[0].powerOn);

    CHECK(engine.evaluate({"CO2-F2", "F2", "co2", 980.0}).empty());    // still high, no repeat
    CHECK(engine.evaluate({"CO2-F2", "F2", "co2", 850.0}).empty());    // inside hysteresis band
    auto off = engine.evaluate({"CO2-F2", "F2", "co2", 700.0});
    CHECK(off.size() == 1);
    CHECK(!off[0].powerOn);
}

void testAutomationIsScopedByZone() {
    AutomationEngine engine(defaultAutomation());
    CHECK(engine.evaluate({"CO2-F2", "F2", "co2", 950.0}).size() == 1);
    auto other = engine.evaluate({"CO2-F3", "F3", "co2", 950.0});
    CHECK(other.size() == 1);
    CHECK(other[0].zone == "F3");
}

void testSilenceDetection() {
    using TP = SilenceDetector::TimePoint;
    SilenceDetector det(10s);
    const TP t0 = TP{} + 1000s;

    det.heard("TS-A", t0);
    det.heard("TS-B", t0);
    CHECK(det.newlySilent(t0 + 5s).empty());

    det.heard("TS-B", t0 + 8s);                          // B keeps reporting
    auto silent = det.newlySilent(t0 + 11s);
    CHECK(silent.size() == 1);
    CHECK(silent[0] == "TS-A");
    CHECK(det.newlySilent(t0 + 20s).size() == 1);        // B is now silent too; A not repeated

    CHECK(det.heard("TS-A", t0 + 21s));                  // A recovers
    CHECK(!det.heard("TS-A", t0 + 22s));                 // ordinary message, not a recovery
}

void testHealthRules() {
    ThresholdEvaluator ev(defaultThresholds());

    auto low = ev.evaluate({"TS-A", "F2", "battery", 15.0});
    CHECK(low.size() == 1);
    CHECK(low[0].rule == "battery-low");
    CHECK(low[0].level == Level::Warning);

    auto critical = ev.evaluate({"TS-B", "F2", "battery", 5.0});
    CHECK(critical.size() == 1);
    CHECK(critical[0].level == Level::Critical);

    auto weak = ev.evaluate({"HS-A", "F2", "rssi", -90.0});
    CHECK(weak.size() == 1);
    CHECK(weak[0].rule == "signal-weak");
    CHECK(weak[0].level == Level::Warning);   // -90 is past -85, but not past -95

    auto back = ev.evaluate({"TS-A", "F2", "battery", 50.0});
    CHECK(back.size() == 1);
    CHECK(back[0].recovered);
}

}  // namespace

int main() {
    testBackoff();
    testHysteresisAbove();
    testHysteresisBelow();
    testEvaluatorLevelsAndRecovery();
    testEvaluatorIgnoresOtherSensorTypes();
    testAutomationFanFollowsCo2();
    testAutomationIsScopedByZone();
    testSilenceDetection();
    testHealthRules();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
