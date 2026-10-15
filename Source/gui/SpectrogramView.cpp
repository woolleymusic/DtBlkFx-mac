#include "SpectrogramView.h"
#include "Skin.h"

#include "DtBlkFx.hpp"
#include "NoteFreq.h"

namespace dtgui
{
const char* const SpectrogramView::menuStrings[7] = { "Input L+R",  "Input Left",  "Input Right", "Output L+R",
                                                      "Output Left", "Output Right", "Paused" };

SpectrogramView::SpectrogramView(DtBlkFx& e, SgramModel& m, int idx) : blkfx(e), model(m), index(idx)
{
    hilite.fill(-1);
    ring.assign((size_t) (SgramModel::WIDTH * SgramModel::IMG_H), 0xff000000u);
    display = juce::Image(juce::Image::RGB, SgramModel::WIDTH, SgramModel::DISP_H, true);
    displayInv = juce::Image(juce::Image::RGB, SgramModel::WIDTH, SgramModel::DISP_H, true);

    // Spectrogram::init geometry for a 410 x 95 rectangle
    displayRect = { 5, 1, SgramModel::WIDTH, SgramModel::DISP_H };
    pixPerOctave = BlkFxParam::octaveFrac() * (float) SgramModel::WIDTH;
    auto parts = splitH({ 0, 3, 410, 18 }, { 0.2f, 1.2f, 1.2f, 1.0f, 0.2f });
    infoRect = parts[1];
    nameRect = parts[3];

    setOpaque(true);
}

void SpectrogramView::refresh(bool force)
{
    bool changed = false;
    {
        std::unique_lock<std::recursive_mutex> lock(blkfx._protect.m, std::try_to_lock);
        if (!lock.owns_lock())
            return; // audio thread is busy, try again next tick
        model.checkSampleRate(blkfx.getSampleRate());
        auto& s = model.sg[index];
        if (force || s.generation != lastGeneration)
        {
            lastGeneration = s.generation;
            ring = s.image;
            dispI = s.dispI;
            changed = true;
        }
    }
    if (changed)
    {
        rebuildImages();
        repaint();
    }
}

void SpectrogramView::rebuildImages()
{
    // Spectrogram::drawBm: bottom display row is the most recent line, older lines above
    const juce::Image::BitmapData d(display, juce::Image::BitmapData::writeOnly);
    const juce::Image::BitmapData di(displayInv, juce::Image::BitmapData::writeOnly);
    for (int row = 0; row < SgramModel::DISP_H; ++row)
    {
        int src = dispI - (SgramModel::DISP_H - 1 - row);
        while (src < 0)
            src += SgramModel::IMG_H;
        src %= SgramModel::IMG_H;
        const juce::uint32* line = ring.data() + (size_t) (src * SgramModel::WIDTH);
        for (int x = 0; x < SgramModel::WIDTH; ++x)
        {
            const juce::uint32 p = line[x];
            const auto c = juce::Colour(p);
            d.setPixelColour(x, row, c);
            di.setPixelColour(x, row, juce::Colour((juce::uint8) (255 - c.getRed()), (juce::uint8) (255 - c.getGreen()),
                                                   (juce::uint8) (255 - c.getBlue())));
        }
    }
}

void SpectrogramView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    // octave tick strip: inverse of the frame colour, with frame coloured ticks
    const int tickY = getHeight() - 1;
    g.setColour(juce::Colours::white);
    g.fillRect(0, tickY, getWidth() - 1, 1);
    g.setColour(juce::Colours::black);
    for (float x = 0; x < (float) displayRect.getWidth(); x += pixPerOctave)
        g.fillRect(RndToInt(x) + displayRect.getX(), tickY, 1, 1);

    // image, inverted between hilite edges (Spectrogram::drawBm with _hilite_x)
    bool inv = false;
    int x = 0, i = 0;
    const int w = displayRect.getWidth();
    while (x < w)
    {
        int nx = i < (int) hilite.size() ? hilite[(size_t) i] : w;
        ++i;
        nx = juce::jlimit(x, w, nx);
        if (nx > x)
        {
            const auto& img = inv ? displayInv : display;
            g.drawImage(img, displayRect.getX() + x, displayRect.getY(), nx - x, displayRect.getHeight(), x, 0, nx - x,
                        displayRect.getHeight());
        }
        inv = !inv;
        x = nx;
        if (i > (int) hilite.size() && nx >= w)
            break;
    }

    // overlays (Spectrogram::drawDirtyOverlay): black outline, white text
    const auto outline = juce::Colours::black, fill = juce::Colours::white;
    if (infoHz >= 0)
    {
        CharArray<64> str;
        str << HzToNote(infoHz);
        outlineText(g, juce::String(str.data), infoRect.withWidth(infoRect.getWidth() / 2), outline, fill);
        str << sprnum(infoHz, /*min unit*/ 1.0f) << "Hz";
        const int half = infoRect.getWidth() / 2;
        outlineText(g, juce::String(str.data), infoRect.withTrimmedLeft(half), outline, fill);
    }
    const int menuVal = juce::jlimit(0, 6, model.sg[index].menuVal.load());
    outlineText(g, menuStrings[menuVal], nameRect, outline, fill);
}

void SpectrogramView::mouseMove(const juce::MouseEvent& e)
{
    const int x = e.x - displayRect.getX();
    const float xFrac = (float) x / (float) (SgramModel::WIDTH - 1);
    setInfoFreq(model.pixHz[(size_t) juce::jlimit(0, SgramModel::WIDTH - 1, x)]);

    for (SpectrogramView* s = this; s != nullptr;)
    {
        HiliteArray h;
        h.fill(-1);
        const int px = s->fracToPix(xFrac);
        h[0] = px;
        h[1] = px + 1;
        s->setHilite(h);
        s = s->linked;
        if (s == this)
            break;
    }
}

void SpectrogramView::mouseExit(const juce::MouseEvent&)
{
    for (SpectrogramView* s = this; s != nullptr;)
    {
        s->clrHilite();
        s->setInfoFreq(-1);
        s = s->linked;
        if (s == this)
            break;
    }
}

void SpectrogramView::mouseDown(const juce::MouseEvent&)
{
    juce::PopupMenu m;
    const int cur = model.sg[index].menuVal.load();
    for (int i = 0; i < 7; ++i)
        m.addItem(i + 1, menuStrings[i], true, i == cur);
    if (extendMenu)
        extendMenu(m);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                    [safe = safeThis](int result) {
                        if (safe == nullptr || result <= 0)
                            return;
                        if (result >= kExtraMenuBase)
                        {
                            if (safe->onExtraMenuItem)
                                safe->onExtraMenuItem(result);
                            return;
                        }
                        safe->model.sg[safe->index].menuVal = result - 1;
                        safe->repaint();
                    });
}
} // namespace dtgui
