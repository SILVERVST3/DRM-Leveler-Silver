#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace drmp;

static constexpr double kWindowMs = 8.0;   // janela de medicao do pico da batida
static constexpr double kAttackMs = 1.5;   // rampa de ganho antes do transiente
static constexpr double kColumnMs = 5.0;   // resolucao do display

DRMLevelerAudioProcessor::DRMLevelerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "DRMLevelerSILVER", createLayout())
{
    for (int ch = 0; ch < 2; ++ch)
    {
        auto g = [&] (const char* stem) { return apvts.getRawParameterValue (id (ch, stem)); };
        cp[ch] = { g (hiThr), g (loThr), g (target), g (comp), g (range), g (retrig), g (hold),
                   g (recovery), g (sens), g (gate), g (scfOn), g (scfMode), g (scfLo), g (scfHi),
                   g (solo), g (sc) };
    }
    modeParam = apvts.getRawParameterValue (mode);
    inLP      = apvts.getRawParameterValue (inL);
    inRP      = apvts.getRawParameterValue (inR);
    outLP     = apvts.getRawParameterValue (outL);
    outRP     = apvts.getRawParameterValue (outR);
    softClipP = apvts.getRawParameterValue (softClip);
    sCurveP   = apvts.getRawParameterValue (sCurve);
    bypassP   = apvts.getRawParameterValue (bypass);
    bypassParam = apvts.getParameter (bypass);

    apvts.state.setProperty ("view", 1, nullptr);
}

bool DRMLevelerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

bool DRMLevelerAudioProcessor::hasSidechain() const
{
    if (getBusCount (true) < 2) return false;
    auto* b = getBus (true, 1);
    return b != nullptr && b->isEnabled() && b->getNumberOfChannels() > 0;
}

void DRMLevelerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    const int W = juce::jmax (1, (int) std::round (kWindowMs * 0.001 * sr));
    const int A = juce::jmax (1, (int) std::round (kAttackMs * 0.001 * sr));
    latency = W + A;
    setLatencySamples (latency);

    for (int c = 0; c < 2; ++c)
    {
        scf[c].prepare (sr);
        det[c].prepare (sr, W);
        env[c].prepare (sr, A);
        audioDelay[c].prepare (latency);
        detDelay[c].prepare (latency);
        dryDelay[c].prepare (latency);
        gIn[c].reset (sr, 0.02);
        gOut[c].reset (sr, 0.02);
    }
    gIn[0].setCurrentAndTargetValue (drm::dbToGain (inLP->load()));
    gIn[1].setCurrentAndTargetValue (drm::dbToGain (inRP->load()));
    gOut[0].setCurrentAndTargetValue (drm::dbToGain (outLP->load()));
    gOut[1].setCurrentAndTargetValue (drm::dbToGain (outRP->load()));

    colSamples = juce::jmax (16, (int) std::round (kColumnMs * 0.001 * sr));
    colCount = 0;
    acc = {};
    lastMode = -1;
    bypassMix = bypassP->load() > 0.5f ? 1.0f : 0.0f;
}

void DRMLevelerAudioProcessor::readParams (int ch, drm::ChannelParams& p) const
{
    const auto& c = cp[ch];
    p.hiThr    = c.hiThr->load();
    p.loThr    = juce::jmin (c.loThr->load(), p.hiThr);
    p.target   = c.target->load();
    p.comp     = c.comp->load() * 0.01f;
    p.rangeDb  = c.range->load();
    p.retrigMs = c.retrig->load();
    p.holdMs   = c.hold->load();
    p.recMs    = c.recovery->load();
    p.sens     = c.sens->load() * 0.01f;
    p.gateDb   = c.gate->load();
    p.sCurve   = sCurveP->load() > 0.5f;
}

void DRMLevelerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getMainBusNumInputChannels());
    if (numCh <= 0) return;

    auto mainBuf = getBusBuffer (buffer, true, 0);
    float* io[2] = { mainBuf.getWritePointer (0), numCh > 1 ? mainBuf.getWritePointer (1) : nullptr };

    const float* scp[2] = { nullptr, nullptr };
    int scCh = 0;
    if (hasSidechain())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        scCh = juce::jmin (2, scBuf.getNumChannels());
        for (int c = 0; c < scCh; ++c) scp[c] = scBuf.getReadPointer (c);
    }

    // ---- parametros ----
    int m = (int) modeParam->load();
    if (numCh < 2) m = DualMono;
    const bool linked = (m == Stereo) || numCh < 2;
    const bool ms = (m == MidSide);
    if (m != lastMode)
    {
        for (int c = 0; c < 2; ++c) { det[c].reset(); env[c].reset(); }
        lastMode = m;
    }

    drm::ChannelParams P[2];
    readParams (0, P[0]);
    readParams (1, P[1]);
    if (linked) P[1] = P[0];

    bool useSC[2], solo[2];
    for (int c = 0; c < 2; ++c)
    {
        const int src = linked ? 0 : c;
        scf[c].update (cp[src].scfOn->load() > 0.5f, (int) cp[src].scfMode->load(),
                       cp[src].scfLo->load(), cp[src].scfHi->load());
        useSC[c] = scCh > 0 && cp[src].sc->load() > 0.5f;
        solo[c]  = cp[src].solo->load() > 0.5f;
        det[c].setParams (P[c]);
        env[c].setParams (P[c]);
    }

    gIn[0].setTargetValue (drm::dbToGain (inLP->load()));
    gIn[1].setTargetValue (drm::dbToGain (inRP->load()));
    gOut[0].setTargetValue (drm::dbToGain (outLP->load()));
    gOut[1].setTargetValue (drm::dbToGain (outRP->load()));
    const bool clip = softClipP->load() > 0.5f;
    const float bypTarget = bypassP->load() > 0.5f ? 1.0f : 0.0f;
    const float bypStep = (float) (1.0 / (0.02 * sr));

    float pkIn[2] = { 0, 0 }, pkOut[2] = { 0, 0 };

    for (int i = 0; i < numSamples; ++i)
    {
        float raw[2] = { io[0][i], numCh > 1 ? io[1][i] : 0.0f };
        float dry[2] = { dryDelay[0].process (raw[0]), numCh > 1 ? dryDelay[1].process (raw[1]) : 0.0f };

        const float gi0 = gIn[0].getNextValue(), gi1 = gIn[1].getNextValue();
        float x[2] = { raw[0] * gi0, raw[1] * gi1 };
        pkIn[0] = juce::jmax (pkIn[0], std::abs (x[0]));
        pkIn[1] = juce::jmax (pkIn[1], std::abs (x[1]));

        if (ms) { const float mm = (x[0] + x[1]) * 0.5f, ss = (x[0] - x[1]) * 0.5f; x[0] = mm; x[1] = ss; }

        // ---- sinal de deteccao (key) ----
        float key[2] = { x[0], x[1] };
        if (useSC[0] || useSC[1])
        {
            float s0 = scp[0][i], s1 = scCh > 1 ? scp[1][i] : s0;
            if (ms) { const float mm = (s0 + s1) * 0.5f, ss = (s0 - s1) * 0.5f; s0 = mm; s1 = ss; }
            if (useSC[0]) key[0] = s0;
            if (useSC[1]) key[1] = s1;
        }
        float d[2] = { scf[0].process (key[0]), numCh > 1 ? scf[1].process (key[1]) : 0.0f };

        // ---- deteccao de batidas ----
        auto handleBeat = [&] (int unit, float lvl)
        {
            float g = 0; bool ignored = false;
            if (drm::computeBeatGain (P[unit], lvl, g, ignored))
            {
                env[unit].trigger (g);
                const int flag = ignored ? 2 : 1;
                for (int c = 0; c < 2; ++c)
                    if (linked || c == unit) { acc.beat[c] = flag; acc.beatDb[c] = lvl; }
            }
        };

        float lvl;
        if (linked)
        {
            if (det[0].push (juce::jmax (std::abs (d[0]), std::abs (d[1])), lvl)) handleBeat (0, lvl);
        }
        else
        {
            for (int c = 0; c < numCh; ++c)
                if (det[c].push (std::abs (d[c]), lvl)) handleBeat (c, lvl);
        }

        // ---- caminho de audio atrasado ----
        float dl[2], dd[2];
        for (int c = 0; c < 2; ++c) { dl[c] = audioDelay[c].process (x[c]); dd[c] = detDelay[c].process (d[c]); }

        const float g0 = env[0].next();
        const float g1 = linked ? g0 : env[1].next();
        const float gd[2] = { g0, g1 };

        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            y[c] = solo[c] ? dd[c] : dl[c] * drm::dbToGain (gd[c]);
            acc.in[c]  = juce::jmax (acc.in[c],  std::abs (dl[c]));
            acc.out[c] = juce::jmax (acc.out[c], std::abs (y[c]));
            if (std::abs (gd[c]) > std::abs (acc.gain[c])) acc.gain[c] = gd[c];
        }

        if (ms) { const float l = y[0] + y[1], r = y[0] - y[1]; y[0] = l; y[1] = r; }

        y[0] *= gOut[0].getNextValue();
        y[1] *= gOut[1].getNextValue();
        if (clip) { y[0] = drm::softClip (y[0]); y[1] = drm::softClip (y[1]); }

        // bypass com crossfade (mantem a latencia)
        if (bypassMix != bypTarget)
            bypassMix = bypTarget > bypassMix ? juce::jmin (bypTarget, bypassMix + bypStep)
                                              : juce::jmax (bypTarget, bypassMix - bypStep);
        if (bypassMix > 0.0f)
            for (int c = 0; c < 2; ++c) y[c] = y[c] * (1.0f - bypassMix) + dry[c] * bypassMix;

        io[0][i] = y[0];
        if (numCh > 1) io[1][i] = y[1];
        pkOut[0] = juce::jmax (pkOut[0], std::abs (y[0]));
        pkOut[1] = juce::jmax (pkOut[1], std::abs (y[1]));

        // ---- coluna do display ----
        if (++colCount >= colSamples)
        {
            colCount = 0;
            if (numCh < 2) { acc.in[1] = acc.in[0]; acc.out[1] = acc.out[0]; acc.gain[1] = acc.gain[0];
                             acc.beat[1] = acc.beat[0]; acc.beatDb[1] = acc.beatDb[0]; }
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0) fifoBuf[(size_t) scope.startIndex1] = acc;
            else if (scope.blockSize2 > 0) fifoBuf[(size_t) scope.startIndex2] = acc;
            acc = {};
        }
    }

    if (numCh < 2) { pkIn[1] = pkIn[0]; pkOut[1] = pkOut[0]; }
    for (int c = 0; c < 2; ++c)
    {
        meterIn[c].store (juce::jmax (meterIn[c].load(), pkIn[c]));
        meterOut[c].store (juce::jmax (meterOut[c].load(), pkOut[c]));
    }
    gainNow[0].store (env[0].current());
    gainNow[1].store (linked ? env[0].current() : env[1].current());
}

int DRMLevelerAudioProcessor::readColumns (DisplayColumn* dest, int maxCols)
{
    const int n = juce::jmin (maxCols, fifo.getNumReady());
    if (n <= 0) return 0;
    const auto scope = fifo.read (n);
    int k = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[k++] = fifoBuf[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[k++] = fifoBuf[(size_t) (scope.startIndex2 + i)];
    return k;
}

// ---- view / A-B ----
int DRMLevelerAudioProcessor::getView() const
{
    return (int) apvts.state.getProperty ("view", 1);
}

void DRMLevelerAudioProcessor::setView (int v)
{
    apvts.state.setProperty ("view", juce::jlimit (0, 2, v), nullptr);
}

void DRMLevelerAudioProcessor::switchAB (int slot)
{
    if (slot == abSlot) return;
    const int view = getView();
    abStates[abSlot] = apvts.copyState();
    abSlot = slot;
    if (abStates[slot].isValid())
        apvts.replaceState (abStates[slot].createCopy());
    setView (view);
}

void DRMLevelerAudioProcessor::copyToOtherSlot()
{
    abStates[1 - abSlot] = apvts.copyState();
}

// ---- programas: presets ficam no combo da interface (evita host sobrescrever o estado) ----
int DRMLevelerAudioProcessor::getNumPrograms() { return 1; }
void DRMLevelerAudioProcessor::setCurrentProgram (int) {}
const juce::String DRMLevelerAudioProcessor::getProgramName (int) { return "Default"; }

void DRMLevelerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DRMLevelerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentPreset = (int) vt.getProperty ("preset", 0);
            apvts.replaceState (vt);
            if (! apvts.state.hasProperty ("view")) setView (1);
        }
}

juce::AudioProcessorEditor* DRMLevelerAudioProcessor::createEditor()
{
    return new DRMLevelerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DRMLevelerAudioProcessor();
}
