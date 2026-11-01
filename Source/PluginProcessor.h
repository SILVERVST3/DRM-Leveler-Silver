#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Parameters.h"
#include "LevelerEngine.h"

// Uma coluna do display (~5 ms de audio) por canal de processamento
struct DisplayColumn
{
    float in[2]     { 0, 0 };   // pico de entrada (linear)
    float out[2]    { 0, 0 };   // pico de saida (linear)
    float gain[2]   { 0, 0 };   // ganho aplicado mais extremo (dB)
    float beatDb[2] { 0, 0 };   // nivel medido da batida (dB)
    int   beat[2]   { 0, 0 };   // 0 nenhuma, 1 processada, 2 ignorada (acima do High)
};

class DRMLevelerAudioProcessor : public juce::AudioProcessor
{
public:
    DRMLevelerAudioProcessor();
    ~DRMLevelerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ---- UI helpers ----
    juce::AudioProcessorValueTreeState apvts;

    int  getView() const;                 // 0 = L/M, 1 = SUM, 2 = R/S
    void setView (int v);
    int  getMode() const { return (int) modeParam->load(); }
    bool isStereoLinked() const { return getMode() == drmp::Stereo || getTotalNumInputChannels() < 2; }
    bool isMonoLayout() const { return getTotalNumInputChannels() < 2; }
    bool hasSidechain() const;

    // A/B
    int  getABSlot() const { return abSlot; }
    void switchAB (int slot);
    void copyToOtherSlot();

    // FIFO do display (audio -> UI)
    int  readColumns (DisplayColumn* dest, int maxCols);

    std::atomic<float> meterIn[2]  { { 0.0f }, { 0.0f } };
    std::atomic<float> meterOut[2] { { 0.0f }, { 0.0f } };
    std::atomic<float> gainNow[2]  { { 0.0f }, { 0.0f } };   // dB

    int currentPreset = 0;

private:
    void readParams (int ch, drm::ChannelParams& p) const;

    struct ChParamPtrs
    {
        std::atomic<float>* hiThr, *loThr, *target, *comp, *range, *retrig, *hold, *recovery, *sens, *gate;
        std::atomic<float>* scfOn, *scfMode, *scfLo, *scfHi, *solo, *sc;
    } cp[2];

    std::atomic<float>* modeParam, *inLP, *inRP, *outLP, *outRP, *softClipP, *sCurveP, *bypassP;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    drm::SideChainFilter scf[2];
    drm::BeatDetector    det[2];
    drm::GainEnvelope    env[2];
    drm::DelayLine       audioDelay[2], detDelay[2], dryDelay[2];

    double sr = 44100.0;
    int latency = 0, colSamples = 220, colCount = 0;
    int lastMode = -1;
    float bypassMix = 0.0f;
    juce::SmoothedValue<float> gIn[2], gOut[2];

    // acumulador da coluna atual
    DisplayColumn acc;

    juce::AbstractFifo fifo { 2048 };
    std::vector<DisplayColumn> fifoBuf = std::vector<DisplayColumn> (2048);

    int abSlot = 0;
    juce::ValueTree abStates[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DRMLevelerAudioProcessor)
};
