#include "Controls.h"
#include "Skin.h"

#include <BinaryData.h>

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
// one popup slider: the original used two layered windows (background + knob) per slider; this
// is one transparent, click-through desktop window drawing both
class SliderPopupWindow : public juce::Component
{
public:
    SliderPopupWindow()
    {
        setInterceptsMouseClicks(false, false);
        setOpaque(false);
    }

    // rects in the owning control's coordinates (logical, unscaled)
    void update(juce::Component& owner, const juce::Image& bg, int bgTile, juce::Rectangle<int> bgRect,
                const juce::Image& knob, int knobTile, juce::Rectangle<int> knobRect)
    {
        bgImg = bg;
        knobImg = knob;
        bgI = bgTile;
        knobI = knobTile;
        const auto area = bgRect.getUnion(knobRect);
        bgR = bgRect - area.getPosition();
        knobR = knobRect - area.getPosition();

        const auto g1000 = owner.localAreaToGlobal(juce::Rectangle<int>(0, 0, 1000, 1000));
        scale = (float) g1000.getWidth() / 1000.0f;
        const auto topLeft = owner.localPointToGlobal(area.getPosition());
        setBounds(topLeft.x, topLeft.y, juce::roundToInt((float) area.getWidth() * scale),
                  juce::roundToInt((float) area.getHeight() * scale));

        if (!isOnDesktop())
        {
            addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresMouseClicks
                         | juce::ComponentPeer::windowIgnoresKeyPresses);
            setAlwaysOnTop(true);
            setVisible(true);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.addTransform(juce::AffineTransform::scale(scale));
        const int th = bgImg.getHeight() / 2, kh = knobImg.getHeight() / 2;
        g.drawImage(bgImg, bgR.getX(), bgR.getY(), bgR.getWidth(), bgR.getHeight(), 0, bgI * th, bgImg.getWidth(), th);
        g.drawImage(knobImg, knobR.getX(), knobR.getY(), knobR.getWidth(), knobR.getHeight(), 0, knobI * kh,
                    knobImg.getWidth(), kh);
    }

private:
    juce::Image bgImg, knobImg;
    int bgI = 0, knobI = 0;
    juce::Rectangle<int> bgR, knobR;
    float scale = 1.0f;
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

void PopupHSlider::sliderMouseClick(const juce::MouseEvent& e)
{
    if (state >= MENU_OPEN)
        return;
    setState(PRE_DRAG);

    const auto& skin = Skin::get();
    constexpr int IMAGE_BORDER = 2;
    int y[2];
    y[FINE] = 0 - skin.hsBg[FINE].getHeight() / 2 + IMAGE_BORDER;
    y[COARSE] = getHeight() - IMAGE_BORDER;
    const float magnify[2] = { 1.0f, 1.0f / fineScaleFrac };

    for (int i = 0; i < 2; ++i)
    {
        slider[i].create(y[i], skin.hsBg[i].getWidth() - IMAGE_BORDER * 2,
                         skin.hsBg[i].getHeight() / 2 - IMAGE_BORDER * 2, magnify[i]);
        slider[i].shift(value, (float) e.x);
    }
    updatePopups();
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
        const int yDelta = e.y - clickPos.y;
        if (yDelta < 0)
        {
            editing = true;
            if (onGestureBegin)
                onGestureBegin();
            setState(FINE);
        }
        else if (yDelta > 1)
        {
            editing = true;
            if (onGestureBegin)
                onGestureBegin();
            setState(COARSE);
        }
        updatePopups();
    }
    else if (state >= COARSE)
    {
        auto newState = state;
        for (int i = 0; i < 2; ++i)
            if (slider[i].bgRect.contains(e.getPosition()))
                newState = (State) i;
        state = newState;

        float v = slider[(int) state].drag(e.x);
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
    }

    const auto& skin = Skin::get();
    for (int i = 0; i < 2; ++i)
    {
        const int idx = state == (State) i ? 1 : 0;
        const auto& bg = skin.hsBg[i];
        const auto& knob = skin.hsKnob[i];
        const auto bgCent = slider[i].bgRect.getCentre();
        const juce::Rectangle<int> bgTile(0, 0, bg.getWidth(), bg.getHeight() / 2);
        const auto bgR = bgTile.withCentre(bgCent);
        const int knobX = RndToInt(slider[i].getPixel(v));
        const juce::Rectangle<int> knobTile(0, 0, knob.getWidth(), knob.getHeight() / 2);
        const auto knobR = knobTile.withCentre({ knobX, bgCent.y });

        if (headlessPopups)
            continue;
        if (popup[i] == nullptr)
            popup[i] = std::make_unique<SliderPopupWindow>();
        popup[i]->update(*this, bg, idx, bgR, knob, idx, knobR);
    }
}

void PopupHSlider::closePopups()
{
    for (auto& p : popup)
        p.reset();
}

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
