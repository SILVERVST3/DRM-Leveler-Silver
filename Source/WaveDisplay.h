#pragma once
#include "SilverUI.h"

// =============================================================================
//  Display rolante: entrada (cinza), boost (laranja), reducao (azul), saida
//  (branco), marcadores de batida e as 3 linhas arrastaveis HI / LO / TARGET.
// =============================================================================
class WaveDisplay : public juce::Component
{
public:
    WaveDisplay (DRMLevelerAudioProcessor& p, DualSource& hiS, DualSource& loS, DualSource& tgS, DualSource& gateS)
        : proc (p), hi (hiS), lo (loS), tg (tgS), gate (gateS)
    {
        hist.resize (2048);
    }

    void push (const DisplayColumn* cols, int n)
    {
        for (int i = 0; i < n; ++i) { hist[(size_t) w] = cols[i]; w = (w + 1) % (int) hist.size(); }
        if (n > 0) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();
        g.setColour (silver::display);
        g.fillRoundedRectangle (full, 6.0f);
        auto r = plotArea();

        // grade
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int db = 0; db >= (int) dbBot; db -= 6)
        {
            const float y = yFor ((float) db, r);
            g.setColour (silver::grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (silver::lineLt.withAlpha (0.35f));
            g.drawText (juce::String (db), juce::Rectangle<float> (r.getRight() + 2, y - 6, 26, 12),
                        juce::Justification::centredLeft);
        }

        const bool linked = proc.isStereoLinked();
        const int view = linked ? 1 : proc.getView();
        const int N = juce::jmin ((int) r.getWidth(), (int) hist.size());
        const int Nh = (int) hist.size();

        auto colAt = [&] (int k) -> const DisplayColumn& { return hist[(size_t) ((w - 1 - k + 2 * Nh) % Nh)]; };
        auto pick = [&] (const float* v) { return view == 1 ? juce::jmax (v[0], v[1]) : v[view == 0 ? 0 : 1]; };
        auto pickGain = [&] (const DisplayColumn& c)
        {
            if (view != 1) return c.gain[view == 0 ? 0 : 1];
            return std::abs (c.gain[0]) >= std::abs (c.gain[1]) ? c.gain[0] : c.gain[1];
        };
        auto lin2y = [&] (float lin) { return yFor (juce::Decibels::gainToDecibels (lin, -100.0f), r); };

        // entrada (preenchida)
        juce::Path inP, outP;
        inP.startNewSubPath (r.getRight(), r.getBottom());
        for (int k = 0; k < N; ++k)
        {
            const auto& c = colAt (k);
            inP.lineTo (r.getRight() - (float) k, lin2y (pick (c.in)));
        }
        inP.lineTo (r.getRight() - (float) N, r.getBottom());
        inP.closeSubPath();
        g.setColour (juce::Colour (0xff8c95a5).withAlpha (0.55f));
        g.fillPath (inP);

        // canal oposto em sobreposicao (LEFT/RIGHT view)
        if (view != 1)
        {
            juce::Path other;
            const int oc = view == 0 ? 1 : 0;
            for (int k = 0; k < N; ++k)
            {
                const auto& c = colAt (k);
                const float x = r.getRight() - (float) k, y = lin2y (c.in[oc]);
                if (k == 0) other.startNewSubPath (x, y); else other.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.strokePath (other, juce::PathStrokeType (1.0f));
        }

        // ganho: area entre entrada e saida
        for (int k = 0; k < N; ++k)
        {
            const auto& c = colAt (k);
            const float yi = lin2y (pick (c.in)), yo = lin2y (pick (c.out));
            const float gdb = pickGain (c);
            if (std::abs (yi - yo) < 0.5f || std::abs (gdb) < 0.05f) continue;
            g.setColour ((gdb > 0 ? silver::orange : silver::blue).withAlpha (0.85f));
            g.fillRect (r.getRight() - (float) k - 1.0f, juce::jmin (yi, yo), 1.0f, std::abs (yi - yo));
        }

        // saida (linha branca)
        for (int k = 0; k < N; ++k)
        {
            const auto& c = colAt (k);
            const float x = r.getRight() - (float) k, y = lin2y (pick (c.out));
            if (k == 0) outP.startNewSubPath (x, y); else outP.lineTo (x, y);
        }
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.strokePath (outP, juce::PathStrokeType (1.1f));

        // marcadores de batida
        for (int k = 0; k < N; ++k)
        {
            const auto& c = colAt (k);
            int flag = 0; float lvl = -100;
            if (view == 1) { for (int ch = 0; ch < 2; ++ch) if (c.beat[ch]) { flag = juce::jmax (flag, c.beat[ch]); lvl = juce::jmax (lvl, c.beatDb[ch]); } }
            else { const int ch = view == 0 ? 0 : 1; flag = c.beat[ch]; lvl = c.beatDb[ch]; }
            if (flag == 0) continue;
            const float x = r.getRight() - (float) k, y = yFor (lvl, r);
            juce::Path tri;
            tri.addTriangle (x - 3.5f, y - 7.0f, x + 3.5f, y - 7.0f, x, y - 1.5f);
            g.setColour (flag == 1 ? silver::orange : silver::lineLt.withAlpha (0.6f));
            g.fillPath (tri);
        }

        g.saveState();
        g.reduceClipRegion (getLocalBounds());
        drawLine (g, r, hi.get(), silver::lineLt, "HI THRESH", true, hover == 0 || drag == 0);
        drawLine (g, r, lo.get(), silver::lineLt.withAlpha (0.8f), "LO THRESH", true, hover == 1 || drag == 1);
        drawLine (g, r, tg.get(), silver::orange, "TARGET LVL", false, hover == 2 || drag == 2);
        g.restoreState();

        // indicadores
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        const float gr = gate.get();
        if (std::abs (gr) > 0.05f)
        {
            g.setColour (gr < 0 ? silver::blue : silver::orange);
            g.drawText ("GATE " + juce::String (gr, 1) + " dB", juce::Rectangle<float> (r.getX() + 6, r.getBottom() - 18, 140, 14),
                        juce::Justification::centredLeft);
        }
        g.setColour (silver::lineLt.withAlpha (0.55f));
        const char* names[3] = { proc.getMode() == drmp::MidSide ? "MID" : "LEFT", "SUM",
                                 proc.getMode() == drmp::MidSide ? "SIDE" : "RIGHT" };
        g.drawText (linked ? "STEREO" : names[view], juce::Rectangle<float> (r.getCentreX() - 50, r.getY() + 2, 100, 14),
                    juce::Justification::centred);

        g.setColour (silver::edge);
        g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);
    }

    // ---- mouse: arrastar linhas ----
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position.y);
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hit (e.position.y);
        if (drag >= 0) src (drag).begin();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        float db = juce::jlimit (dbBot, 0.0f, dbFor (e.position.y, plotArea()));
        db = std::round (db * 10.0f) / 10.0f;
        if (drag == 0) db = juce::jmax (db, lo.get());   // HI nunca abaixo do LO
        if (drag == 1) db = juce::jmin (db, hi.get());   // LO nunca acima do HI
        auto rr = src (drag).range();
        src (drag).set (juce::jlimit (rr.start, rr.end, db));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag >= 0) src (drag).end();
        drag = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position.y);
        if (h >= 0) src (h).setOnce (src (h).def());
    }

private:
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (8.0f, 10.0f).withTrimmedRight (24.0f); }
    float yFor (float db, juce::Rectangle<float> r) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (0.0f - db) / (0.0f - dbBot));
        return r.getY() + t * r.getHeight();
    }
    float dbFor (float y, juce::Rectangle<float> r) const { return -(y - r.getY()) / r.getHeight() * (0.0f - dbBot); }

    DualSource& src (int i) { return i == 0 ? hi : (i == 1 ? lo : tg); }

    int hit (float y)
    {
        auto r = plotArea();
        int best = -1; float bd = 7.0f;
        const int order[3] = { 2, 0, 1 };
        for (int i : order)
        {
            const float d = std::abs (y - yFor (src (i).get(), r));
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }

    void drawLine (juce::Graphics& g, juce::Rectangle<float> r, float db, juce::Colour c,
                   const juce::String& name, bool labelLeft, bool hot)
    {
        const float y = yFor (db, r);
        g.setColour (c.withAlpha (hot ? 1.0f : 0.85f));
        g.drawLine (r.getX(), y, r.getRight(), y, hot ? 2.0f : 1.2f);
        for (float x : { r.getX(), r.getRight() })
        {
            g.setColour (c);
            g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
            g.setColour (silver::display);
            g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.0f);
        }
        g.setColour (c);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        const auto txt = juce::String (db, 1) + " dB  " + name;
        const float ty = (y - 15.0f < r.getY()) ? y + 3.0f : y - 15.0f;
        const auto txtR = labelLeft ? juce::Rectangle<float> (r.getX() + 10, ty, 200, 13)
                                    : juce::Rectangle<float> (r.getRight() - 210, ty, 200, 13);
        g.drawText (labelLeft ? txt : name + "  " + juce::String (db, 1) + " dB", txtR,
                    labelLeft ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }

    DRMLevelerAudioProcessor& proc;
    DualSource& hi; DualSource& lo; DualSource& tg; DualSource& gate;
    std::vector<DisplayColumn> hist;
    int w = 0;
    int hover = -1, drag = -1;
    const float dbBot = -60.0f;
};
