/**************************************************************************************************
DtBlkFxHarness: offline test bench for the DtBlkFx JUCE port (no DAW needed).

  DtBlkFxHarness list
      parameter names, default display strings, effect types and program names
  DtBlkFxHarness process <in.wav> <out.wav> [options]
      run a WAV through the plug-in and print output stats (NaN/inf count, peak, RMS)
  DtBlkFxHarness snapshot <out.png> [options] [--feed <in.wav>]
      render the editor to a PNG (optionally after feeding audio so the spectrograms have data)
  DtBlkFxHarness state
      save state, change everything, restore, and check that every parameter came back
  DtBlkFxHarness ui
      simulate mouse drags/clicks on the rebuilt editor and check the parameters they set
  DtBlkFxHarness popup <dir>
      open the Fine / Coarse popup sliders on cells in every corner of the editor at 1x, 1.5x and
      2x, check they sit inside the editor and line up with the cell, drag them past the edges,
      check nothing is left behind after closing, and write snapshots to <dir>/popup-*.png
  DtBlkFxHarness preset-load <file.fxp|file.fxb> [--drop]
      load a VST2 preset or bank into a fresh plug-in (with --drop: by dropping it on the editor)
      and print the result, current program, every parameter and every program name
  DtBlkFxHarness preset-save <file.fxp|file.fxb> [options] [--load <file>]
      apply options (after loading a preset first, if given) and save a .fxp or .fxb

options:
  --program N          select factory program N first (like picking it in the host)
  --set I=V            set parameter index I to normalised value V (repeatable)
  --fx S:TYPE          set fx slot S (0..7) to effect type index TYPE (see "list")
  --block N            host block size (default 512)
  --random-blocks      vary the host block size randomly between 1 and 2048
  --rate R             override the sample rate passed to prepareToPlay
  --scale S            snapshot scale (default 1 = original pixel size)
***************************************************************************************************/

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/PresetIO.h"

#include "DtBlkFx.hpp"
#include "FxRun1_0.h"

#include "../Source/gui/Controls.h"
#include "../Source/gui/Skin.h"

#include <cmath>
#include <iostream>

namespace
{
struct Options
{
    int program = -1;
    std::vector<std::pair<int, float>> sets;
    int block = 512;
    bool randomBlocks = false;
    double rate = 0;
    float scale = 1.0f;
    juce::File feed;
    juce::File load;
    bool drop = false;
};

bool parseOptions(const juce::StringArray& args, int start, Options& o)
{
    for (int i = start; i < args.size(); ++i)
    {
        const auto a = args[i];
        auto next = [&]() { return i + 1 < args.size() ? args[++i] : juce::String(); };
        if (a == "--program")
            o.program = next().getIntValue();
        else if (a == "--set")
        {
            auto kv = next();
            o.sets.push_back({ kv.upToFirstOccurrenceOf("=", false, false).getIntValue(),
                               kv.fromFirstOccurrenceOf("=", false, false).getFloatValue() });
        }
        else if (a == "--fx")
        {
            auto kv = next();
            int slot = kv.upToFirstOccurrenceOf(":", false, false).getIntValue();
            int type = kv.fromFirstOccurrenceOf(":", false, false).getIntValue();
            o.sets.push_back({ BlkFxParam::paramOffs(slot) + BlkFxParam::FX_TYPE,
                               BlkFxParam::getEffectTypeInv(type) });
        }
        else if (a == "--block")
            o.block = juce::jmax(1, next().getIntValue());
        else if (a == "--random-blocks")
            o.randomBlocks = true;
        else if (a == "--rate")
            o.rate = next().getDoubleValue();
        else if (a == "--scale")
            o.scale = next().getFloatValue();
        else if (a == "--feed")
            o.feed = juce::File::getCurrentWorkingDirectory().getChildFile(next());
        else if (a == "--load")
            o.load = juce::File::getCurrentWorkingDirectory().getChildFile(next());
        else if (a == "--drop")
            o.drop = true;
        else
        {
            std::cerr << "unknown option " << a << "\n";
            return false;
        }
    }
    return true;
}

void applyOptions(DtBlkFxProcessor& p, const Options& o)
{
    if (o.program >= 0)
        p.setCurrentProgram(o.program);
    for (auto& [idx, v] : o.sets)
        if (idx >= 0 && idx < p.numParams())
            p.param(idx)->setValueNotifyingHost(v);
}

std::unique_ptr<juce::AudioFormatReader> openWav(const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(f));
}

void runAudio(DtBlkFxProcessor& p, juce::AudioBuffer<float>& audio, double rate, const Options& o)
{
    const int numSamples = audio.getNumSamples();
    const int maxBlock = o.randomBlocks ? 2048 : o.block;
    p.setPlayConfigDetails(2, 2, rate, maxBlock);
    p.prepareToPlay(rate, maxBlock);

    juce::Random rng(1234);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < numSamples;)
    {
        int n = o.randomBlocks ? 1 + rng.nextInt(2048) : o.block;
        n = juce::jmin(n, numSamples - pos);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, pos, n);
        p.processBlock(view, midi);
        pos += n;
    }
    p.releaseResources();
}

int cmdList()
{
    DtBlkFxProcessor p;
    std::cout << "parameters (" << p.numParams() << "):\n";
    for (int i = 0; i < p.numParams(); ++i)
        std::cout << "  " << i << "\t" << p.param(i)->getName(64) << "\t" << p.param(i)->get()
                  << "\t\"" << p.param(i)->getCurrentValueAsText() << "\"\n";
    std::cout << "effect types (" << g_num_fx_1_0 << "):\n";
    for (int i = 0; i < g_num_fx_1_0; ++i)
        std::cout << "  " << i << "\t" << GetFxRun1_0(i)->name() << "\tparam=" << BlkFxParam::getEffectTypeInv(i) << "\n";
    std::cout << "programs (" << p.getNumPrograms() << "):\n";
    for (int i = 0; i < p.getNumPrograms(); ++i)
        std::cout << "  " << i << "\t" << p.getProgramName(i) << "\n";
    std::cout << "latency samples: " << p.getLatencySamples() << "\n";
    return 0;
}

int cmdProcess(const juce::StringArray& args)
{
    if (args.size() < 3)
        return 1;
    Options o;
    if (!parseOptions(args, 3, o))
        return 1;

    auto reader = openWav(juce::File::getCurrentWorkingDirectory().getChildFile(args[1]));
    if (!reader)
    {
        std::cerr << "cannot read " << args[1] << "\n";
        return 1;
    }
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio(2, n);
    reader->read(&audio, 0, n, 0, true, true);
    if (reader->numChannels == 1)
        audio.copyFrom(1, 0, audio, 0, 0, n);
    const double rate = o.rate > 0 ? o.rate : reader->sampleRate;

    DtBlkFxProcessor p;
    applyOptions(p, o);
    std::cout << "settings:";
    for (int i = 0; i < p.numParams(); ++i)
        if (p.param(i)->get() != 0.0f || i < 4)
            std::cout << " [" << i << " " << p.param(i)->getName(16) << "=" << p.param(i)->getCurrentValueAsText() << "]";
    std::cout << "\n";

    runAudio(p, audio, rate, o);

    long nonFinite = 0;
    double sumSq = 0, peak = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const float v = audio.getSample(ch, i);
            if (!std::isfinite(v))
            {
                ++nonFinite;
                continue;
            }
            sumSq += (double) v * v;
            peak = std::max(peak, (double) std::abs(v));
        }
    const double rms = std::sqrt(sumSq / std::max(1, 2 * n));
    std::cout << "out: samples=" << n << " nonfinite=" << nonFinite << " peak=" << peak
              << " rms_dBFS=" << juce::Decibels::gainToDecibels(rms, -200.0) << "\n";

    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(args[2]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    auto os = outFile.createOutputStream();
    if (!os)
        return 1;
    std::unique_ptr<juce::OutputStream> stream(os.release());
    auto writer = wav.createWriterFor(stream,
                                      juce::AudioFormatWriterOptions {}
                                          .withSampleRate(rate)
                                          .withNumChannels(2)
                                          .withBitsPerSample(32)
                                          .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!writer)
        return 1;
    writer->writeFromAudioSampleBuffer(audio, 0, n);
    return nonFinite == 0 ? 0 : 2;
}

int cmdSnapshot(const juce::StringArray& args)
{
    if (args.size() < 2)
        return 1;
    Options o;
    if (!parseOptions(args, 2, o))
        return 1;

    DtBlkFxProcessor p;
    applyOptions(p, o);

    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());

    if (o.feed.existsAsFile())
    {
        if (auto reader = openWav(o.feed))
        {
            const int n = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> audio(2, n);
            reader->read(&audio, 0, n, 0, true, true);
            const double rate = reader->sampleRate;
            p.setPlayConfigDetails(2, 2, rate, o.block);
            p.prepareToPlay(rate, o.block);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < n; pos += o.block)
            {
                const int len = juce::jmin(o.block, n - pos);
                juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, pos, len);
                p.processBlock(view, midi);
                // let the editor's timer-driven redraw catch up, as it would live
                if ((pos / o.block) % 4 == 0)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            }
        }
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);

    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, o.scale);
    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
    outFile.deleteFile();
    juce::FileOutputStream os(outFile);
    juce::PNGImageFormat png;
    if (!os.openedOk() || !png.writeImageToStream(img, os))
        return 1;
    std::cout << "wrote " << outFile.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
    return 0;
}

int cmdState()
{
    DtBlkFxProcessor a;
    a.setCurrentProgram(5);
    a.param(9)->setValueNotifyingHost(0.123f);
    a.changeProgramName(3, "renamed slot");
    juce::MemoryBlock mb;
    a.getStateInformation(mb);

    DtBlkFxProcessor b;
    b.setStateInformation(mb.getData(), (int) mb.getSize());
    int bad = 0;
    for (int i = 0; i < a.numParams(); ++i)
        if (std::abs(a.param(i)->get() - b.param(i)->get()) > 1e-6f)
        {
            std::cout << "param " << i << " differs: " << a.param(i)->get() << " vs " << b.param(i)->get() << "\n";
            ++bad;
        }
    std::cout << "state bytes=" << mb.getSize() << " program a=" << a.getCurrentProgram() << " b=" << b.getCurrentProgram()
              << " name3='" << b.getProgramName(3) << "' mismatched params=" << bad << "\n";
    return (bad == 0 && a.getCurrentProgram() == b.getCurrentProgram()) ? 0 : 3;
}

// --- VST2 presets -------------------------------------------------------------------------------
void printPresetState(DtBlkFxProcessor& p)
{
    std::cout << "current " << p.getCurrentProgram() << " \"" << p.getProgramName(p.getCurrentProgram()) << "\"\n";
    std::cout << "params";
    for (int i = 0; i < p.numParams(); ++i)
        std::cout << " " << juce::String::formatted("%.9g", (double) p.engine().getParameter(i));
    std::cout << "\n";
    for (int i = 0; i < p.getNumPrograms(); ++i)
        std::cout << "program " << i << "\t" << p.getProgramName(i) << "\n";
}

int cmdPresetLoad(const juce::StringArray& args)
{
    if (args.size() < 2)
        return 1;
    Options o;
    if (!parseOptions(args, 2, o))
        return 1;
    const auto f = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
    DtBlkFxProcessor p;
    bool ok;
    juce::String msg;
    if (o.drop)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto& e = dynamic_cast<DtBlkFxEditor&>(*ed);
        const bool wantsWav = e.isInterestedInFileDrag({ "/x/a.wav" });
        const bool wants = e.isInterestedInFileDrag({ f.getFullPathName() });
        e.fileDragEnter({ f.getFullPathName() }, 10, 10);
        e.filesDropped({ f.getFullPathName() }, 10, 10);
        msg = e.lastPresetMessage;
        ok = wants && !wantsWav && msg.startsWith("loaded");
        std::cout << "drop: interested(.wav)=" << wantsWav << " interested(file)=" << wants << "\n";
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // editor timer picks the values up
    }
    else
    {
        auto r = presetio::loadFile(p, f);
        ok = r.ok;
        msg = r.message;
    }
    std::cout << "result " << (ok ? "ok" : "FAIL") << " " << msg << "\n";
    printPresetState(p);
    return ok ? 0 : 5;
}

int cmdPresetSave(const juce::StringArray& args)
{
    if (args.size() < 2)
        return 1;
    Options o;
    if (!parseOptions(args, 2, o))
        return 1;
    DtBlkFxProcessor p;
    if (o.load != juce::File())
    {
        auto r = presetio::loadFile(p, o.load);
        std::cout << "load " << (r.ok ? "ok" : "FAIL") << " " << r.message << "\n";
        if (!r.ok)
            return 5;
    }
    applyOptions(p, o);
    auto r = presetio::saveFile(p, juce::File::getCurrentWorkingDirectory().getChildFile(args[1]));
    std::cout << "result " << (r.ok ? "ok" : "FAIL") << " " << r.message << "\n";
    printPresetState(p);
    return r.ok ? 0 : 5;
}

// --- simulated mouse input -----------------------------------------------------------------------
struct Mouse
{
    explicit Mouse(juce::Component& comp) : c(comp) {}
    juce::Component& c;
    juce::Point<float> downPos;
    juce::Time downTime = juce::Time::getCurrentTime();

    juce::MouseEvent ev(juce::Point<float> p, juce::ModifierKeys mods, bool dragged = false)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent(src, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(),
                                downPos, downTime, 1, dragged);
    }
    void down(juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        downPos = p;
        c.mouseEnter(ev(p, {}));
        c.mouseDown(ev(p, mods));
    }
    void drag(juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        c.mouseDrag(ev(p, mods, true));
    }
    void up(juce::Point<float> p, juce::ModifierKeys mods = {})
    {
        c.mouseUp(ev(p, mods, true));
    }
};

int cmdUi()
{
    DtBlkFxProcessor p;
    p.setCurrentProgram(1); // "4 band para"
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* content = ed->getChildComponent(0);
    auto* global = content->getChildComponent(0);
    auto* fx0 = content->getChildComponent(3);
    int fails = 0;
    auto check = [&](const char* what, bool ok, float before, float after, int idx) {
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << ": param " << idx << " " << before << " -> " << after
                  << " (\"" << p.param(idx)->getCurrentValueAsText() << "\")\n";
        if (!ok)
            ++fails;
    };

    {   // coarse drag on slot 0 amp: down, move down 5 px (picks COARSE), move right 40 px
        auto& amp = *fx0->getChildComponent(2);
        const float before = p.param(6)->get();
        Mouse m { amp };
        const juce::Point<float> c0 = amp.getLocalBounds().getCentre().toFloat();
        m.down(c0);
        m.drag(c0 + juce::Point<float>(0, 5));
        m.drag(c0 + juce::Point<float>(40, 5));
        m.up(c0 + juce::Point<float>(40, 5));
        const float after = p.param(6)->get();
        check("coarse drag amp +40px", std::abs((after - before) - 40.0f / 204.0f) < 0.01f, before, after, 6);
    }
    {   // fine drag on slot 1 FreqA: move up 5 px (picks FINE, 15x magnified), move right 40 px
        auto* fx1 = content->getChildComponent(4);
        auto& fa = *fx1->getChildComponent(0);
        const float before = p.param(9)->get();
        Mouse m { fa };
        const juce::Point<float> c0 = fa.getLocalBounds().getCentre().toFloat();
        m.down(c0);
        m.drag(c0 + juce::Point<float>(0, -5));
        m.drag(c0 + juce::Point<float>(40, -5));
        m.up(c0 + juce::Point<float>(40, -5));
        const float after = p.param(9)->get();
        check("fine drag freqA +40px", std::abs((after - before) - 40.0f / (204.0f * 15.0f)) < 0.002f, before, after, 9);
    }
    {   // shift-drag on slot 2 FreqB moves both frequencies
        auto* fx2 = content->getChildComponent(5);
        auto& fb = *fx2->getChildComponent(1);
        const float a0 = p.param(14)->get(), b0 = p.param(15)->get();
        Mouse m { fb };
        const juce::Point<float> c0 = fb.getLocalBounds().getCentre().toFloat();
        const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        m.down(c0, mods);
        m.drag(c0 + juce::Point<float>(-30, 0), mods);
        m.up(c0 + juce::Point<float>(-30, 0), juce::ModifierKeys::shiftModifier);
        const float a1 = p.param(14)->get(), b1 = p.param(15)->get();
        const float expect = -30.0f / 399.0f;
        check("shift-drag both, FreqA", std::abs((a1 - a0) - expect) < 0.01f, a0, a1, 14);
        check("shift-drag both, FreqB", std::abs((b1 - b0) - expect) < 0.01f, b0, b1, 15);
    }
    {   // click the Power toggle: match <-> filter
        auto& t = *global->getChildComponent(1);
        const float before = p.param(0)->get();
        Mouse m { t };
        const juce::Point<float> c0 = t.getLocalBounds().getCentre().toFloat();
        m.down(c0);
        m.up(c0);
        const float after = p.param(0)->get();
        check("click Power toggle", (before > 0.5f) != (after > 0.5f), before, after, 0);
    }
    {   // BlkLen drag snaps to block sizes
        auto& bs = *global->getChildComponent(5);
        const float before = p.param(2)->get();
        Mouse m { bs };
        const juce::Point<float> c0 = bs.getLocalBounds().getCentre().toFloat();
        m.down(c0);
        m.drag(c0 + juce::Point<float>(0, 5));
        m.drag(c0 + juce::Point<float>(30, 5));
        m.up(c0 + juce::Point<float>(30, 5));
        const float after = p.param(2)->get();
        const int plan = (int) BlkFxParam::getPlan(after);
        check("BlkLen coarse drag snaps", after > before && std::abs(after - BlkFxParam::getFFTLenParam(plan)) < 1e-6f, before, after, 2);
    }
    {   // wheel on slot 0 Val
        auto& v = *fx0->getChildComponent(4);
        const float before = p.param(8)->get();
        juce::MouseWheelDetails w {};
        w.deltaY = 0.25f;
        Mouse m { v };
        v.mouseWheelMove(m.ev(v.getLocalBounds().getCentre().toFloat(), {}), w);
        const float after = p.param(8)->get();
        check("wheel on Val", after > before, before, after, 8);
    }

    // the editor polls parameters on a timer; let it catch up, then save what it looks like
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    std::cout << (fails == 0 ? "UI OK" : "UI FAILURES") << "\n";
    return fails == 0 ? 0 : 4;
}

// --- popup sliders -------------------------------------------------------------------------------
juce::Image snap(juce::Component& ed)
{
    return ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
}

bool writePng(const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat png;
    return os.openedOk() && png.writeImageToStream(img, os);
}

int countDiff(const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return -1;
    const juce::Image::BitmapData da(a, juce::Image::BitmapData::readOnly), db(b, juce::Image::BitmapData::readOnly);
    int n = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (da.getPixelColour(x, y) != db.getPixelColour(x, y))
                ++n;
    return n;
}

int cmdPopup(const juce::StringArray& args)
{
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(args.size() > 1 ? args[1] : "docs");
    dir.createDirectory();
    int fails = 0;
    auto check = [&](const juce::String& what, bool ok) {
        if (!ok)
        {
            std::cout << "  FAIL " << what << "\n";
            ++fails;
        }
        return ok;
    };
    auto idle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };

    struct Case
    {
        const char* name;
        int row;   // -1 = global strip, 0..7 = effect rows
        int child; // control index in that row
    };
    const Case cases[] = { { "top-first", -1, 0 },   { "top-last", -1, 5 },  { "row1-first", 0, 0 },
                           { "row1-last", 0, 4 },    { "row4-amp", 3, 2 },   { "row8-first", 7, 0 },
                           { "row8-last", 7, 4 } };
    const float scales[] = { 1.0f, 1.5f, 2.0f };

    for (float scale : scales)
    {
        DtBlkFxProcessor p;
        p.setCurrentProgram(1); // "4 band para"
        // values that push the bars off the edges: high on the left, low on the right
        p.param(4)->setValueNotifyingHost(0.95f);  // slot 0 FreqA (first column)
        p.param(8)->setValueNotifyingHost(0.02f);  // slot 0 Val (last column)
        p.param(39)->setValueNotifyingHost(0.98f); // slot 7 FreqA
        p.param(43)->setValueNotifyingHost(0.03f); // slot 7 Val
        p.param(0)->setValueNotifyingHost(0.9f);   // MixBack
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(juce::roundToInt(dtgui::kWidth * scale), juce::roundToInt(dtgui::kHeight * scale));
        idle();
        auto* content = ed->getChildComponent(0);
        const auto edBounds = ed->getLocalBounds();
        const juce::String sx = juce::String(scale, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "x";

        for (auto& c : cases)
        {
            auto* row = content->getChildComponent(c.row < 0 ? 0 : 3 + c.row);
            auto* ctrl = dynamic_cast<dtgui::PopupHSlider*>(row->getChildComponent(c.child));
            const juce::String tag = juce::String(c.name) + " @" + sx;
            if (!check(tag + ": control is a PopupHSlider", ctrl != nullptr))
                continue;

            Mouse m { *ctrl };
            const auto c0 = ctrl->getLocalBounds().getCentre().toFloat();
            ctrl->mouseEnter(m.ev(c0, {}));
            idle();
            const auto before = snap(*ed);

            // --- open (mouse down) --------------------------------------------------------------
            const int childrenBefore = content->getNumChildComponents();
            m.down(c0);
            idle();
            check(tag + ": popup opens", ctrl->isPopupOpen());
            auto* host = ctrl->getPopupHost();
            if (!check(tag + ": popup host is the editor content", host == content))
            {
                m.up(c0);
                continue;
            }
            const auto cell = host->getLocalArea(ctrl, ctrl->getLocalBounds());
            const auto hostArea = host->getLocalBounds();
            const auto clickX = host->getLocalPoint(ctrl, c0).x;
            juce::Rectangle<int> barR[2], knobR[2];
            for (int i = 0; i < 2; ++i)
            {
                barR[i] = ctrl->getPopupBarBounds(i);
                knobR[i] = ctrl->getPopupKnobBounds(i);
            }
            const int F = dtgui::PopupHSlider::FINE, C = dtgui::PopupHSlider::COARSE;

            // inside the editor, in the editor's own (scaled) coordinates
            for (int i = 0; i < 2; ++i)
            {
                const auto eb = ed->getLocalArea(host, barR[i]), ek = ed->getLocalArea(host, knobR[i]);
                check(tag + ": " + (i == F ? "fine" : "coarse") + " bar inside editor " + eb.toString(), edBounds.contains(eb));
                check(tag + ": " + (i == F ? "fine" : "coarse") + " knob inside editor " + ek.toString(), edBounds.contains(ek));
                check(tag + ": bar is " + juce::String(209 * scale) + " px wide in the editor",
                      std::abs((float) eb.getWidth() - 209.0f * scale) <= 1.0f);
            }
            // fine above the cell, coarse below it; where one does not fit it goes on the far side of
            // the other (top row: cell, coarse, fine; bottom row: coarse, fine, cell)
            const bool fineMoved = cell.getY() - barR[F].getHeight() < hostArea.getY();
            const bool coarseMoved = cell.getBottom() - 4 + barR[C].getHeight() > hostArea.getBottom();
            check(tag + ": fine bar " + barR[F].toString() + " cell " + cell.toString(),
                  fineMoved ? barR[F].getY() == barR[C].getBottom() : barR[F].getBottom() == cell.getY());
            check(tag + ": coarse bar " + barR[C].toString(),
                  coarseMoved ? barR[C].getBottom() == barR[F].getY() : barR[C].getY() == cell.getBottom() - 4);
            for (int i = 0; i < 2; ++i) // the value being edited stays readable (the original overlapped 4 px)
                check(tag + ": bar clear of the clicked value", !barR[i].intersects(cell.withTrimmedBottom(4)));
            check(tag + ": bars do not overlap", !barR[F].intersects(barR[C]));
            // knobs lined up with each other; under the mouse unless a bar had to slide in, and then
            // that bar is at the edge margin (knob half width, minus the 2 px image border)
            const int kxF = knobR[F].getCentreX(), kxC = knobR[C].getCentreX();
            check(tag + ": knobs line up", std::abs(kxF - kxC) <= 1);
            const bool underMouse = std::abs((float) kxC - clickX) <= 1.0f;
            const bool atEdge = barR[C].getX() == hostArea.getX() + 9 || barR[C].getRight() == hostArea.getRight() - 9
                                || barR[F].getX() == hostArea.getX() + 9 || barR[F].getRight() == hostArea.getRight() - 9;
            check(tag + ": knob under the mouse, or a bar at the edge", underMouse || atEdge);
            {
                bool overlaps = false;
                for (int i = 0; i < 2; ++i)
                    overlaps |= barR[i].getX() < cell.getRight() && barR[i].getRight() > cell.getX();
                check(tag + ": a bar overlaps the cell sideways", overlaps);
            }

            const auto open = snap(*ed);
            check(tag + ": popup visible in the snapshot", countDiff(before, open) > 0);
            const auto png = dir.getChildFile("popup-" + juce::String(c.name) + "@" + sx + ".png");
            writePng(open, png);
            std::cout << "  " << tag << ": click x=" << clickX << " knob x=" << kxC << (underMouse ? " (under mouse)" : " (slid in)")
                      << " fine " << barR[F].toString() << " coarse " << barR[C].toString() << " -> " << png.getFileName() << "\n";

            // --- close (mouse up, no drag) and look for leftovers ---------------------------------
            const auto drawn = ctrl->getPopupDrawnArea();
            m.up(c0);
            idle();
            check(tag + ": popup closed", !ctrl->isPopupOpen());
            check(tag + ": overlay removed from the editor (" + juce::String(host->getNumChildComponents()) + " children)",
                  host->getNumChildComponents() == childrenBefore);
            check(tag + ": close repaints all it drew " + ctrl->getLastCloseRepaint().toString() + " >= " + drawn.toString(),
                  ctrl->getLastCloseRepaint().contains(drawn));
            const auto after = snap(*ed);
            const int diff = countDiff(before, after);
            check(tag + ": snapshot after close == before open (" + juce::String(diff) + " px differ)", diff == 0);

            // --- drag past both editor edges: bars stay inside, the value still reaches 0 and 1 ----
            {
                m.down(c0);
                m.drag(c0 + juce::Point<float>(0, 5)); // coarse
                const float edge = (float) ctrl->getWidth() + 600.0f;
                bool inside = true;
                float lastDx = 0;
                for (float dx = 0; dx <= edge; dx += 37.0f)
                {
                    lastDx = dx;
                    m.drag(c0 + juce::Point<float>(dx, 5));
                    for (int i = 0; i < 2; ++i)
                        inside &= hostArea.contains(ctrl->getPopupBarBounds(i)) && hostArea.contains(ctrl->getPopupKnobBounds(i));
                }
                const float vHi = ctrl->getValue();
                // back the other way: value comes down at once (no dead zone)
                m.drag(c0 + juce::Point<float>(lastDx - 20.0f, 5));
                const float vBack = ctrl->getValue();
                for (float dx = lastDx - 20.0f; dx >= -edge - 400.0f; dx -= 37.0f)
                {
                    m.drag(c0 + juce::Point<float>(dx, 5));
                    for (int i = 0; i < 2; ++i)
                        inside &= hostArea.contains(ctrl->getPopupBarBounds(i)) && hostArea.contains(ctrl->getPopupKnobBounds(i));
                }
                const float vLo = ctrl->getValue();
                // up into the fine bar without moving sideways: no jump
                const auto lastPos = c0 + juce::Point<float>(-edge - 400.0f, 5);
                const float vBefore = ctrl->getValue();
                const auto fineCentreY = (float) ctrl->getLocalArea(host, ctrl->getPopupBarBounds(F)).getCentreY();
                m.drag({ lastPos.x, fineCentreY });
                const float vSwitch = ctrl->getValue();
                // fine drag right 30 px: 30 / (204 * 15) of the range
                m.drag({ lastPos.x + 30.0f, fineCentreY });
                const float vFine = ctrl->getValue();
                m.up({ lastPos.x + 30.0f, fineCentreY });
                idle();
                const bool snaps = c.row < 0 && c.child == 5; // BlkLen snaps to block sizes
                check(tag + ": drag keeps the bars inside", inside);
                check(tag + ": drag right reaches the top (" + juce::String(vHi) + ")", snaps ? vHi >= 0.8f : vHi > 0.999f);
                check(tag + ": coming back moves at once (" + juce::String(vBack) + ")", snaps || vBack < vHi);
                check(tag + ": drag left reaches the bottom (" + juce::String(vLo) + ")", snaps ? vLo <= 0.2f : vLo < 0.001f);
                check(tag + ": switching to fine does not jump (" + juce::String(vBefore) + " -> " + juce::String(vSwitch) + ")",
                      snaps || std::abs(vSwitch - vBefore) < 1.5f / (204.0f * 15.0f));
                check(tag + ": fine drag 30 px (" + juce::String(vFine - vSwitch) + ")",
                      snaps || std::abs((vFine - vSwitch) - 30.0f / (204.0f * 15.0f)) < 1.5f / (204.0f * 15.0f));
                check(tag + ": closed after the drag", !ctrl->isPopupOpen());
            }
        }
    }
    std::cout << (fails == 0 ? "POPUP OK" : "POPUP FAILURES") << "\n";
    return fails == 0 ? 0 : 6;
}
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(argv[i]);

    if (args.isEmpty())
    {
        std::cerr << "usage: DtBlkFxHarness list | process <in> <out> [opts] | snapshot <png> [opts] | state\n";
        return 1;
    }
    if (args[0] == "list")
        return cmdList();
    if (args[0] == "process")
        return cmdProcess(args);
    if (args[0] == "snapshot")
        return cmdSnapshot(args);
    if (args[0] == "state")
        return cmdState();
    if (args[0] == "ui")
        return cmdUi();
    if (args[0] == "popup")
        return cmdPopup(args);
    if (args[0] == "preset-load")
        return cmdPresetLoad(args);
    if (args[0] == "preset-save")
        return cmdPresetSave(args);
    std::cerr << "unknown command\n";
    return 1;
}
