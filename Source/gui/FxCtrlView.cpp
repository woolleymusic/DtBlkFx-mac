#include "FxCtrlView.h"
#include "Skin.h"

#include "DtBlkFx.hpp"

namespace dtgui
{
FxCtrlView::FxCtrlView(DtBlkFx& e, int set, SpectrogramView* s0, SpectrogramView* s1)
    : blkfx(e), fxSet(set), paramOffset(BlkFxParam::paramOffs(set)), sgram { s0, s1 }
{
    auto parts = splitH({ 0, 0, kWidth, kFxRowH }, { 1.0f, 1.0f, .6f, 1.2f, 1.2f });
    juce::Component* ctrls[] = { &freq[0], &freq[1], &amp, &fxtype, &fxval };
    for (int i = 0; i < 5; ++i)
    {
        addAndMakeVisible(ctrls[i]);
        ctrls[i]->setBounds(parts[(size_t) i]);
    }

    auto redraw = [this] { repaint(); };
    for (auto* s : { &freq[0], &freq[1], &amp, &fxval })
    {
        s->onStateChanged = redraw;
        s->afterMouse = [this](juce::ModifierKeys m) { afterMouse(m); };
        s->afterExit = [this] { afterExit(); };
        s->onValueChanged = [this, s](float v, int menuIdx) { valueChanged(s, v, menuIdx); };
    }
    fxtype.onStateChanged = [this] {
        repaint();
        doHiliteSgrams();
    };
    fxtype.onSelect = [this](int idx) { valueChanged(&fxtype, (float) idx, idx); };

    // gestures: tag = parameter index
    auto gest = [this](PopupHSlider& s, int param) {
        s.onGestureBegin = [this, param] { if (gesture) gesture(param, true); };
        s.onGestureEnd = [this, param] { if (gesture) gesture(param, false); };
    };
    for (int i = 0; i < 2; ++i)
        gest(freq[i], paramOffset + BlkFxParam::FX_FREQ_A + i);
    gest(amp, paramOffset + BlkFxParam::FX_AMP);
    gest(fxval, paramOffset + BlkFxParam::FX_VAL);

    amp.onMouseDownHook = [this] { updateAmpMenu(); };
    fxval.onMouseDownHook = [this] { updateFxValMenu(); };

    // drag both frequencies: right button, or shift + left button (FxCtrl::mouseInterceptBefore)
    for (int i = 0; i < 2; ++i)
    {
        freq[i].interceptDown = [this](const juce::MouseEvent& ev) {
            if (!(ev.mods.isPopupMenu() || (ev.mods.isLeftButtonDown() && ev.mods.isShiftDown())))
                return false;
            dragBoth = DRAG_BOTH_ACTIVE;
            repaint();
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            const float mx = (float) ev.getEventRelativeTo(this).x;
            for (int j = 0; j < 2; ++j)
            {
                dragBothRescale[j].init((float) getWidth() - 10.0f);
                dragBothRescale[j].setOffs(freq[j].getValue(), mx);
                if (gesture)
                    gesture(paramOffset + BlkFxParam::FX_FREQ_A + j, true);
            }
            doHiliteSgrams();
            return true;
        };
        freq[i].interceptDrag = [this](const juce::MouseEvent& ev) {
            const float mx = (float) ev.getEventRelativeTo(this).x;
            for (int j = 0; j < 2; ++j)
            {
                const float v = dragBothRescale[j].getFrac(mx);
                freq[j].setValue(v, true);
            }
            doHiliteSgrams();
        };
        freq[i].interceptUp = [this](const juce::MouseEvent&) {
            setMouseCursor(juce::MouseCursor::NormalCursor);
            dragBoth = DRAG_BOTH_INACTIVE;
            for (int j = 0; j < 2; ++j)
                if (gesture)
                    gesture(paramOffset + BlkFxParam::FX_FREQ_A + j, false);
            repaint();
            doHiliteSgrams();
        };
    }

    // effect menu, skipping "DoNotUse" entries
    const int maxEffects = (int) BlkFxParam::getEffectType(1.0f) + 1;
    mapEffectToMenu.assign((size_t) maxEffects, -1);
    for (int i = 0; i < g_num_fx_1_0; ++i)
    {
        FxRun1_0* fx = GetFxRun1_0(i);
        if (strcmp(fx->name(), "DoNotUse") == 0)
            continue;
        mapEffectToMenu[(size_t) i] = (int) mapMenuToParam.size();
        mapMenuToParam.push_back(BlkFxParam::getEffectTypeInv(i));
        fxtype.addEntry(fx->name());
    }

    updateAmpMenu();
    updateFxValMenu();
}

juce::Component* FxCtrlView::getCtrlFocus()
{
    if (freq[0].getState() != PopupHSlider::INACTIVE)
        return &freq[0];
    if (freq[1].getState() != PopupHSlider::INACTIVE)
        return &freq[1];
    if (amp.getState() != PopupHSlider::INACTIVE)
        return &amp;
    if (fxtype.getState() != OwnerMenu::INACTIVE)
        return &fxtype;
    if (fxval.getState() != PopupHSlider::INACTIVE)
        return &fxval;
    return nullptr;
}

void FxCtrlView::afterMouse(juce::ModifierKeys mods)
{
    if (dragBoth == DRAG_BOTH_FOCUS && !mods.isShiftDown())
    {
        dragBoth = DRAG_BOTH_INACTIVE;
        repaint();
    }
    if (mods.isShiftDown() && (freq[0].getState() == PopupHSlider::FOCUS || freq[1].getState() == PopupHSlider::FOCUS)
        && dragBoth != DRAG_BOTH_ACTIVE)
    {
        dragBoth = DRAG_BOTH_FOCUS;
        repaint();
    }
    doHiliteSgrams();
}

void FxCtrlView::afterExit()
{
    sgram[0]->setInfoFreq(-1);
    sgram[0]->clrHilite();
    sgram[1]->clrHilite();
    if (dragBoth != DRAG_BOTH_INACTIVE && dragBoth != DRAG_BOTH_ACTIVE)
    {
        dragBoth = DRAG_BOTH_INACTIVE;
        repaint();
    }
}

void FxCtrlView::doHiliteSgrams()
{
    juce::Component* active = getCtrlFocus();
    if (active == nullptr && dragBoth == DRAG_BOTH_INACTIVE)
        return;

    SpectrogramView::HiliteArray hilite;
    hilite.fill(-1);

    float val[2];
    int pix[2];
    for (int i = 0; i < 2; ++i)
    {
        val[i] = freq[i].getValue();
        pix[i] = sgram[0]->fracToPix(val[i]);
    }
    const int endPix = sgram[0]->fracToPix(1.0f) + 1;

    if (dragBoth != DRAG_BOTH_INACTIVE)
    {
        sgram[0]->setInfoFreq(-1);
        active = nullptr;
    }
    else
    {
        for (int i = 0; i < 2; ++i)
            if (active == &freq[i])
                sgram[0]->setInfoFreq(BlkFxParam::getHz(val[i]));
    }

    const int diff = pix[1] - pix[0];
    if (diff > 2 && active == &freq[0])
        hilite = { pix[0], pix[0] + 1, pix[0] + 2, pix[1] + 1, -1, -1 };
    else if (diff > 2 && active == &freq[1])
        hilite = { pix[0], pix[1] - 1, pix[1], pix[1] + 1, -1, -1 };
    else if (diff < -2 && active == &freq[0])
        hilite = { 0, pix[1] + 1, pix[0], pix[0] + 1, pix[0] + 2, endPix };
    else if (diff < -2 && active == &freq[1])
        hilite = { 0, pix[1] - 1, pix[1], pix[1] + 1, pix[0], endPix };
    else if (diff >= 0)
        hilite = { pix[0], pix[1], -1, -1, -1, -1 };
    else
        hilite = { 0, pix[1] + 1, pix[0], endPix, -1, -1 };

    for (auto* s : sgram)
        s->setHilite(hilite);
}

void FxCtrlView::updateFxValMenu()
{
    FxRun1_0* fx = blkfx._fx1_0[fxSet].getFxRun();
    if (fx == fxvalMenuType)
        return;
    fxvalMenuType = fx;
    fxval.removeAllMenuEntries();
    for (int i = 0;; ++i)
    {
        const char* s = fx->getValueName(i);
        if (s == nullptr)
            break;
        fxval.addMenuEntry(-1, s);
    }
}

void FxCtrlView::updateAmpMenu()
{
    FxRun1_0* fx = blkfx._fx1_0[fxSet].getFxRun();
    const int mode = fx->ampMixMode() ? AMP_MENU_PERCENT : AMP_MENU_DB;
    if (mode == ampMenuMixmode)
        return;
    ampMenuMixmode = mode;
    amp.removeAllMenuEntries();

    CharArray<64> str;
    float dB;
    if (mode == AMP_MENU_DB)
    {
        amp.addMenuEntry(0.0f, "-inf");
        dB = -40;
    }
    else
    {
        for (int i = 0; i <= 100; i += 25)
        {
            str << i << "%";
            amp.addMenuEntry(BlkFxParam::getAmpParam0dB() * 0.01f * (float) i, juce::String(str.data));
        }
        dB = 10.0f;
    }
    while (dB <= 40.0f)
    {
        str << (int) dB << " dB";
        amp.addMenuEntry(BlkFxParam::getAmpParam(dB), juce::String(str.data));
        dB += 10.0f;
    }
}

void FxCtrlView::valueChanged(juce::Component* ctrl, float v, int menuIdx)
{
    int tag = -1;
    float paramVal = v;
    if (ctrl == &freq[0])
        tag = paramOffset + BlkFxParam::FX_FREQ_A;
    else if (ctrl == &freq[1])
        tag = paramOffset + BlkFxParam::FX_FREQ_B;
    else if (ctrl == &amp)
        tag = paramOffset + BlkFxParam::FX_AMP;
    else if (ctrl == &fxval)
    {
        tag = paramOffset + BlkFxParam::FX_VAL;
        if (menuIdx >= 0)
            paramVal = blkfx._fx1_0[fxSet].getFxRun()->getValue(menuIdx, paramVal);
    }
    else if (ctrl == &fxtype)
    {
        tag = paramOffset + BlkFxParam::FX_TYPE;
        if (menuIdx < 0 || menuIdx >= (int) mapMenuToParam.size())
            return;
        paramVal = mapMenuToParam[(size_t) menuIdx];
        fxtype.setValue(menuIdx);
    }
    if (tag < 0)
        return;

    // sliders (drag, wheel and their own menus) already wrap their changes in a gesture
    const bool needGesture = ctrl == &fxtype;
    if (needGesture && gesture)
        gesture(tag, true);
    if (sendParam)
        sendParam(tag, paramVal);
    if (needGesture && gesture)
        gesture(tag, false);

    if (ctrl == &fxval && menuIdx >= 0)
        fxval.setValue(paramVal);

    repaint();
    if (onChanged)
        onChanged();
}

void FxCtrlView::setParameter(int fxParam, float v)
{
    switch (fxParam)
    {
        case BlkFxParam::FX_FREQ_A:
            freq[0].setValue(v);
            doHiliteSgrams();
            break;
        case BlkFxParam::FX_FREQ_B:
            freq[1].setValue(v);
            doHiliteSgrams();
            break;
        case BlkFxParam::FX_AMP:
            amp.setValue(v);
            break;
        case BlkFxParam::FX_TYPE:
        {
            const int t = (int) BlkFxParam::getEffectType(v);
            fxtype.setValue(t >= 0 && t < (int) mapEffectToMenu.size() ? mapEffectToMenu[(size_t) t] : -1);
        }
        break;
        case BlkFxParam::FX_VAL:
            fxval.setValue(v);
            break;
        default:
            break;
    }
    repaint();
}

void FxCtrlView::paint(juce::Graphics& g)
{
    const auto& skin = Skin::get();
    const auto& bg = skin.fxBg;
    const int th = kFxRowH;
    auto tile = [](int i) { return juce::Rectangle<int>(0, i * th, kWidth, th); };

    FxRun1_0* fx = blkfx._fx1_0[fxSet].getFxRun();

    enum
    {
        NORMAL_EFFECT,
        MASK_EFFECT,
        MASK_NO_EFFECT
    } fxState = fx->isMask() ? MASK_EFFECT : NORMAL_EFFECT;
    if (fxState == MASK_EFFECT)
    {
        if (fxSet + 1 >= BlkFxParam::NUM_FX_SETS)
            fxState = MASK_NO_EFFECT;
        else if (blkfx._fx1_0[fxSet + 1].getFxRun()->isMask())
            fxState = MASK_NO_EFFECT;
    }

    // background: 0 = inactive, 1 = row selected, 2 = control selected, 3 = amp, 4 = freq scale
    juce::Component* active = getCtrlFocus();
    if (fxState != MASK_NO_EFFECT && (active != nullptr || dragBoth != DRAG_BOTH_INACTIVE))
    {
        juce::Rectangle<int> a = dragBoth != DRAG_BOTH_INACTIVE ? freq[0].getBounds().getUnion(freq[1].getBounds())
                                                                : active->getBounds();
        const int edges[4] = { 0, a.getX(), a.getRight(), kWidth };
        const int srcTile[3] = { 1, 2, 1 };
        for (int i = 0; i < 3; ++i)
        {
            const int x0 = edges[i], x1 = edges[i + 1];
            blit(g, bg, { x0, srcTile[i] * th, x1 - x0, th }, { x0, 0 });
        }
    }
    else
    {
        blit(g, bg, tile(0), { 0, 0 });
    }

    // masks: hide the 1 px separator at the bottom by repeating the line above it
    if (fxState == MASK_EFFECT)
        blit(g, bg, { 0, th - 2, kWidth, 1 }, { 0, th - 1 });

    // amp overlay
    const float ampV = amp.getValue();
    {
        auto r = tile(3);
        const int right = RndToInt(lin_interp(ampV, (float) r.getX() + 5, (float) r.getRight() - 5));
        int alpha = 255;
        if (!fx->paramUsed(BlkFxParam::FX_AMP))
            alpha = 63;
        else if (amp.getState() == PopupHSlider::INACTIVE)
            alpha = 127;
        blit(g, bg, r.withRight(right), { r.getX(), 0 }, alpha);
    }

    // frequency scale
    float fv[2];
    int fpos[2];
    const auto fr = tile(4);
    for (int i = 0; i < 2; ++i)
    {
        fv[i] = freq[i].getValue();
        fpos[i] = RndToInt(lin_interp(fv[i], (float) fr.getX() + 5, (float) fr.getRight() - 5));
    }
    const int freqAlpha = fx->paramUsed(BlkFxParam::FX_FREQ_A) || fx->paramUsed(BlkFxParam::FX_FREQ_B) ? 255 : 127;
    if (fv[0] <= fv[1])
    {
        auto r = fr.withLeft(fpos[0]).withRight(fpos[1]);
        blit(g, bg, r, { r.getX(), 0 }, freqAlpha);
    }
    else
    {
        auto r = fr.withRight(fpos[1]);
        blit(g, bg, r, { r.getX(), 0 }, freqAlpha);
        r = fr.withLeft(fpos[0]);
        blit(g, bg, r, { r.getX(), 0 }, freqAlpha);
    }

    // text
    const auto black = juce::Colours::black;
    CharArray<256> str;
    for (int i = 0; i < 2; ++i)
    {
        const bool act = freq[i].getState() > PopupHSlider::INACTIVE || dragBoth != DRAG_BOTH_INACTIVE;
        const auto col = act ? focusTxt() : normalTxt();
        if (!fx->paramUsed(BlkFxParam::FX_FREQ_A + i))
            outlineText(g, "-", freq[i].getBounds(), col, black);
        else
        {
            const float hz = BlkFxParam::getHz(fv[i]);
            str << sprnum(hz, /*min unit*/ 1.0f) << "Hz";
            outlineText(g, juce::String(str.data), freq[i].getBounds(), col, black);
        }
    }

    {
        const auto col = amp.getState() == PopupHSlider::INACTIVE ? normalTxt() : focusTxt();
        const bool mixMode = fx->ampMixMode();
        const float ampMult = BlkFxParam::getEffectAmpMult(ampV, mixMode);
        if (!fx->paramUsed(BlkFxParam::FX_AMP))
            str << "-";
        else if (mixMode && ampMult <= 1.0f)
            str << sprf("%.1f %%", ampMult * 100.0f);
        else if (ampMult > 0)
            str << sprf("%.1f dB", BlkFxParam::getEffectAmp(ampV));
        else
            str << "-inf dB";
        outlineText(g, juce::String(str.data), amp.getBounds(), col, black);
    }

    outlineText(g, fx->name(), fxtype.getBounds(), fxtype.getState() == OwnerMenu::INACTIVE ? normalTxt() : focusTxt(), black);

    {
        const auto col = fxval.getState() != PopupHSlider::INACTIVE ? focusTxt() : normalTxt();
        Clear(str);
        fx->dispVal(&blkfx._fx1_0[fxSet], str, fxval.getValue());
        outlineText(g, juce::String(str.data), fxval.getBounds(), col, black);
    }
}
} // namespace dtgui
