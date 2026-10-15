#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: one effect row (original FxCtrl.cpp).

410 x 24 over fx_bg.png, split 1 : 1 : .6 : 1.2 : 1.2 into FreqA, FreqB, Amp, Effect, Value. The
red amp bar and the frequency scale strip are drawn from the skin; masks join the row below.
Shift-drag or right-drag on either frequency moves both. Hovering a frequency marks the range in
the spectrograms.
***************************************************************************************************/

#include "Controls.h"
#include "SpectrogramView.h"

#include <functional>
#include <vector>

class DtBlkFx;
class FxRun1_0;

namespace dtgui
{
class FxCtrlView : public juce::Component
{
public:
    FxCtrlView(DtBlkFx& e, int fxSet, SpectrogramView* sgram0, SpectrogramView* sgram1);

    // FxCtrl::setParameter (fx_param 0..4)
    void setParameter(int fxParam, float v);

    std::function<void(int index, float value)> sendParam;
    std::function<void(int index, bool begin)> gesture;
    std::function<void()> onChanged; // owner repaints all rows (mask joins)

    void paint(juce::Graphics&) override;

private:
    enum DragBoth
    {
        DRAG_BOTH_INACTIVE = 0,
        DRAG_BOTH_FOCUS,
        DRAG_BOTH_ACTIVE
    };

    juce::Component* getCtrlFocus();
    void doHiliteSgrams();
    void updateAmpMenu();
    void updateFxValMenu();
    void valueChanged(juce::Component* ctrl, float v, int menuIdx);
    void afterMouse(juce::ModifierKeys mods);
    void afterExit();

    DtBlkFx& blkfx;
    int fxSet, paramOffset;
    SpectrogramView* sgram[2];

    PopupHSlider freq[2], amp, fxval;
    OwnerMenu fxtype;

    enum
    {
        AMP_MENU_EMPTY = -1,
        AMP_MENU_PERCENT,
        AMP_MENU_DB
    };
    int ampMenuMixmode = AMP_MENU_EMPTY;
    FxRun1_0* fxvalMenuType = nullptr;

    DragBoth dragBoth = DRAG_BOTH_INACTIVE;
    FracRescale dragBothRescale[2] {};

    std::vector<float> mapMenuToParam;
    std::vector<int> mapEffectToMenu;
};
} // namespace dtgui
