#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: the original owner-drawn VSTGUI controls (VstGuiSupport.*), rebuilt as JUCE
components. They are invisible hot areas; the parent panel draws their text from the skin.

  PopupHSlider  (DtPopupHSlider)  left-click pops up a coarse slider below and a 15x fine slider
                                  above, move the mouse down/up to pick one and drag sideways;
                                  right-click opens the value menu; wheel nudges (ctrl = fine)
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
class SliderPopupWindow;

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

    // the offline harness sets this so drags can be simulated without opening desktop windows
    static inline bool headlessPopups = false;

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
        float getValue(float pixel) const { return rescale.getFrac(pixel); }
        float getPixel(float v) const { return rescale.getRescaled(v); }
    };

    void setState(State s);
    void sliderMouseClick(const juce::MouseEvent&);
    void openMenu();
    void updatePopups();
    void closePopups();

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
    std::unique_ptr<SliderPopupWindow> popup[2];
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
