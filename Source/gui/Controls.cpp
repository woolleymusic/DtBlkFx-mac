#include "Controls.h"
#include "Skin.h"

#include <BinaryData.h>

#include <limits>

namespace dtgui
{
//==============================================================================
const Skin& Skin::get()
{
    static const Skin skin = [] {
        Skin s;
        auto load = [](const char* data, int size) { return juce::ImageCache::getFromMemory(data, size); };
        s.fxBg = load(BinaryData::stereo_fx_bg_png, BinaryData::stereo_fx_bg_pngSize);
        s.globBg = load(BinaryData::stereo_global_ctrl_png, BinaryData::stereo_global_ctrl_pngSize);
        s.splash = load(BinaryData::stereo_splash_png, BinaryData::stereo_splash_pngSize);
        s.hsBg[PopupHSlider::COARSE] = load(BinaryData::stereo_hslider_bg_coarse_png, BinaryData::stereo_hslider_bg_coarse_pngSize);
        s.hsBg[PopupHSlider::FINE] = load(BinaryData::stereo_hslider_bg_fine_png, BinaryData::stereo_hslider_bg_fine_pngSize);
        s.hsKnob[PopupHSlider::COARSE] = load(BinaryData::stereo_hslider_knob_coarse_png, BinaryData::stereo_hslider_knob_coarse_pngSize);
        s.hsKnob[PopupHSlider::FINE] = load(BinaryData::stereo_hslider_knob_fine_png, BinaryData::stereo_hslider_knob_fine_pngSize);
        return s;
    }();
    return skin;
}

//==============================================================================
// both popup sliders (the original used two layered windows per slider, background + knob, in
// screen coordinates). This is one transparent, click-through child of the editor's content, so
// the bars and knobs move together, scale with the editor, stay inside it, and whatever they
// covered is repainted when they move or close.
class SliderPopupOverlay : public juce::Component
{
public:
    struct Bar
    {
        juce::Image bg, knob;
        int bgI = 0, knobI = 0;
        juce::Rectangle<int> bgR, knobR; // in the owning control's coordinates
    };

    SliderPopupOverlay()
    {
        setInterceptsMouseClicks(false, false);
        setOpaque(false);
        setWantsKeyboardFocus(false);
    }

    // bars drawn with "control to host" transform t
    void update(const Bar (&b)[2], juce::AffineTransform t)
    {
        const auto before = drawn;
        for (int i = 0; i < 2; ++i)
            bars[i] = b[i];
        xf = t;
        drawn = {};
        for (auto& bar : bars)
            drawn = drawn.getUnion(toHostRect(bar.bgR)).getUnion(toHostRect(bar.knobR));
        everDrawn = everDrawn.getUnion(drawn);
        repaint(before.getUnion(drawn));
    }

    juce::Rectangle<int> toHostRect(juce::Rectangle<int> r) const
    {
        // one extra pixel all round for the resampling filter at fractional scales
        return r.toFloat().transformedBy(xf).getSmallestIntegerContainer().expanded(1);
    }
    juce::Rectangle<int> barBounds(int i) const { return bars[i].bgR.toFloat().transformedBy(xf).getSmallestIntegerContainer(); }
    juce::Rectangle<int> knobBounds(int i) const { return bars[i].knobR.toFloat().transformedBy(xf).getSmallestIntegerContainer(); }
    juce::Rectangle<int> getEverDrawn() const { return everDrawn; }

    void paint(juce::Graphics& g) override
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.addTransform(xf);
        for (auto& b : bars)
        {
            if (!b.bg.isValid() || !b.knob.isValid())
                continue;
            const int th = b.bg.getHeight() / 2, kh = b.knob.getHeight() / 2;
            g.drawImage(b.bg, b.bgR.getX(), b.bgR.getY(), b.bgR.getWidth(), b.bgR.getHeight(), 0, b.bgI * th,
                        b.bg.getWidth(), th);
            g.drawImage(b.knob, b.knobR.getX(), b.knobR.getY(), b.knobR.getWidth(), b.knobR.getHeight(), 0,
                        b.knobI * kh, b.knob.getWidth(), kh);
        }
    }

private:
    Bar bars[2];
    juce::AffineTransform xf;
    juce::Rectangle<int> drawn, everDrawn;
};

//==============================================================================
// DtPopupHSliderInternal
void PopupHSlider::Internal::create(int top, int width, int height, float mag)
{
    magnify = mag;
    bgRect = { 0, top, width, height };
    rescale.init((float) width, mag);
}

void PopupHSlider::Internal::shift(float v, float pixel)
{
    rescale.setOffs(v, pixel);
    const float leftVal = juce::jlimit(0.0f, 1.0f - 1.0f / magnify, v - 0.5f / magnify);
    bgRect.setX(RndToInt(getPixel(leftVal)));
}

void PopupHSlider::Internal::translate(int d, bool withMapping)
{
    bgRect.translate(d, 0);
    if (withMapping)
        rescale.setOffs(rescale.getLeft() + (float) d);
}

int PopupHSlider::Internal::fitDelta(int lo, int hi, int margin) const
{
    const int l = bgRect.getX() - margin, r = bgRect.getRight() + margin;
    if (r - l > hi - lo)
        return lo - l; // cannot fit: keep the left edge in
    if (l < lo)
        return lo - l;
    if (r > hi)
        return hi - r;
    return 0;
}

float PopupHSlider::Internal::drag(int pixel)
{
    const float pixelF = (float) pixel;
    const float v = getValue(pixelF);
    if (pixel < bgRect.getX())
        bgRect.setX(pixel);
    else if (pixel >= bgRect.getRight())
        bgRect.setX(pixel + 1 - bgRect.getWidth());
    else
        return v;
    rescale.setOffs(v, pixelF);
    return v;
}

//==============================================================================
PopupHSlider::PopupHSlider() {}
PopupHSlider::~PopupHSlider() { closePopups(); }

void PopupHSlider::setState(State s)
{
    if (s == state)
        return;
    state = s;
    if (onStateChanged)
        onStateChanged();
}

void PopupHSlider::setValue(float v, bool sendToListener)
{
    v = juce::jlimit(0.0f, 1.0f, v);
    if (juce::exactlyEqual(v, value))
        return;
    value = v;
    if (sendToListener && onValueChanged)
        onValueChanged(value, -1);
    if (onStateChanged)
        onStateChanged();
}

int PopupHSlider::findClosestMenuIdx(float v) const
{
    int closest = -1;
    float closestDiff = 1e20f;
    for (int i = 0; i < (int) menu.size(); ++i)
    {
        const float d = std::abs(v - menu[(size_t) i].value);
        if (d < closestDiff)
        {
            closest = i;
            closestDiff = d;
        }
    }
    return closest;
}

void PopupHSlider::mouseEnter(const juce::MouseEvent& e) { mouseMove(e); }

void PopupHSlider::mouseExit(const juce::MouseEvent&)
{
    if (state < PRE_DRAG && state != MENU_OPEN)
        setState(INACTIVE);
    if (afterExit)
        afterExit();
}

void PopupHSlider::mouseMove(const juce::MouseEvent& e)
{
    if (state != MENU_OPEN)
        setState(FOCUS);
    if (afterMouse)
        afterMouse(e.mods);
}

void PopupHSlider::modifierKeysChanged(const juce::ModifierKeys& mods)
{
    if (afterMouse && isMouseOver())
        afterMouse(mods);
}

void PopupHSlider::mouseDown(const juce::MouseEvent& e)
{
    if (onMouseDownHook)
        onMouseDownHook();

    if (interceptDown && interceptDown(e))
    {
        intercepted = true;
        return;
    }

    clickPos = e.getPosition();
    if (e.mods.isPopupMenu())
        openMenu();
    else
        sliderMouseClick(e);

    if (afterMouse)
        afterMouse(e.mods);
}

namespace
{
constexpr int IMAGE_BORDER = 2; // 2 pixel border around the popup images (sliderMouseClick)
}

juce::Component* PopupHSlider::findPopupHost()
{
    for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
        if ((bool) p->getProperties()[popupHostProperty])
            return p;
    return getParentComponent() != nullptr ? getTopLevelComponent() : nullptr;
}

juce::AffineTransform PopupHSlider::toHost() const
{
    auto* host = overlayHost.getComponent();
    if (host == nullptr)
        return {};
    const auto o = host->getLocalPoint(this, juce::Point<float>());
    const auto x = host->getLocalPoint(this, juce::Point<float>(1000.0f, 0.0f));
    return juce::AffineTransform::scale((x.x - o.x) / 1000.0f).translated(o);
}

void PopupHSlider::fitInside(int i, bool withMapping)
{
    const int margin = Skin::get().hsKnob[i].getWidth() / 2; // a knob at either end stays inside too
    const int d = slider[i].fitDelta(area.getX(), area.getRight(), margin);
    if (d != 0)
        slider[i].translate(d, withMapping);
}

void PopupHSlider::sliderMouseClick(const juce::MouseEvent& e)
{
    if (state >= MENU_OPEN)
        return;
    setState(PRE_DRAG);

    closePopups();
    auto* host = findPopupHost();
    overlayHost = host;
    area = host != nullptr ? getLocalArea(host, host->getLocalBounds())
                           : juce::Rectangle<int>(-100000, -100000, 200000, 200000);

    const auto& skin = Skin::get();
    int y[2];
    y[FINE] = 0 - skin.hsBg[FINE].getHeight() / 2 + IMAGE_BORDER;
    y[COARSE] = getHeight() - IMAGE_BORDER;
    const float magnify[2] = { 1.0f, 1.0f / fineScaleFrac };

    for (int i = 0; i < 2; ++i)
        slider[i].create(y[i], skin.hsBg[i].getWidth() - IMAGE_BORDER * 2,
                         skin.hsBg[i].getHeight() / 2 - IMAGE_BORDER * 2, magnify[i]);

    // fine above, coarse below, as the original. Where one does not fit inside the editor (top and
    // bottom rows) it goes on the far side of the other one, so neither covers the clicked value
    {
        auto& f = slider[FINE].bgRect;
        auto& c = slider[COARSE].bgRect;
        const int B = IMAGE_BORDER;
        const bool fineFits = f.getY() - B >= area.getY();
        const bool coarseFits = c.getBottom() + B <= area.getBottom();
        if (!fineFits && coarseFits)
            f.setY(c.getBottom() + 2 * B); // top row: cell, coarse, fine
        else if (fineFits && !coarseFits)
            c.setY(f.getY() - 2 * B - c.getHeight()); // bottom row: coarse, fine, cell
        for (auto* r : { &f, &c }) // last resort for an editor smaller than the bars
            r->setY(juce::jlimit(area.getY() + B, juce::jmax(area.getY() + B, area.getBottom() - B - r->getHeight()), r->getY()));
    }

    placeBars(e.x);
    updatePopups();
}

void PopupHSlider::placeBars(int mouseX)
{
    const auto& skin = Skin::get();
    // the original put the current value under the mouse on both bars. Keep that when it fits,
    // otherwise slide both bars by the smallest amount that keeps them inside, and map the mouse
    // with the same offset
    for (int i = 0; i < 2; ++i)
        slider[i].shift(value, (float) mouseX);
    int dLo = std::numeric_limits<int>::min(), dHi = std::numeric_limits<int>::max();
    for (int i = 0; i < 2; ++i)
    {
        const int m = skin.hsKnob[i].getWidth() / 2;
        dLo = juce::jmax(dLo, area.getX() + m - slider[i].bgRect.getX());
        dHi = juce::jmin(dHi, area.getRight() - m - slider[i].bgRect.getRight());
    }
    xOffs = dLo <= dHi ? juce::jlimit(dLo, dHi, 0) : 0;
    for (int i = 0; i < 2; ++i)
        slider[i].shift(value, (float) (mouseX + xOffs));
    if (dLo > dHi) // editor narrower than both bars allow (never at the normal size)
    {
        fitInside(COARSE, true);
        fitInside(FINE, false);
    }
}

void PopupHSlider::mouseDrag(const juce::MouseEvent& e)
{
    if (intercepted)
    {
        if (interceptDrag)
            interceptDrag(e);
        return;
    }

    if (state == PRE_DRAG)
    {
        // Pick a bar on the first real movement, in any direction: clearly up = Fine, anything
        // else (sideways or down) = Coarse. The original only looked at vertical movement
        // (up 1 px = Fine, down 2 px = Coarse), so a sideways drag did nothing until the hand
        // wobbled, and then the value leapt by the whole sideways distance and the bars set off
        // after the pointer (seen in Live, 1.2.0). Then the bars are placed again at the pointer,
        // so picking a bar never changes the value.
        const int dx = e.x - clickPos.x, dy = e.y - clickPos.y;
        const int dist = juce::jmax(std::abs(dx), std::abs(dy));
        if (dist >= kPickDistance)
        {
            editing = true;
            if (onGestureBegin)
                onGestureBegin();
            setState(dy < 0 && -dy >= std::abs(dx) ? FINE : COARSE);
            placeBars(e.x);
        }
        updatePopups();
    }
    else if (state >= COARSE)
    {
        int vx = e.x + xOffs; // mouse x on the bars' scale
        auto newState = state;
        for (int i = 0; i < 2; ++i)
            if (slider[i].bgRect.contains(vx, e.y))
                newState = (State) i;
        if (newState != state)
        {
            // the other bar's knob is normally lined up with this one; if it was slid to stay
            // inside, carry on from its knob so the value does not jump
            const int dk = RndToInt(slider[(int) newState].getPixel(value)) - RndToInt(slider[(int) state].getPixel(value));
            xOffs += dk;
            vx += dk;
            state = newState;
        }

        float v = slider[(int) state].drag(vx);
        {
            // a bar dragged along past the editor edge stays at the edge; the scale moves under it
            const int d = slider[(int) state].fitDelta(area.getX(), area.getRight(),
                                                       Skin::get().hsKnob[(int) state].getWidth() / 2);
            if (d != 0)
            {
                slider[(int) state].translate(d, true);
                xOffs += d;
            }
        }
        if (snapToMenu)
        {
            const int idx = findClosestMenuIdx(v);
            if (idx >= 0)
                v = menu[(size_t) idx].value;
        }
        setValue(v, true);
        updatePopups();
        if (onStateChanged)
            onStateChanged();
    }
    if (afterMouse)
        afterMouse(e.mods);
}

void PopupHSlider::mouseUp(const juce::MouseEvent& e)
{
    if (intercepted)
    {
        intercepted = false;
        if (interceptUp)
            interceptUp(e);
        return;
    }
    if (state == MENU_OPEN)
        return;

    closePopups();
    if (editing)
    {
        editing = false;
        if (onGestureEnd)
            onGestureEnd();
    }
    setState(getLocalBounds().contains(e.getPosition()) ? FOCUS : INACTIVE);
    if (afterMouse)
        afterMouse(e.mods);
}

void PopupHSlider::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // VSTGUI gave one unit per wheel notch; JUCE reports roughly 0.1..0.25 per notch on macOS
    // Momentum (inertial) events keep arriving for a second or more after the fingers leave a
    // trackpad or Magic Mouse, which made the value keep going on its own. A wheel turn during a
    // popup drag is ignored too (the drag owns the value then).
    if (w.isInertial || state >= PRE_DRAG || state == MENU_OPEN)
        return;
    const float distance = juce::jlimit(-4.0f, 4.0f, (w.isReversed ? -w.deltaY : w.deltaY) * 4.0f);
    if (distance == 0.0f)
        return;
    const float delta = wheelInc * distance * (e.mods.isCtrlDown() || e.mods.isCommandDown() ? fineScaleFrac : 1.0f);
    if (onGestureBegin)
        onGestureBegin();
    setValue(value + delta, true);
    if (onGestureEnd)
        onGestureEnd();
}

void PopupHSlider::openMenu()
{
    if (menu.empty())
        return;
    setState(MENU_OPEN);
    const float valueBefore = value;

    juce::PopupMenu m;
    const int closest = snapToMenu ? findClosestMenuIdx(value) : -1;
    for (int i = 0; i < (int) menu.size(); ++i)
        m.addItem(i + 1, menu[(size_t) i].text, true, i == closest);

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                    [safe = safeThis, valueBefore](int result) {
                        if (safe == nullptr)
                            return;
                        auto& s = *safe;
                        s.state = INACTIVE;
                        if (result > 0)
                        {
                            const int idx = result - 1;
                            float v = s.menu[(size_t) idx].value;
                            if (!(v >= 0.0f && v <= 1.0f))
                                v = valueBefore;
                            if (s.onGestureBegin)
                                s.onGestureBegin();
                            // force an update even if the value is the same (DtPopupHSlider::valueChanged)
                            s.value = juce::jlimit(0.0f, 1.0f, v);
                            if (s.onValueChanged)
                                s.onValueChanged(s.value, idx);
                            if (s.onGestureEnd)
                                s.onGestureEnd();
                        }
                        s.state = s.isMouseOver() ? FOCUS : INACTIVE;
                        if (s.onStateChanged)
                            s.onStateChanged();
                    });
}

void PopupHSlider::updatePopups()
{
    if (state < PRE_DRAG)
        return;

    const float v = value;
    if (state > PRE_DRAG)
    {
        const int other = state == COARSE ? FINE : COARSE;
        slider[other].shift(v, slider[(int) state].getPixel(v));
        // keep it inside: the fine bar is a window on a scale 15 bars long, so only the bar moves
        // and the knobs stay lined up; the coarse bar is the whole 0..1 scale, so it moves with it
        fitInside(other, other == COARSE);
    }

    const auto& skin = Skin::get();
    SliderPopupOverlay::Bar bars[2];
    for (int i = 0; i < 2; ++i)
    {
        const int idx = state == (State) i ? 1 : 0;
        const auto& bg = skin.hsBg[i];
        const auto& knob = skin.hsKnob[i];
        const auto bgCent = slider[i].bgRect.getCentre();
        const juce::Rectangle<int> bgTile(0, 0, bg.getWidth(), bg.getHeight() / 2);
        // a snapped value can sit far off the fine bar (the original drew its knob there anyway);
        // keep the knob on its bar
        const int knobX = juce::jlimit(slider[i].bgRect.getX(), slider[i].bgRect.getRight() - 1,
                                       RndToInt(slider[i].getPixel(v)));
        const juce::Rectangle<int> knobTile(0, 0, knob.getWidth(), knob.getHeight() / 2);
        bars[i] = { bg, knob, idx, idx, bgTile.withCentre(bgCent), knobTile.withCentre({ knobX, bgCent.y }) };
    }

    auto* host = overlayHost.getComponent();
    if (host == nullptr)
        return;
    if (overlay == nullptr)
    {
        overlay = std::make_unique<SliderPopupOverlay>();
        overlay->setBounds(host->getLocalBounds());
        host->addAndMakeVisible(*overlay); // last child = drawn over everything else
    }
    overlay->update(bars, toHost());
}

void PopupHSlider::closePopups()
{
    if (overlay != nullptr)
    {
        // repaint everything the popups ever covered, so nothing is left behind
        lastCloseRepaint = overlay->getEverDrawn();
        if (auto* host = overlayHost.getComponent())
        {
            host->removeChildComponent(overlay.get());
            host->repaint(lastCloseRepaint);
        }
        overlay.reset();
    }
    xOffs = 0;
}

juce::Rectangle<int> PopupHSlider::getPopupBarBounds(int i) const { return overlay ? overlay->barBounds(i) : juce::Rectangle<int>(); }
juce::Rectangle<int> PopupHSlider::getPopupKnobBounds(int i) const { return overlay ? overlay->knobBounds(i) : juce::Rectangle<int>(); }
juce::Rectangle<int> PopupHSlider::getPopupDrawnArea() const { return overlay ? overlay->getEverDrawn() : juce::Rectangle<int>(); }

//==============================================================================
void OwnerToggle::setBoolValue(bool v, bool sendToListener)
{
    if (v == value)
        return;
    value = v;
    if (sendToListener && onToggle)
        onToggle(value);
    if (onStateChanged)
        onStateChanged();
}

void OwnerToggle::mouseUp(const juce::MouseEvent& e)
{
    if (state <= FOCUS)
        return;
    if (getLocalBounds().contains(e.getPosition()))
    {
        state = FOCUS;
        setBoolValue(!value, true);
    }
    else
    {
        state = INACTIVE;
    }
    if (onStateChanged)
        onStateChanged();
}

//==============================================================================
void OwnerMenu::mouseDown(const juce::MouseEvent&)
{
    setState(MENU_OPEN);
    juce::PopupMenu m;
    for (int i = 0; i < entries.size(); ++i)
        m.addItem(i + 1, entries[i], true, i == current);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                    [safe = safeThis](int result) {
                        if (safe == nullptr)
                            return;
                        if (result > 0 && safe->onSelect)
                            safe->onSelect(result - 1);
                        if (safe != nullptr)
                            safe->setState(safe->isMouseOver() ? FOCUS : INACTIVE);
                    });
}
} // namespace dtgui
