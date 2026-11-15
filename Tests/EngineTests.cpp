#include "LevelerEngine.h"
#include <iostream>
#include <stdexcept>

static void check (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
}

static void envelopeAutomation()
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (bool curve : { false, true })
            for (float recovery : { 0.0f, 1.0f, 10.0f, 3000.0f })
            {
                drm::GainEnvelope env;
                env.prepare (sr, (int) (0.0015 * sr));
                drm::ChannelParams p;
                p.holdMs = 0; p.recMs = 1000; p.sCurve = curve;
                env.setParams (p); env.trigger (-12);
                for (int i = 0; i < (int) (sr * 0.5); ++i) env.next();
                p.recMs = recovery; env.setParams (p);
                float before = env.current();
                for (int i = 0; i < (int) (sr * 3.1); ++i)
                {
                    const float value = env.next();
                    check (std::isfinite (value) && value >= -12.001f && value <= 0.001f, "Recovery automation escaped its endpoints");
                    check (value >= before - 0.0001f, "Recovery reversed unexpectedly");
                    before = value;
                }
                check (std::abs (env.current()) < 0.001f, "Recovery did not reach gate");
                // Re-trigger and automate both gate and shape while recovering.
                env.trigger (8);
                for (int i = 0; i < (int) (sr * 0.05); ++i) env.next();
                p.gateDb = -24; p.sCurve = !curve; p.recMs = 10; env.setParams (p);
                for (int i = 0; i < (int) sr / 10; ++i)
                {
                    const auto v = env.next();
                    check (std::isfinite (v) && v >= -24.001f && v <= 8.001f, "Gate/curve automation escaped endpoints");
                }
            }
}

static void gainRules()
{
    drm::ChannelParams p;
    p.comp = 1; p.target = -12; p.boostLimit = 6; p.cutLimit = 9;
    float gain = 0; bool ignored = false;
    check (! drm::computeBeatGain (p, -60, gain, ignored), "Low threshold must reject spill");
    p.hiThr = -3;
    check (drm::computeBeatGain (p, -1, gain, ignored) && ignored && gain == 0, "High threshold must pass hit unchanged");
    check (drm::computeBeatGain (p, -30, gain, ignored) && gain == 6, "Boost cap failed");
    check (drm::computeBeatGain (p, -3, gain, ignored) && gain == -9, "Cut cap failed");
    p.rangeDb = 2;
    drm::computeBeatGain (p, -30, gain, ignored);
    check (gain == 2, "Legacy Gain Range must still cap new limits");
    p.rangeDb = 24; p.boostLimit = p.cutLimit = 24;
    drm::computeBeatGain (p, -30, gain, ignored, -18);
    check (gain == 6, "Main-input measurement must be independent of key level");
    p.comp = -1;
    drm::computeBeatGain (p, -18, gain, ignored);
    check (gain == -6, "Expansion direction changed");
}

static void detectionAndLookahead()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (bool adaptive : { false, true })
        {
            const int window = (int) std::round (sr * 0.008);
            const int attack = (int) std::round (sr * 0.0015);
            drm::BeatDetector detector; detector.prepare (sr, window);
            drm::ChannelParams p; p.adaptive = adaptive; p.sens = 1; p.loThr = -70; p.retrigMs = 5;
            detector.setParams (p);
            drm::DelayLine delay; delay.prepare (window + attack);
            drm::GainEnvelope env; env.prepare (sr, attack); env.setParams (p);
            int hits = 0, outputIndex = -1, triggerIndex = -1;
            for (int i = 0; i < (int) sr; ++i)
            {
                float level = 0;
                const float input = i == 100 ? 0.1f : 0;
                if (detector.push (i == 100 ? 0.5f : 0, level, input))
                {
                    if (level < p.loThr) continue;
                    ++hits; triggerIndex = i; env.trigger (6);
                    check (std::abs (detector.measuredInputDb() + 20) < 0.001f, "Input peak measurement failed");
                }
                const float gain = env.next();
                const float delayed = delay.process (input);
                if (delayed > 0)
                {
                    outputIndex = i;
                    check (std::abs (gain - 6) < 0.001f, "Gain ramp missed the transient");
                }
            }
            check (hits == 1, "One impulse must produce one hit");
            check (triggerIndex == 100 + window, "Detector window timing changed");
            check (outputIndex == 100 + window + attack, "Audio lookahead is misaligned");
            detector.reset(); detector.setParams (p); hits = 0;
            // Four progressively quieter ghost notes, separated by 60 ms.
            const int gap = (int) (sr * 0.06);
            for (int i = 0; i < gap * 6; ++i)
            {
                const int note = i / gap, pos = i % gap;
                const float value = note < 4 && pos == 0 ? 0.3f * std::pow (0.5f, (float) note) : 0;
                float level = 0; if (detector.push (value, level, value) && level >= p.loThr) ++hits;
            }
            if (adaptive) check (hits == 4, "Adaptive detection missed or duplicated ghost notes");
        }
}

static void clipper()
{
    for (int i = -10000; i <= 10000; ++i)
    {
        const float x = (float) i / 1000;
        const float y = drm::softClip (x);
        check (std::isfinite (y) && std::abs (y) <= 1.00001f, "Clipper exceeded its nominal sample ceiling");
        check (std::abs (y + drm::softClip (-x)) < 0.00001f, "Clipper lost symmetry");
    }
}

int main()
{
    try { envelopeAutomation(); gainRules(); detectionAndLookahead(); clipper(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "PASS: envelope automation, gain limits, independent measurement, detection, lookahead and clipper\n";
}
