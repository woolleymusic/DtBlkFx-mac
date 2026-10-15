#include "PluginEditor.h"
#include "GuiBridge.h"
#include "PresetIO.h"
#include "gui/Skin.h"

#include "DtBlkFx.hpp"

using namespace dtgui;

DtBlkFxEditor::DtBlkFxEditor(DtBlkFxProcessor& p) : juce::AudioProcessorEditor(p), proc(p)
{
    auto& e = proc.engine();
    auto& model = proc.guiBridge().sgram;

    // Gui::open: reset the spectrograms to the splash and start feeding them
    {
        ScopeCriticalSection lock(e._protect);
        model.open(Skin::get().splash, e.getSampleRate());
    }

    addAndMakeVisible(content);
    content.setBounds(0, 0, kWidth, kHeight);

    global = std::make_unique<GlobalCtrlView>(e);
    content.addAndMakeVisible(*global);
    global->setBounds(0, 0, kWidth, kGlobalH);

    int y = kGlobalH;
    for (int i = 0; i < 2; ++i)
    {
        sgram[i] = std::make_unique<SpectrogramView>(e, model, i);
        content.addAndMakeVisible(*sgram[i]);
        sgram[i]->setBounds(0, y, kWidth, kSgramH);
        y += kSgramH;
    }
    sgram[0]->setLinked(sgram[1].get());
    sgram[1]->setLinked(sgram[0].get());
    for (auto& sg : sgram)
    {
        sg->extendMenu = [this](juce::PopupMenu& m) { addPresetItems(m); };
        sg->onExtraMenuItem = [this](int id) { presetMenuAction(id); };
    }

    auto send = [this](int index, float v) { proc.setParameterFromUi(index, v); };
    auto gesture = [this](int index, bool begin) {
        if (auto* prm = proc.param(index))
        {
            if (begin)
                prm->beginChangeGesture();
            else
                prm->endChangeGesture();
        }
    };
    global->sendParam = send;
    global->gesture = gesture;

    for (int i = 0; i < kNumFx; ++i)
    {
        auto row = std::make_unique<FxCtrlView>(e, i, sgram[0].get(), sgram[1].get());
        content.addAndMakeVisible(*row);
        row->setBounds(0, y, kWidth, kFxRowH);
        row->sendParam = send;
        row->gesture = gesture;
        row->onChanged = [this] {
            for (auto& r : fx)
                r->repaint();
        };
        y += kFxRowH;
        fx.push_back(std::move(row));
    }

    cache.assign((size_t) proc.numParams(), -1.0f);
    timerCallback(); // load params into the controls (Gui::open does this too)

    setResizable(true, true);
    setResizeLimits(kWidth, kHeight, kWidth * 3, kHeight * 3);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double) kWidth / (double) kHeight);
    setSize(kWidth, kHeight);

    startTimerHz(30);
}

DtBlkFxEditor::~DtBlkFxEditor()
{
    stopTimer();
    auto& e = proc.engine();
    ScopeCriticalSection lock(e._protect);
    proc.guiBridge().sgram.close();
}

void DtBlkFxEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void DtBlkFxEditor::resized()
{
    const float scale = (float) getWidth() / (float) kWidth;
    content.setTransform(juce::AffineTransform::scale(scale));
}

void DtBlkFxEditor::dispatch(int index, float v)
{
    // Gui::setParameter
    BlkFxParam::SplitParamNum p(index);
    if (p.glob_param >= 0)
        global->setParameter(p.glob_param, v);
    else if (p.fx_param >= 0 && p.fx_set >= 0 && p.fx_set < (int) fx.size())
    {
        fx[(size_t) p.fx_set]->setParameter(p.fx_param, v);
        // an effect type change can change how the row above is drawn (mask joins)
        if (p.fx_param == BlkFxParam::FX_TYPE)
            for (auto& r : fx)
                r->repaint();
    }
}

void DtBlkFxEditor::timerCallback()
{
    for (int i = 0; i < proc.numParams(); ++i)
    {
        const float v = proc.param(i)->get();
        if (!juce::exactlyEqual(v, cache[(size_t) i]))
        {
            cache[(size_t) i] = v;
            dispatch(i, v);
        }
    }

    for (auto& s : sgram)
        s->refresh(false);

    // displays that depend on tempo / sample rate (delay in beats, blk size *) refresh now and then
    if (++tick % 15 == 0)
        global->repaint();
}

//==============================================================================
// Presets. The original had no load/save of its own (the VST2 host did it), so the items go at the
// bottom of the spectrogram menu, the one menu that is not about a value.
namespace
{
juce::File& lastPresetDir()
{
    static juce::File dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return dir;
}
} // namespace

void DtBlkFxEditor::addPresetItems(juce::PopupMenu& m) const
{
    m.addSeparator();
    m.addItem(kLoadPreset, "Load preset / bank (.fxp .fxb)...");
    m.addItem(kSavePreset, "Save preset (.fxp)...");
    m.addItem(kSaveBank, "Save bank (.fxb)...");
}

void DtBlkFxEditor::reportPreset(bool ok, const juce::String& message)
{
    lastPresetMessage = message;
#ifndef DTBLKFX_HEADLESS
    if (!ok)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "DtBlkFx", message, {}, this);
#endif
}

void DtBlkFxEditor::presetMenuAction(int id)
{
    if (id == kLoadPreset)
    {
        chooser = std::make_unique<juce::FileChooser>("Load DtBlkFx preset or bank", lastPresetDir(), "*.fxp;*.fxb");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<DtBlkFxEditor>(this)](const juce::FileChooser& fc) {
                                 if (safe == nullptr || fc.getResult() == juce::File())
                                     return;
                                 const auto f = fc.getResult();
                                 lastPresetDir() = f.getParentDirectory();
                                 auto r = presetio::loadFile(safe->proc, f);
                                 safe->reportPreset(r.ok, r.message);
                             });
    }
    else if (id == kSavePreset || id == kSaveBank)
    {
        const bool bank = id == kSaveBank;
        auto name = bank ? juce::String("DtBlkFx bank") : proc.getProgramName(proc.getCurrentProgram());
        name = juce::File::createLegalFileName(name.trim().isEmpty() ? juce::String("DtBlkFx") : name.trim());
        const auto start = lastPresetDir().getChildFile(name + (bank ? ".fxb" : ".fxp"));
        chooser = std::make_unique<juce::FileChooser>(bank ? "Save DtBlkFx bank" : "Save DtBlkFx preset", start,
                                                      bank ? "*.fxb" : "*.fxp");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<DtBlkFxEditor>(this), bank](const juce::FileChooser& fc) {
                                 if (safe == nullptr || fc.getResult() == juce::File())
                                     return;
                                 auto f = fc.getResult().withFileExtension(bank ? "fxb" : "fxp");
                                 lastPresetDir() = f.getParentDirectory();
                                 auto r = presetio::saveFile(safe->proc, f);
                                 safe->reportPreset(r.ok, r.message);
                             });
    }
}

bool DtBlkFxEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
        if (presetio::isPresetFile(f))
            return true;
    return false;
}

void DtBlkFxEditor::fileDragEnter(const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void DtBlkFxEditor::fileDragExit(const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void DtBlkFxEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    for (auto& path : files)
        if (presetio::isPresetFile(path))
        {
            const juce::File f(path);
            lastPresetDir() = f.getParentDirectory();
            auto r = presetio::loadFile(proc, f);
            reportPreset(r.ok, r.message);
            return; // one preset at a time
        }
}

void DtBlkFxEditor::paintOverChildren(juce::Graphics& g)
{
    if (dragHover)
    {
        g.setColour(focusTxt());
        g.drawRect(getLocalBounds(), 3);
    }
}
