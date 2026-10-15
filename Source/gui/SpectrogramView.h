#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: one spectrogram panel (original Spectrogram.cpp drawing + Gui.cpp menu).

410 x 95: 1 px black frame, a 400 x 92 scrolling image (newest line at the bottom), a black line,
then a 1 px white strip with black octave ticks. Hovering draws an inverted vertical line in both
spectrograms and shows note + Hz; clicking opens the source menu (Input L+R ... Paused).
***************************************************************************************************/

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "SgramModel.h"

class DtBlkFx;

namespace dtgui
{
class SpectrogramView : public juce::Component
{
public:
    enum
    {
        NUM_HILITE_RECT = 3
    };
    using HiliteArray = std::array<int, NUM_HILITE_RECT * 2>;

    static const char* const menuStrings[7];

    SpectrogramView(DtBlkFx& e, SgramModel& m, int index);

    void setLinked(SpectrogramView* other) { linked = other; }

    // JUCE port: the editor appends its preset items (load / save .fxp / .fxb) to the source menu.
    // Items use IDs >= kExtraMenuBase; a chosen one goes to onExtraMenuItem.
    static constexpr int kExtraMenuBase = 100;
    std::function<void(juce::PopupMenu&)> extendMenu;
    std::function<void(int id)> onExtraMenuItem;

    // copy any new lines out of the model (call from the editor timer, message thread)
    void refresh(bool force);

    // FxCtrl / mouse hover interface (Spectrogram.h)
    int fracToPix(float v) const
    {
        const int w = SgramModel::WIDTH - 1;
        return juce::jlimit(0, w, RndToInt(v * (float) w));
    }
    void setHilite(const HiliteArray& h)
    {
        if (h != hilite)
        {
            hilite = h;
            repaint();
        }
    }
    void clrHilite()
    {
        HiliteArray h;
        h.fill(-1);
        setHilite(h);
    }
    void setInfoFreq(float hz)
    {
        if (!juce::exactlyEqual(hz, infoHz))
        {
            infoHz = hz;
            repaint();
        }
    }

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent& e) override { mouseMove(e); }
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void rebuildImages();

    DtBlkFx& blkfx;
    SgramModel& model;
    int index;
    SpectrogramView* linked = nullptr;

    // local copy of the model's circular image, plus the order-resolved display image
    std::vector<juce::uint32> ring;
    int dispI = -1;
    juce::uint32 lastGeneration = 0xffffffffu;
    juce::Image display, displayInv;

    HiliteArray hilite;
    float infoHz = -1;

    // rectangles from Spectrogram::init (component coordinates)
    juce::Rectangle<int> displayRect, infoRect, nameRect;
    float pixPerOctave = 0;

    juce::Component::SafePointer<SpectrogramView> safeThis { this };
};
} // namespace dtgui
