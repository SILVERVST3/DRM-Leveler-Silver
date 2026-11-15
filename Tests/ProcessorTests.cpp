#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <complex>

namespace
{
void require (bool value, const char* message) { if (! value) throw std::runtime_error (message); }
void set (DRMLevelerAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
float get (DRMLevelerAudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
void neutral (DRMLevelerAudioProcessor& p)
{
    set (p, "a_comp", 0); set (p, "b_comp", 0);
}
void latencyAndParallel()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 17, 64, 511, 1024 })
            for (float mix : { 0.0f, 37.0f, 100.0f })
            {
                DRMLevelerAudioProcessor p; neutral (p); set (p, "mix", mix);
                p.prepareToPlay (sr, block);
                const int latency = p.getLatencySamples();
                require (latency >= (int) std::round (sr * 0.0095), "Latency was not reported");
                int time = 0;
                for (int b = 0; b < 20 + latency / block; ++b)
                {
                    juce::AudioBuffer<float> audio (2, block); juce::MidiBuffer midi;
                    for (int i = 0; i < block; ++i)
                        for (int c = 0; c < 2; ++c)
                            audio.setSample (c, i, (time + i) == 0 ? (c == 0 ? 0.25f : -0.5f) : 0);
                    p.processBlock (audio, midi);
                    for (int i = 0; i < block; ++i)
                        for (int c = 0; c < 2; ++c)
                        {
                            const float expected = (time + i) == latency ? (c == 0 ? 0.25f : -0.5f) : 0;
                            require (std::abs (audio.getSample (c, i) - expected) < 1.0e-5f, "Dry/wet delay or unity null failed");
                        }
                    time += block;
                }
            }
}
void monoSidechain()
{
    DRMLevelerAudioProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    layout.inputBuses.set (1, juce::AudioChannelSet::stereo());
    require (p.setBusesLayout (layout), "Mono plus stereo sidechain layout rejected");
    require (p.isMonoLayout() && p.isStereoLinked() && p.hasSidechain(), "Sidechain changed mono UI classification");
    neutral (p); p.prepareToPlay (48000, 64);
    juce::AudioBuffer<float> audio (3, 64); juce::MidiBuffer midi;
    for (int b = 0; b < 20; ++b)
    {
        audio.clear(); audio.addFrom (1, 0, std::vector<float> (64, 0.75f).data(), 64);
        p.processBlock (audio, midi);
        require (audio.getMagnitude (0, 0, 64) == 0, "Sidechain leaked into main output");
    }
}
void meteringAndModes()
{
    DRMLevelerAudioProcessor p; neutral (p); p.prepareToPlay (48000, 64);
    juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
    for (int b = 0; b < 220; ++b)
    {
        if (b == 30) set (p, "mode", 2);
        if (b == 60) set (p, "softClip", 1);
        if (b == 90) set (p, "oversample", 1);
        if (b == 120) set (p, "bypass", 1);
        if (b == 150) { set (p, "mode", 0); set (p, "bypass", 0); }
        if (b == 180) { set (p, "ceiling", -6); set (p, "outL", 6); }
        p.meterOut[0].exchange (0); p.meterOut[1].exchange (0);
        for (int i = 0; i < 64; ++i)
        {
            audio.setSample (0, i, 0.8f * std::sin ((float) (b * 64 + i) * 0.15f));
            audio.setSample (1, i, 0.5f * std::sin ((float) (b * 64 + i) * 0.11f));
        }
        p.processBlock (audio, midi);
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 64; ++i) require (std::isfinite (audio.getSample (c, i)), "Mode/quality automation produced nonfinite output");
            require (std::abs (p.meterOut[c].load() - audio.getMagnitude (c, 0, 64)) < 1.0e-5f, "Output meter does not match final L/R audio");
        }
    }
}
void oversamplingAlias()
{
    double aliases[2] {};
    for (int quality = 0; quality < 2; ++quality)
    {
        DRMLevelerAudioProcessor p; neutral (p);
        set (p, "softClip", 1); set (p, "oversample", (float) quality);
        p.prepareToPlay (48000, 64);
        std::complex<double> sum {};
        juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
        for (int block = 0; block < 825; ++block)
        {
            for (int i = 0; i < 64; ++i)
            {
                const int time = block * 64 + i;
                const float x = 2.0f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 10000.0 * time / 48000.0);
                audio.setSample (0, i, x); audio.setSample (1, i, x);
            }
            p.processBlock (audio, midi);
            for (int i = 0; i < 64; ++i)
            {
                const int time = block * 64 + i;
                if (time >= 4800)
                    sum += (double) audio.getSample (0, i) * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * 18000.0 * time / 48000.0);
            }
        }
        aliases[quality] = std::abs (sum) / 48000.0;
    }
    require (aliases[1] < aliases[0] * 0.5, "4x clipper did not sufficiently reduce 18 kHz alias from a 10 kHz tone");
    std::cout << "Clipper alias improvement: " << 20 * std::log10 (aliases[0] / aliases[1]) << " dB (10 kHz tone, 48 kHz sample rate)\n";
}
void stateRoundTrip()
{
    DRMLevelerAudioProcessor p;
    set (p, "mix", 31); set (p, "a_comp", 72); p.markPresetClean();
    require (! p.isPresetModified(), "Clean preset marked modified");
    p.copyToOtherSlot(); p.switchAB (1); set (p, "mix", 68);
    require (p.isPresetModified(), "Parameter edit not marked modified");
    juce::MemoryBlock state; p.getStateInformation (state);
    DRMLevelerAudioProcessor restored; restored.setStateInformation (state.getData(), (int) state.getSize());
    require (restored.getABSlot() == 1 && get (restored, "mix") == 68, "Current A/B slot was not restored");
    restored.switchAB (0);
    require (get (restored, "mix") == 31 && get (restored, "a_comp") == 72, "Other A/B snapshot was not restored");
    restored.switchAB (1); require (get (restored, "mix") == 68, "A/B switching lost restored state");
    require (restored.getNumPrograms() == 1 && restored.getCurrentProgram() == 0, "Host program index is out of range");
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    auto legacy = juce::ValueTree::fromXml (*xml);
    legacy.removeChild (legacy.getChildWithName ("ABSlots"), nullptr);
    for (int i = legacy.getNumChildren(); --i >= 0;)
    {
        const auto id = legacy.getChild (i)["id"].toString();
        if (id == "mix" || id == "boostMax" || id == "cutMax" || id == "measureInput" || id == "adaptive" || id == "oversample" || id == "ceiling")
            legacy.removeChild (i, nullptr);
    }
    juce::MemoryBlock legacyData;
    juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), legacyData);
    set (restored, "oversample", 1); set (restored, "measureInput", 1);
    restored.setStateInformation (legacyData.getData(), (int) legacyData.getSize());
    require (get (restored, "mix") == 100 && get (restored, "boostMax") == 80 && get (restored, "oversample") == 0 && get (restored, "measureInput") == 0,
             "Legacy state inherited new settings from a prior session");
}
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        latencyAndParallel(); monoSidechain(); meteringAndModes(); oversamplingAlias(); stateRoundTrip();
        if (argc == 3 && juce::String (argv[1]) == "--render")
        {
            DRMLevelerAudioProcessor p; p.prepareToPlay (48000, 64);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            juce::File file (juce::String::fromUTF8 (argv[2])); file.deleteFile();
            auto stream = file.createOutputStream();
            require (stream != nullptr && juce::PNGImageFormat().writeImageToStream (image, *stream), "Could not render editor preview");
        }
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "PASS: latency/parallel, mono sidechain, output metering, mode/quality automation, A/B and legacy state\n";
}
