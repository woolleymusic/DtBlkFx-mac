#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: the original PNG skin (resources/stereo_*.png) and the text style the VSTGUI
editor used (Arial 12 bold, drawn with a one pixel outline: OutlineDrawString in VstGuiSupport.cpp).

Layout constants come straight from the original Gui.cpp / GlobalCtrl.cpp / FxCtrl.cpp.
***************************************************************************************************/

#include <juce_gui_basics/juce_gui_basics.h>

namespace dtgui
{
// original window: width of global_ctrl.png, height = global + splash + 8 fx rows (Gui::Gui)
constexpr int kWidth = 410;
constexpr int kGlobalH = 26;
constexpr int kSgramH = 95;   // splash.png (191) / 2
constexpr int kFxRowH = 24;   // fx_bg.png (120) / 5 tiles
constexpr int kNumFx = 8;
constexpr int kHeight = kGlobalH + 191 + kFxRowH * kNumFx; // 409, as Gui::Gui computed it

// pixel to freq mapping is frame width - 10 (Gui::open)
constexpr int kSgramPixels = kWidth - 10;

struct Skin
{
    juce::Image fxBg, globBg, splash;
    juce::Image hsBg[2], hsKnob[2]; // index 0 = COARSE, 1 = FINE (DtPopupHSlider::Images)

    static const Skin& get();

    static juce::Font font()
    {
        // VSTGUI kNormalFont = Arial 12, kBoldFace
        return juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold));
    }
};

// colours used by the original
inline juce::Colour normalTxt() { return juce::Colour(0xff, 0xff, 0xff); }
inline juce::Colour focusTxt() { return juce::Colour(0xff, 0x80, 0x80); }

// OutlineDrawString: the string drawn 4 times offset by one pixel in "outline" (the VSTGUI font
// colour), then once in the middle in "fill" (the VSTGUI fill colour)
inline void outlineText(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r,
                        juce::Colour outline, juce::Colour fill,
                        juce::Justification just = juce::Justification::centred)
{
    g.setFont(Skin::font());
    g.setColour(outline);
    g.drawText(s, r.translated(-1, 0), just, false);
    g.drawText(s, r.translated(1, 0), just, false);
    g.drawText(s, r.translated(0, -1), just, false);
    g.drawText(s, r.translated(0, 1), just, false);
    g.setColour(fill);
    g.drawText(s, r, just, false);
}

// draw "src" region of an image at "dst" with an overall alpha (0..255)
inline void blit(juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> src, juce::Point<int> dst,
                 int alpha = 255)
{
    if (src.isEmpty())
        return;
    g.setOpacity((float) alpha / 255.0f);
    g.drawImage(img, dst.x, dst.y, src.getWidth(), src.getHeight(), src.getX(), src.getY(), src.getWidth(),
                src.getHeight());
    g.setOpacity(1.0f);
}

// SplitCRect from VstGuiSupport.h: split a width by ratios, rounding each edge
inline std::vector<juce::Rectangle<int>> splitH(juce::Rectangle<int> r, std::initializer_list<float> ratios)
{
    float sum = 0;
    for (float f : ratios)
        sum += f;
    const float scale = (float) r.getWidth() / sum;
    std::vector<juce::Rectangle<int>> out;
    float next = (float) r.getX();
    int left = r.getX();
    for (float f : ratios)
    {
        next += f * scale;
        const int right = (int) std::floor(next + 0.5f);
        out.push_back({ left, r.getY(), right - left, r.getHeight() });
        left = right;
    }
    return out;
}
} // namespace dtgui
