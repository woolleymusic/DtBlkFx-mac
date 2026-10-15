#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: the data side of the two spectrograms.

The original Spectrogram (Source/original_gui/Spectrogram.*) was fed straight from the audio thread
inside Gui::FFTDataRdy with DtBlkFx::_protect held: it took the max power per pixel through the
PixelFreqBin maps and wrote coloured lines into a circular image. This class does the same work in
the same place (GuiBridge::FFTDataRdy); the JUCE SpectrogramView copies the finished lines out on
a timer while briefly holding the same lock.
***************************************************************************************************/

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <atomic>
#include <vector>

#include "PixelFreqBin.h"

class DtBlkFx;

class SgramModel
{
public:
    enum
    {
        WIDTH = 400,  // pixels (frame width - 10)
        IMG_H = 95,   // image lines (sgram rect height)
        DISP_H = 92,  // display rect height (1 px top border, 1 px bottom border, 1 px ticks)
        MENU_PAUSED = 6
    };

    explicit SgramModel(DtBlkFx& e);

    // --- call with DtBlkFx::_protect held
    // reset both images to the splash picture (as Gui::openSgram did) and start taking data
    void open(const juce::Image& splash, float sampleRate);
    void close() { active = false; }
    // rebuild the pixel -> bin maps if the host changed sample rate while the editor is open
    void checkSampleRate(float sampleRate)
    {
        if (!juce::exactlyEqual(sampleRate, pixBinRate))
            for (int ch = 0; ch < 2; ++ch)
                pixBin[(size_t) ch].init(Rng<float>(pixHz.data(), WIDTH), sampleRate, binData(ch));
        pixBinRate = sampleRate;
    }
    void suspend();
    void fftDataReady(int a); // Gui::FFTDataRdy

    struct Sgram
    {
        std::vector<juce::uint32> image; // IMG_H lines of WIDTH ARGB pixels, circular
        int imageI = 0;                  // next line to write (_image_i)
        int dispI = 0;                   // most recent finished line (_disp_i[1])
        long currLineSampPos = 0;
        bool currLineIsEmpty = true;
        std::vector<float> currMax;
        std::atomic<int> menuVal { 0 }; // 0..6, see SpectrogramView::menuStrings
        juce::uint32 generation = 0;    // bumped whenever a line is written
    };
    Sgram sg[2];

    // pixel -> Hz (BlkFxParam::genPixelToHz), shared with the views
    std::vector<float> pixHz;

private:
    void newData(Sgram& s, cplxf** binRng, float pwrScale);
    void blkDone(Sgram& s, long sampPos, long timeFftN);
    juce::uint32 mapCol(float in) const;

    DtBlkFx& blkfx;
    bool active = false;
    std::array<PixelFreqBin, 2> pixBin;
    float pixBinRate = 0;
    cplxf* binData(int ch);
    std::vector<juce::uint32> colMap;
    float colLogOffs = 0, colLogScale = 0, pwrMin = 0, pwrMax = 0;
    int minSampsPerLine = 600;
};
