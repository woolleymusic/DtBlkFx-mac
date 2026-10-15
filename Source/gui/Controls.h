#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: the original owner-drawn VSTGUI controls (VstGuiSupport.*), rebuilt as JUCE
components. They are invisible hot areas; the parent panel draws their text from the skin.

  PopupHSlider  (DtPopupHSlider)  left-click pops up a coarse slider below and a 15x fine slider
                                  above, move the mouse down/up to pick one and drag sideways;
                                  right-click opens the value menu; wheel nudges (ctrl = fine)

The popup sliders: the original drew them as topmost layered Windows windows in screen
coordinates, placed only by the value mapping, so they could hang outside the plug-in window. Here
they are one child component (SliderPopupOverlay) of the editor's content, kept inside it. When
the original geometry fits, it is used unchanged. When it does not, the bars are slid back inside
and the mouse is mapped with a matching offset (xOffs), so the value never jumps and dragging
still works past the editor edge.
  OwnerToggle   (DtOwnerToggle)   click toggles, release outside cancels
  OwnerMenu     (COptionMenuOwnerDraw) click opens a ticked popup menu
***************************************************************************************************/

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "misc_stuff.h" // FracRescale

namespace dtgui
{
class SliderPopupOverlay;

//-------------------------------------------------------------------------------------------------
class PopupHSlider : public juce::Component
{
public:
    enum State
    {
        INACTIVE = -10, // mouse not over control
        FOCUS,          // mouse over, no button
        MENU_OPEN,      // value menu dropped down
        PRE_DRAG,       // clicked, neither slider picked yet
        COARSE = 0,
        FINE = 1
    };

    PopupHSlider();
    ~PopupHSlider() override;

    // the component the popup sliders are drawn in, and kept inside: the nearest parent with
    // this property set to true (the editor's content), else the top-level component
    static constexpr const char* popupHostProperty = "dtguiPopupHost";

    // for tests: whether the popups are showing, and what they cover, in the host's coordinates
    bool isPopupOpen() const { return overlay != nullptr; }
    juce::Component* getPopupHost() const { return overlayHost.getComponent(); }
    juce::Rectangle<int> getPopupBarBounds(int i) const;  // bar image (i = COARSE or FINE)
    juce::Rectangle<int> getPopupKnobBounds(int i) const; // knob image
    juce::Rectangle<int> getPopupDrawnArea() const;       // everything drawn since opening
    juce::Rectangle<int> getLastCloseRepaint() const { return lastCloseRepaint; }

    float getValue() const { return value; }
    State getState() const { return state; }

    // set value, notify if asked (GUI activity)
    void setValue(float v, bool sendToListener = false);

    // menu interface (DtPopupHSlider)
    void setMenuSnap(bool s) { snapToMenu = s; }
    void removeAllMenuEntries() { menu.clear(); }
    int addMenuEntry(float v, const juce::String& txt)
    {
        menu.push_back({ v, txt });
        return (int) menu.size() - 1;
    }
    bool setMenuEntry(int idx, float v, const juce::String& txt)
    {
        if (idx < 0 || idx >= (int) menu.size())
            return false;
        menu[(size_t) idx] = { v, txt };
        return true;
    }
    int getNumMenuEntries() const { return (int) menu.size(); }
    int findClosestMenuIdx(float v) const;

    // value changed by the user; menuIdx >= 0 when it came from the menu (lastMenuIdx())
    std::function<void(float value, int menuIdx)> onValueChanged;
    std::function<void()> onGestureBegin, onGestureEnd;
    // called on any mouse down before anything else (GlobalCtrl::onMouseDown -> updateMenus)
    std::function<void()> onMouseDownHook;
    // state changed (owner redraws)
    std::function<void()> onStateChanged;
    // lets a parent steal mouse events (FxCtrl drag-both); return true when consumed
    std::function<bool(const juce::MouseEvent&)> interceptDown;
    std::function<void(const juce::MouseEvent&)> interceptDrag, interceptUp;
    std::function<void(juce::ModifierKeys)> afterMouse; // FxCtrl::mouseInterceptAfter
    std::function<void()> afterExit;

    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;

private:
    struct Internal // DtPopupHSliderInternal, in this control's coordinates
    {
        float magnify = 1;
        FracRescale rescale {};
        juce::Rectangle<int> bgRect; // active area (without the 2 px image border)
        void create(int top, int width, int height, float mag);
        void shift(float v, float pixel);
        float drag(int pixel);
        // move the bar sideways by d; with the mapping too (knob moves) or just the bar
        void translate(int d, bool withMapping);
        // the smallest sideways move that keeps [bgRect.x - margin, bgRect.right + margin) in lo..hi
        int fitDelta(int lo, int hi, int margin) const;
        float getValue(float pixel) const { return rescale.getFrac(pixel); }
        float getPixel(float v) const { return rescale.getRescaled(v); }
    };

    void setState(State s);
    void sliderMouseClick(const juce::MouseEvent&);
    void openMenu();
    void updatePopups();
    void closePopups();
    juce::Component* findPopupHost();
    juce::AffineTransform toHost() const;
    void fitInside(int i, bool withMapping);

    float value = 0;
    State state = INACTIVE;
    bool snapToMenu = false;
    float fineScaleFrac = 1.0f / 15.0f;
    float wheelInc = 0.1f;
    juce::Point<int> clickPos;
    struct Entry
    {
        float value;
        juce::String text;
    };
    std::vector<Entry> menu;
    Internal slider[2];
    std::unique_ptr<SliderPopupOverlay> overlay;
    juce::Component::SafePointer<juce::Component> overlayHost;
    juce::Rectangle<int> area;     // host bounds in this control's coordinates (clamp area)
    int xOffs = 0;                 // virtual mouse x = e.x + xOffs (non-zero only when clamped)
    juce::Rectangle<int> lastCloseRepaint;
    bool editing = false;
    bool intercepted = false;
    juce::Component::SafePointer<PopupHSlider> safeThis { this };
};

//-------------------------------------------------------------------------------------------------
class OwnerToggle : public juce::Component
{
public:
    enum State
    {
        INACTIVE,
        FOCUS,
        MOUSE_DOWN_OUTSIDE,
        MOUSE_DOWN_INSIDE
    };

    bool getBoolValue() const { return value; }
    void setBoolValue(bool v, bool sendToListener);
    bool getTentativeValue() const { return state == MOUSE_DOWN_INSIDE ? !value : value; }
    State getState() const { return state; }

    std::function<void(bool)> onToggle;
    std::function<void()> onStateChanged;

    void mouseEnter(const juce::MouseEvent&) override { setState(FOCUS); }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (state <= FOCUS)
            setState(INACTIVE);
    }
    void mouseMove(const juce::MouseEvent&) override { setState(FOCUS); }
    void mouseDown(const juce::MouseEvent&) override { setState(MOUSE_DOWN_INSIDE); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        setState(getLocalBounds().contains(e.getPosition()) ? MOUSE_DOWN_INSIDE : MOUSE_DOWN_OUTSIDE);
    }
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void setState(State s)
    {
        if (s == state)
            return;
        state = s;
        if (onStateChanged)
            onStateChanged();
    }
    bool value = false;
    State state = INACTIVE;
};

//-------------------------------------------------------------------------------------------------
class OwnerMenu : public juce::Component
{
public:
    enum State
    {
        INACTIVE,
        FOCUS,
        MENU_OPEN
    };

    void addEntry(const juce::String& s) { entries.add(s); }
    int getNumEntries() const { return entries.size(); }
    void setValue(int idx) { current = idx; }
    int getValue() const { return current; }
    State getState() const { return state; }

    std::function<void(int)> onSelect;
    std::function<void()> onStateChanged;

    void mouseEnter(const juce::MouseEvent&) override { setState(FOCUS); }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (state != MENU_OPEN)
            setState(INACTIVE);
    }
    void mouseDown(const juce::MouseEvent&) override;

private:
    void setState(State s)
    {
        if (s == state)
            return;
        state = s;
        if (onStateChanged)
            onStateChanged();
    }
    juce::StringArray entries;
    int current = -1;
    State state = INACTIVE;
    juce::Component::SafePointer<OwnerMenu> safeThis { this };
};
} // namespace dtgui
