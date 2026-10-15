#include "GlobalCtrlView.h"
#include "Skin.h"

#include "DtBlkFx.hpp"

namespace dtgui
{
GlobalCtrlView::GlobalCtrlView(DtBlkFx& e) : blkfx(e)
{
    // GetTile(rect, 6, 1): 410 / 6 = 68 px cells
    const int cw = kWidth / 6;
    auto cell = [](int i) { return juce::Rectangle<int>(i * cw, 0, cw, kGlobalH); };

    Component* order[] = { &mixback, &pwrmatch, &delay, &overlap, &blksync, &blksz };
    for (int i = 0; i < 6; ++i)
    {
        addAndMakeVisible(order[i]);
        order[i]->setBounds(cell(i));
    }

    auto redraw = [this] { repaint(); };
    for (auto* s : { &mixback, &delay, &overlap, &blksz })
        s->onStateChanged = redraw;
    pwrmatch.onStateChanged = redraw;
    blksync.onStateChanged = redraw;

    mixback.onValueChanged = [this](float, int m) { valueChanged(BlkFxParam::MIX_BACK, m); };
    delay.onValueChanged = [this](float, int m) { valueChanged(BlkFxParam::DELAY, m); };
    overlap.onValueChanged = [this](float, int m) { valueChanged(BlkFxParam::OVERLAP, m); };
    blksz.onValueChanged = [this](float, int m) { valueChanged(BlkFxParam::FFT_LEN, m); };
    pwrmatch.onToggle = [this](bool) {
        if (gesture) gesture(BlkFxParam::MIX_BACK, true);
        valueChanged(BlkFxParam::MIX_BACK, -1);
        if (gesture) gesture(BlkFxParam::MIX_BACK, false);
    };
    blksync.onToggle = [this](bool) {
        if (gesture) gesture(BlkFxParam::OVERLAP, true);
        valueChanged(BlkFxParam::OVERLAP, -1);
        if (gesture) gesture(BlkFxParam::OVERLAP, false);
    };

    auto gest = [this](PopupHSlider& s, int param) {
        s.onGestureBegin = [this, param] { if (gesture) gesture(param, true); };
        s.onGestureEnd = [this, param] { if (gesture) gesture(param, false); };
    };
    gest(mixback, BlkFxParam::MIX_BACK);
    gest(delay, BlkFxParam::DELAY);
    gest(overlap, BlkFxParam::OVERLAP);
    gest(blksz, BlkFxParam::FFT_LEN);

    blksz.setMenuSnap(true);

    // GlobalCtrl::onMouseDown: rebuild the delay & blksz menus before they are used
    delay.onMouseDownHook = [this] { updateMenus(); };
    blksz.onMouseDownHook = [this] { updateMenus(); };

    // menus that updateMenus() doesn't rebuild
    CharArray<64> str;
    for (int i = 0; i <= 100; i += 25)
    {
        const float v = (float) i * 0.01f;
        str << spr_percent(v);
        mixback.addMenuEntry(v, juce::String(str.data));
    }
    overlap.addMenuEntry(0.0f, "minimum");
    for (int i = 10; i <= 80; i += 10)
    {
        str << i << "%";
        overlap.addMenuEntry((float) i * (0.01f / 0.85f), juce::String(str.data));
    }
    overlap.addMenuEntry(1.0f, "maximum");

    updateMenus();
}

void GlobalCtrlView::updateMenus()
{
    CharArray<80> str;

    if (!juce::exactlyEqual(blkfx.getSampleRate(), sampleRate) || !juce::exactlyEqual(blkfx.getSampsPerBeat(), sampsPerBeat))
    {
        sampleRate = std::max(blkfx.getSampleRate(), 1.0f);
        sampsPerBeat = std::max(blkfx.getSampsPerBeat(), 1.0f);

        const float beatPresets[] = { .25f, 1 / 3.0f, 0.5f, 2 / 3.0f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
        const int nb = (int) (sizeof(beatPresets) / sizeof(beatPresets[0]));
        delayMenuUnits.resize((size_t) nb + 2);
        while (delay.getNumMenuEntries() < nb + 2)
            delay.addMenuEntry(-1, "");

        for (int i = 0; i < nb; ++i)
        {
            const float beats = beatPresets[i];
            const float samps = beats * sampsPerBeat;
            const float sec = samps / sampleRate;
            str << sprf("%.2f", beats) << " beats, " << "(" << (int) samps << " samps, " << sprnum(sec, 1e-3f) << "sec)";
            delay.setMenuEntry(i + 2, BlkFxParam::Delay::beats(beats).f_part, juce::String(str.data));
            delayMenuUnits[(size_t) i + 2] = BlkFxParam::Delay::BEATS;
        }

        while (blksz.getNumMenuEntries() < NUM_FFT_SZ)
            blksz.addMenuEntry(-1, "");
        for (int i = 0; i < NUM_FFT_SZ; ++i)
        {
            const int samps = g_fft_sz[i];
            const float sec = (float) samps / sampleRate;
            const float beats = (float) samps / sampsPerBeat;
            str << sprnum(sec, 1e-3f) << "sec " << samps << " samps, " << sprf("(%.2f beats)", beats);
            blksz.setMenuEntry(i, (float) i / (float) NUM_FFT_SZ, juce::String(str.data));
        }
    }

    // entry 0 toggles between msec & beats
    BlkFxParam::Delay delayParam;
    delayParam.setUnits(delayUnits);
    delayParam.f_part = delay.getValue();
    const float delaySamps = (float) (blkfx.getDelaySamps(delayParam) - blkfx._initial_delay);
    const float delaySecs = delaySamps / sampleRate;
    const float delayBeats = delaySamps / sampsPerBeat;
    float delayValue;
    if (delayUnits == BlkFxParam::Delay::MSEC)
    {
        delayValue = BlkFxParam::Delay::beats(delayBeats).f_part;
        str << sprf("%.2f beats", delayBeats) << "(" << (int) delaySamps << " samps, " << sprnum(delaySecs, 1e-3f) << "sec)";
        delayMenuUnits[0] = BlkFxParam::Delay::BEATS;
    }
    else
    {
        delayValue = BlkFxParam::Delay::msec(delaySecs * 1000.0f).f_part;
        str << sprnum(delaySecs, 1e-3f) << "sec, " << (int) delaySamps << " samps, " << sprf("(%.2f beats)", delayBeats);
        delayMenuUnits[0] = BlkFxParam::Delay::MSEC;
    }
    delay.setMenuEntry(0, delayValue, juce::String(str.data));

    // entry 1 = current blk len
    const int fftSamps = g_fft_sz[getBlkSzPlan()];
    const float fftSecs = (float) fftSamps / sampleRate;
    const float fftBeats = (float) fftSamps / sampsPerBeat;
    str << sprnum(fftSecs, 1e-3f) << "sec, " << fftSamps << " samps, " << sprf("(%.2f beats)", fftBeats);
    delay.setMenuEntry(1, BlkFxParam::Delay::msec(fftSecs * 1000.0f).f_part, juce::String(str.data));
    delayMenuUnits[1] = BlkFxParam::Delay::MSEC;
}

void GlobalCtrlView::valueChanged(int paramNum, int menuIdx)
{
    float paramVal = 0;
    switch (paramNum)
    {
        case BlkFxParam::MIX_BACK:
            paramVal = BlkFxParam::getMixbackParam(mixback.getValue(), pwrmatch.getBoolValue());
            break;

        case BlkFxParam::DELAY:
        {
            if (menuIdx >= 0 && menuIdx < (int) delayMenuUnits.size())
            {
                delayUnits = delayMenuUnits[(size_t) menuIdx];
                repaint();
            }
            BlkFxParam::Delay d;
            d.setUnits(delayUnits);
            d.f_part = delay.getValue();
            paramVal = d;
        }
        break;

        case BlkFxParam::FFT_LEN:
            paramVal = BlkFxParam::getFFTLenParam(getBlkSzPlan());
            break;

        case BlkFxParam::OVERLAP:
            paramVal = BlkFxParam::getOverlapParam(overlap.getValue(), blksync.getBoolValue());
            break;

        default:
            return;
    }
    if (sendParam)
        sendParam(paramNum, paramVal);
    repaint();
}

void GlobalCtrlView::setParameter(int index, float v)
{
    switch (index)
    {
        case BlkFxParam::MIX_BACK:
            mixback.setValue(BlkFxParam::getMixBackFrac(v));
            pwrmatch.setBoolValue(BlkFxParam::getPwrMatch(v) != 0.0f, false);
            break;
        case BlkFxParam::DELAY:
        {
            BlkFxParam::Delay d = v;
            delay.setValue(d.f_part);
            delayUnits = d.getUnits();
        }
        break;
        case BlkFxParam::FFT_LEN:
            blksz.setValue((float) BlkFxParam::getPlan(v) / (float) NUM_FFT_SZ);
            break;
        case BlkFxParam::OVERLAP:
            overlap.setValue(BlkFxParam::getOverlapPart(v));
            blksync.setBoolValue(BlkFxParam::getBlkSync(v), false);
            break;
        default:
            break;
    }
    repaint();
}

void GlobalCtrlView::paint(juce::Graphics& g)
{
    // background (blend without source alpha = straight copy)
    g.drawImageAt(Skin::get().globBg, 0, 0);

    auto col = [](bool inactive) { return inactive ? normalTxt() : focusTxt(); };
    const auto black = juce::Colours::black;
    CharArray<32> str;

    str << RndToInt(mixback.getValue() * 100.0f) << "%";
    outlineText(g, juce::String(str.data), textRect(mixback), col(mixback.getState() == PopupHSlider::INACTIVE), black);

    str << (pwrmatch.getTentativeValue() ? "match" : "filter");
    outlineText(g, juce::String(str.data), textRect(pwrmatch), col(pwrmatch.getState() == OwnerToggle::INACTIVE), black);

    BlkFxParam::Delay delayParam;
    delayParam.setUnits(delayUnits);
    delayParam.f_part = delay.getValue();
    const float delaySamps = (float) (blkfx.getDelaySamps(delayParam) - blkfx._initial_delay);
    if (delayParam.i_part == delayParam.BEATS)
        str << sprf("%.2f beats", delaySamps / blkfx.getSampsPerBeat());
    else
        str << sprnum(delaySamps / blkfx.getSampleRate()) << "sec";
    outlineText(g, juce::String(str.data), textRect(delay), col(delay.getState() == PopupHSlider::INACTIVE), black);

    int guessFftN, guessTimeN;
    blkfx.guessFFTLen(guessFftN, guessTimeN);

    const int sampsFwd = (int) BlkFxParam::getBlkShiftFwd(overlap.getValue(), guessTimeN);
    const float overlapFrac = 1.0f - ((float) sampsFwd / (float) guessTimeN);
    str << RndToInt(overlapFrac * 100.0f) << "%";
    outlineText(g, juce::String(str.data), textRect(overlap), col(overlap.getState() == PopupHSlider::INACTIVE), black);

    str << (blksync.getTentativeValue() ? "on" : "off");
    outlineText(g, juce::String(str.data), textRect(blksync), col(blksync.getState() == OwnerToggle::INACTIVE), black);

    const int fftLen = g_fft_sz[getBlkSzPlan()];
    const float fftSecs = (float) fftLen / blkfx.getSampleRate();
    str << sprnum(fftSecs) << "sec" << ((float) fftLen > delaySamps ? " *" : "");
    outlineText(g, juce::String(str.data), textRect(blksz), col(blksz.getState() == PopupHSlider::INACTIVE), black);
}
} // namespace dtgui
