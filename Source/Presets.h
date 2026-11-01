#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

// Presets de fabrica. Valores aplicados aos dois processadores (A e B).
namespace drmpresets
{
    struct Preset
    {
        const char* name;
        int mode;                                     // -1 = manter
        std::vector<std::pair<const char*, float>> v; // stem -> valor (plain)
    };

    inline const std::vector<Preset>& list()
    {
        using namespace drmp;
        static const std::vector<Preset> presets =
        {
            { "Default", -1, {} },
            { "Drum Bus - Leveler", 1,
              { { hiThr, 0 }, { loThr, -17.4f }, { target, -2.7f }, { comp, 54.8f }, { range, 80 },
                { retrig, 23 }, { hold, 85 }, { recovery, 630 }, { sens, 50 }, { gate, 0 },
                { scfOn, 1 }, { scfMode, 0 }, { scfLo, 80 }, { scfHi, 20000 } } },
            { "Kick - Even Hits", 1,
              { { hiThr, 0 }, { loThr, -40 }, { target, -10 }, { comp, 80 }, { range, 12 },
                { retrig, 40 }, { hold, 60 }, { recovery, 200 }, { sens, 50 }, { gate, 0 },
                { scfOn, 1 }, { scfMode, 1 }, { scfLo, 200 }, { scfHi, 3000 } } },
            { "Kick - Gate Spill", 1,
              { { hiThr, 0 }, { loThr, -30 }, { target, -10 }, { comp, 30 }, { range, 12 },
                { retrig, 50 }, { hold, 90 }, { recovery, 250 }, { sens, 45 }, { gate, -24 },
                { scfOn, 1 }, { scfMode, 1 }, { scfLo, 200 }, { scfHi, 3000 } } },
            { "Snare - Even Hits", 1,
              { { hiThr, 0 }, { loThr, -36 }, { target, -12 }, { comp, 75 }, { range, 12 },
                { retrig, 50 }, { hold, 50 }, { recovery, 250 }, { sens, 55 }, { gate, 0 },
                { scfOn, 1 }, { scfMode, 0 }, { scfLo, 180 }, { scfHi, 2500 } } },
            { "Snare - Ghost Notes Up", 1,
              { { hiThr, -18 }, { loThr, -45 }, { target, -20 }, { comp, 70 }, { range, 10 },
                { retrig, 35 }, { hold, 40 }, { recovery, 150 }, { sens, 70 }, { gate, 0 },
                { scfOn, 1 }, { scfMode, 0 }, { scfLo, 180 }, { scfHi, 2500 } } },
            { "Toms - Bleed Gate", 1,
              { { hiThr, 0 }, { loThr, -30 }, { target, -12 }, { comp, 40 }, { range, 12 },
                { retrig, 80 }, { hold, 120 }, { recovery, 400 }, { sens, 40 }, { gate, -30 },
                { scfOn, 1 }, { scfMode, 0 }, { scfLo, 60 }, { scfHi, 1500 } } },
            { "Room - Expand Punch", 1,
              { { hiThr, 0 }, { loThr, -40 }, { target, -18 }, { comp, -40 }, { range, 6 },
                { retrig, 40 }, { hold, 40 }, { recovery, 150 }, { sens, 50 }, { gate, 0 },
                { scfOn, 0 } } },
            { "Overheads - Gentle Level", 1,
              { { hiThr, 0 }, { loThr, -40 }, { target, -14 }, { comp, 35 }, { range, 6 },
                { retrig, 30 }, { hold, 85 }, { recovery, 630 }, { sens, 50 }, { gate, 0 },
                { scfOn, 0 } } },
            { "Bleed Booster (Gate +)", 1,
              { { hiThr, 0 }, { loThr, -30 }, { target, -12 }, { comp, 20 }, { range, 6 },
                { retrig, 40 }, { hold, 60 }, { recovery, 300 }, { sens, 50 }, { gate, 6 },
                { scfOn, 0 } } },
        };
        return presets;
    }

    inline void setPlain (juce::AudioProcessorValueTreeState& s, const juce::String& pid, float v)
    {
        if (auto* p = s.getParameter (pid))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }

    inline void apply (juce::AudioProcessorValueTreeState& s, int index)
    {
        const auto& all = list();
        if (index < 0 || index >= (int) all.size()) return;
        const auto& pr = all[(size_t) index];

        // "Default" = volta todos os parametros por canal ao default
        if (pr.v.empty())
        {
            for (auto* p : s.processor.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto pid = rp->getParameterID();
                    if (pid.startsWith ("a_") || pid.startsWith ("b_"))
                    {
                        rp->beginChangeGesture();
                        rp->setValueNotifyingHost (rp->getDefaultValue());
                        rp->endChangeGesture();
                    }
                }
            return;
        }

        for (int ch = 0; ch < 2; ++ch)
            for (auto& kv : pr.v)
                setPlain (s, drmp::id (ch, kv.first), kv.second);
        if (pr.mode >= 0)
            setPlain (s, drmp::mode, (float) pr.mode);
    }
}
