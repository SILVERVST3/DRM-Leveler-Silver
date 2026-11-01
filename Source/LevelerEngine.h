#pragma once
#include <cmath>
#include <algorithm>
#include <vector>

// =============================================================================
//  DRM_Leveler SILVER - motor DSP
//
//  Principio (igual ao Drum Leveler): em vez de um envelope follower + VCA,
//  o plugin DETECTA cada batida, mede o pico dela e aplica UM ganho para a
//  batida inteira (attack -> hold -> recovery). Com lookahead, a rampa de
//  ganho termina exatamente no inicio do transiente => "transient-accurate".
//
//  Linha do tempo (amostras):
//      onset detectado no sinal "do futuro" (nao atrasado)  -> n0
//      janela de medicao de pico                           -> n0 .. n0+W
//      no fim da janela a saida esta tocando n0 - A        -> rampa de A amostras
//      ganho completo exatamente no onset (n0) na saida
//      latencia total = W + A
// =============================================================================
namespace drm
{
    inline float dbToGain (float db)  { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g)   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

    // -------------------------------------------------------------------------
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0; }

        inline float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void set (bool highPass, double sr, double f, double q)
        {
            f = std::clamp (f, 10.0, sr * 0.45);
            const double w0 = 2.0 * 3.141592653589793 * f / sr;
            const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            double nb0, nb1, nb2;
            if (highPass) { nb0 = (1.0 + c) * 0.5; nb1 = -(1.0 + c); nb2 = nb0; }
            else          { nb0 = (1.0 - c) * 0.5; nb1 =  (1.0 - c); nb2 = nb0; }
            b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
            a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - alpha) / a0);
        }
    };

    // Butterworth de 8a ordem (48 dB/oct) = 4 biquads em cascata
    struct Butter8
    {
        Biquad s[4];
        static constexpr double qs[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        void set (bool hp, double sr, double f) { for (int i = 0; i < 4; ++i) s[i].set (hp, sr, f, qs[i]); }
        void reset() { for (auto& b : s) b.reset(); }
        inline float process (float x) { for (auto& b : s) x = b.process (x); return x; }
    };

    // -------------------------------------------------------------------------
    //  Side-Chain Filter: Band-Pass (HP@lo -> LP@hi) ou Band-Stop (LP@lo + HP@hi)
    // -------------------------------------------------------------------------
    struct SideChainFilter
    {
        Butter8 fLo, fHi;
        bool on = false; int mode = -1; float lo = -1, hi = -1; double sr = 44100;

        void prepare (double sampleRate) { sr = sampleRate; mode = -1; lo = hi = -1; fLo.reset(); fHi.reset(); }

        void update (bool enabled, int newMode, float newLo, float newHi)
        {
            on = enabled;
            if (! on) return;
            if (newHi < newLo) std::swap (newHi, newLo);
            if (newMode != mode || newLo != lo || newHi != hi)
            {
                if (newMode != mode) { fLo.reset(); fHi.reset(); }
                mode = newMode; lo = newLo; hi = newHi;
                if (mode == 0) { fLo.set (true, sr, lo);  fHi.set (false, sr, hi); }
                else           { fLo.set (false, sr, lo); fHi.set (true, sr, hi); }
            }
        }

        inline float process (float x)
        {
            if (! on) return x;
            if (mode == 0) return fHi.process (fLo.process (x));
            return fLo.process (x) + fHi.process (x);
        }
    };

    // -------------------------------------------------------------------------
    struct ChannelParams
    {
        float hiThr = 0, loThr = -40, target = -12;
        float comp = 0.5f;          // -1 .. +1
        float rangeDb = 24;
        float retrigMs = 23, holdMs = 85, recMs = 630;
        float sens = 0.5f;          // 0 .. 1
        float gateDb = 0;
        bool  sCurve = false;
    };

    // -------------------------------------------------------------------------
    //  Detector de batidas (roda no sinal NAO atrasado, ja filtrado pelo SCF)
    // -------------------------------------------------------------------------
    class BeatDetector
    {
    public:
        void prepare (double sampleRate, int windowSamples)
        {
            sr = sampleRate; W = std::max (1, windowSamples);
            relFast = (float) std::exp (-1.0 / (0.020 * sr));   // 20 ms
            kSlow   = (float) (1.0 - std::exp (-1.0 / (0.060 * sr))); // 60 ms
            reset();
        }

        void reset() { fast = slow = 0; since = 1 << 28; measuring = false; peak = 0; left = 0; }

        void setParams (const ChannelParams& p)
        {
            retrigSamples = (int) (p.retrigMs * 0.001 * sr);
            // sensibilidade alta = limiar de onset menor e piso mais baixo
            onsetDb = 14.0f - 11.0f * p.sens;        // 14 dB .. 3 dB
            floorDb = -45.0f - 35.0f * p.sens;       // -45 .. -80 dBFS
            // nao vale a pena "armar" o detector em sinais muito abaixo do Low Threshold
            floorDb = std::max (floorDb, p.loThr - 18.0f);
            loThr = p.loThr;
        }

        // a = |amostra de deteccao|. Retorna true quando uma batida foi finalizada
        // (fim da janela de medicao); levelDb recebe o pico da batida.
        inline bool push (float a, float& levelDb)
        {
            fast = a > fast ? a : fast * relFast;
            slow += (fast - slow) * kSlow;
            if (since < (1 << 28)) ++since;

            if (measuring)
            {
                peak = std::max (peak, a);
                if (--left <= 0)
                {
                    measuring = false; levelDb = gainToDb (peak);
                    // batida abaixo do Low Threshold (ruido/vazamento): nao consome o
                    // tempo de Min Retrigger, o detector re-arma imediatamente
                    if (levelDb < loThr) since = retrigSamples;
                    return true;
                }
                return false;
            }

            if (since >= retrigSamples)
            {
                const float fdb = gainToDb (fast);
                if (fdb > floorDb && fdb - gainToDb (slow + 1.0e-9f) > onsetDb)
                {
                    measuring = true; left = W; peak = a; since = 0;
                }
            }
            return false;
        }

        bool isMeasuring() const { return measuring; }

    private:
        double sr = 44100; int W = 256;
        float relFast = 0.999f, kSlow = 0.001f;
        float fast = 0, slow = 0, peak = 0;
        int since = 0, left = 0, retrigSamples = 1000;
        bool measuring = false;
        float onsetDb = 8, floorDb = -60, loThr = -40;
    };

    // -------------------------------------------------------------------------
    //  Envelope de ganho por batida (roda no tempo da SAIDA)
    //  Idle -> (trigger) Attack(A amostras) -> Hold -> Recovery -> Idle(gate)
    // -------------------------------------------------------------------------
    class GainEnvelope
    {
    public:
        void prepare (double sampleRate, int attackSamples)
        {
            sr = sampleRate; A = std::max (1, attackSamples);
            kIdle = (float) (1.0 - std::exp (-1.0 / (0.010 * sr)));
            reset();
        }
        void reset() { stage = Idle; cur = 0; n = 0; }

        void setParams (const ChannelParams& p)
        {
            holdN = (int) (p.holdMs * 0.001 * sr);
            recN  = (int) (p.recMs * 0.001 * sr);
            gate = p.gateDb; sCurve = p.sCurve;
        }

        void trigger (float beatGainDb) { from = cur; to = beatGainDb; stage = Attack; n = 0; }

        inline float next()
        {
            switch (stage)
            {
                case Idle:   cur += (gate - cur) * kIdle; break;
                case Attack:
                {
                    const float t = (float) (n + 1) / (float) A;
                    cur = from + (to - from) * shape (t);
                    if (++n >= A) { stage = Hold; n = 0; }
                    break;
                }
                case Hold:
                    cur = to;
                    if (++n >= holdN) { stage = Recovery; n = 0; from = to; }
                    break;
                case Recovery:
                    if (recN <= 0) { cur = gate; stage = Idle; break; }
                    {
                        const float t = (float) (n + 1) / (float) recN;
                        cur = from + (gate - from) * shape (t);
                        if (++n >= recN) stage = Idle;
                    }
                    break;
            }
            return cur;
        }

        float current() const { return cur; }

    private:
        enum Stage { Idle, Attack, Hold, Recovery };
        inline float shape (float t) const { return sCurve ? t * t * (3.0f - 2.0f * t) : t; }

        double sr = 44100; int A = 64;
        Stage stage = Idle;
        float cur = 0, from = 0, to = 0, gate = 0, kIdle = 0.01f;
        int n = 0, holdN = 0, recN = 0;
        bool sCurve = false;
    };

    // -------------------------------------------------------------------------
    //  Calculo do ganho da batida
    //  retorna false se a batida ficou abaixo do Low Threshold (nao e batida)
    //  ignored = true se ficou acima do High Threshold (passa sem processar)
    // -------------------------------------------------------------------------
    inline bool computeBeatGain (const ChannelParams& p, float levelDb, float& gainDb, bool& ignored)
    {
        ignored = false;
        if (levelDb < p.loThr) return false;
        if (levelDb > p.hiThr) { ignored = true; gainDb = 0; return true; }
        const float d = p.target - levelDb;      // distancia ate o alvo
        // comp > 0: compressao (puxa para o alvo). comp < 0: expansao (afasta do alvo)
        // ex.: comp = -100%, batida 6 dB acima do alvo -> d = -6 -> g = +6 -> 12 dB acima
        float g = d * p.comp;
        g = std::clamp (g, -p.rangeDb, p.rangeDb);
        gainDb = g;
        return true;
    }

    // -------------------------------------------------------------------------
    inline float softClip (float x)
    {
        constexpr float k = 0.7071f;              // joelho em -3 dBFS, teto 0 dBFS
        const float ax = std::abs (x);
        if (ax <= k) return x;
        const float y = k + (1.0f - k) * std::tanh ((ax - k) / (1.0f - k));
        return x < 0 ? -y : y;
    }

    // -------------------------------------------------------------------------
    struct DelayLine
    {
        std::vector<float> buf; int pos = 0, len = 1;
        void prepare (int samples) { len = std::max (1, samples); buf.assign ((size_t) len, 0.0f); pos = 0; }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        inline float process (float x) { const float y = buf[(size_t) pos]; buf[(size_t) pos] = x; if (++pos >= len) pos = 0; return y; }
    };
}
