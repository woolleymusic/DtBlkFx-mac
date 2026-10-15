#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: the global control strip (original GlobalCtrl.cpp).

410 x 26, six 68 px cells over global_ctrl.png: MixBack, Power (match/filter), Delay, Overlap,
Sync, BlkLen. Values are drawn in the bottom 15 px of each cell.
***************************************************************************************************/

#include "Controls.h"

#include <functional>
#include <vector>

#include "BlkFxParam.h"

class DtBlkFx;

namespace dtgui
{
class GlobalCtrlView : public juce::Component
{
public:
    explicit GlobalCtrlView(DtBlkFx& e);

    // GlobalCtrl::setParameter: host/engine value for a global param changed
    void setParameter(int index, float v);

    // send a value to the plug-in (setParameterAutomated); wired up by the editor
    std::function<void(int index, float value)> sendParam;
    std::function<void(int index, bool begin)> gesture;

    void paint(juce::Graphics&) override;

private:
    void updateMenus();
    void valueChanged(int paramNum, int menuIdx);
    int getBlkSzPlan() const
    {
        return juce::jlimit(0, (int) NUM_FFT_SZ - 1, (int) (blksz.getValue() * (float) NUM_FFT_SZ));
    }
    juce::Rectangle<int> textRect(const juce::Component& c) const
    {
        auto r = c.getBounds();
        return r.withTop(std::max(r.getBottom() - 15, r.getY()));
    }

    DtBlkFx& blkfx;
    PopupHSlider mixback, delay, overlap, blksz;
    OwnerToggle pwrmatch, blksync;

    BlkFxParam::Delay::Units delayUnits = BlkFxParam::Delay::BEATS;
    std::vector<BlkFxParam::Delay::Units> delayMenuUnits;
    float sampleRate = -1, sampsPerBeat = -1;
};
} // namespace dtgui
