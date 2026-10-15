#include "SgramModel.h"
#include "Skin.h"

#include "DtBlkFx.hpp"

namespace
{
// Gui::Gui colour stops: position, R, G, B, A
const float kColVec[][5] = { { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }, { 0.2f, 0.0f, 0.0f, 1.0f, 1.0f },
                             { 0.4f, 0.0f, 1.0f, 1.0f, 1.0f }, { 0.6f, 0.0f, 0.8f, 0.0f, 1.0f },
                             { 0.8f, 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f } };

juce::uint32 argb(float r, float g, float b)
{
    auto c = [](float v) { return (juce::uint32) juce::jlimit(0, 255, (int) std::floor(v * 255.0f + 0.5f)); };
    return 0xff000000u | (c(r) << 16) | (c(g) << 8) | c(b);
}

// GenerateGradientRGBA (VstGuiSupport.cpp): piecewise linear between the stops
std::vector<juce::uint32> makeGradient(int n)
{
    std::vector<juce::uint32> dst((size_t) n);
    const int numStops = (int) (sizeof(kColVec) / sizeof(kColVec[0]));
    int colI = 0, endI = 0, begI = 0;
    const float* a = kColVec[0];
    const float* b = kColVec[0];
    float frac = 0, fracStp = 0;
    for (int i = 0; i < n; ++i)
    {
        if (i >= endI)
        {
            begI = endI;
            a = b;
            if (colI < numStops - 1)
                ++colI;
            b = kColVec[colI];
            endI = (int) std::floor(b[0] * (float) n + 0.5f);
            if (endI < begI + 1)
                endI = begI + 1;
            frac = 0;
            fracStp = 1.0f / (float) (endI - begI);
        }
        dst[(size_t) i] = argb(a[1] + (b[1] - a[1]) * frac, a[2] + (b[2] - a[2]) * frac, a[3] + (b[3] - a[3]) * frac);
        frac += fracStp;
    }
    return dst;
}
} // namespace

SgramModel::SgramModel(DtBlkFx& e) : blkfx(e)
{
    pixHz.resize(WIDTH);
    BlkFxParam::genPixelToHz(Rng<float>(pixHz.data(), WIDTH));

    colMap = makeGradient(1000);

    // Spectrogram::init
    pwrMin = 1e-4f * 1e-4f;
    pwrMax = 0.2f * 0.2f;
    colLogOffs = logf(pwrMin);
    colLogScale = (float) colMap.size() / (logf(pwrMax) - colLogOffs);

    for (auto& s : sg)
    {
        s.image.assign((size_t) (WIDTH * IMG_H), 0xff000000u);
        s.currMax.assign(WIDTH, 0.0f);
    }
    sg[0].menuVal = 0; // "Input L+R"
    sg[1].menuVal = 3; // "Output L+R"
}

cplxf* SgramModel::binData(int ch)
{
    return blkfx.FFTdata(ch);
}

void SgramModel::open(const juce::Image& splash, float sampleRate)
{
    pixBinRate = -1.0f;
    checkSampleRate(sampleRate);

    for (int i = 0; i < 2; ++i)
    {
        auto& s = sg[i];
        // Gui::openSgram: the matching half of the splash, offset by the display rect (5, 1)
        juce::Image img(juce::Image::ARGB, WIDTH, IMG_H, true);
        {
            juce::Graphics g(img);
            g.fillAll(juce::Colours::black);
            g.drawImageAt(splash, -5, -1 - i * IMG_H);
            if (i == 1)
            {
                // ... and a few words on the second one
                const auto white = juce::Colours::white, black = juce::Colours::black;
                juce::Rectangle<int> r(0, 10, WIDTH, 15);
                for (auto* line : { "~", "Hi there BlkFx user", "~" })
                {
                    dtgui::outlineText(g, line, r, white, black);
                    r.translate(0, 15);
                }
                r.translate(0, 5);
                dtgui::outlineText(g, "Stereo (compiled " __DATE__ ")", r, white, black);
            }
        }
        const juce::Image::BitmapData bm(img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < IMG_H; ++y)
            for (int x = 0; x < WIDTH; ++x)
                s.image[(size_t) (y * WIDTH + x)] = bm.getPixelColour(x, y).getARGB() | 0xff000000u;
        // first line displayed at the very top of the client rect
        s.dispI = DISP_H - 1;
        s.imageI = s.dispI + 1;
        s.currLineSampPos = 0;
        s.currLineIsEmpty = true;
        ++s.generation;
    }
    active = true;
}

void SgramModel::suspend()
{
    for (auto& s : sg)
        s.currLineSampPos = 0;
}

juce::uint32 SgramModel::mapCol(float in) const
{
    int out = 0;
    if (in > pwrMin)
    {
        out = (int) ((logf(in) - colLogOffs) * colLogScale + 0.5f);
        if (out >= (int) colMap.size())
            out = (int) colMap.size() - 1;
    }
    return colMap[(size_t) out];
}

void SgramModel::newData(Sgram& s, cplxf** binRng, float pwrScale)
{
    // Spectrogram::getMaxVals
    const bool emptyLine = s.currLineIsEmpty;
    s.currLineIsEmpty = false;
    float* currMax = s.currMax.data();
    float* currMaxEnd = currMax + s.currMax.size();
    cplxf* binA = *binRng++;
    while (currMax < currMaxEnd)
    {
        cplxf* binB = *binRng++;
        float v = norm(*binA++) * pwrScale;
        float maxV = emptyLine ? v : std::max(*currMax, v);
        while (binA < binB)
        {
            v = norm(*binA++) * pwrScale;
            if (v > maxV)
                maxV = v;
        }
        *currMax++ = maxV;
        binA = binB;
    }
}

void SgramModel::blkDone(Sgram& s, long sampPos, long timeFftN)
{
    if (sampPos + timeFftN - s.currLineSampPos < minSampsPerLine)
        return;

    juce::uint32* dst = s.image.data() + (size_t) (s.imageI * WIDTH);
    for (int x = 0; x < WIDTH; ++x)
        dst[x] = mapCol(s.currMax[(size_t) x]);

    s.currLineSampPos = sampPos + timeFftN;
    s.dispI = s.imageI;
    if (++s.imageI >= IMG_H)
        s.imageI = 0;
    s.currLineIsEmpty = true;
    ++s.generation;
}

void SgramModel::fftDataReady(int a)
{
    if (!active)
        return;

    // Gui::FFTDataRdy, stereo branch
    static const bool chMenuMap[2][7][2] = {
        { { true, true }, { true, false }, { false, true }, { false, false }, { false, false }, { false, false }, { false, false } },
        { { false, false }, { false, false }, { false, false }, { true, true }, { true, false }, { false, true }, { false, false } }
    };

    const int plan = (int) blkfx._plan;
    const long sampPos = blkfx._blk_samp_abs;
    const long timeFftN = blkfx._time_fft_n;

    for (auto& s : sg)
    {
        const int menuVal = s.menuVal.load();
        if (menuVal < 0 || menuVal >= 7 || menuVal == MENU_PAUSED)
            continue;

        bool update = false;
        for (int ch = 0; ch < 2; ++ch)
        {
            if (chMenuMap[a][menuVal][ch])
            {
                const float pwrScale = a == 1 ? blkfx._chan[ch].out_pwr_scale : 1.0f;
                newData(s, pixBin[(size_t) ch].getMap(plan), pwrScale);
                update = true;
            }
        }
        if (update)
            blkDone(s, sampPos, timeFftN);
    }
}
