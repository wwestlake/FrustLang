#include "PluginsPanel.h"

PluginsPanel::PluginsPanel()
{
    headerLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(headerLabel);

    loadButton.onClick = [this] { loadPluginClicked(); };
    addAndMakeVisible(loadButton);

    reloadButton.onClick = [this] { reloadSelectedClicked(); };
    addAndMakeVisible(reloadButton);

    unloadButton.onClick = [this] { unloadSelectedClicked(); };
    addAndMakeVisible(unloadButton);

    pluginList.setRowHeight(22);
    addAndMakeVisible(pluginList);

    manifestLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(manifestLabel);
    manifestView.setMultiLine(true);
    manifestView.setReadOnly(true);
    manifestView.setScrollbarsShown(true);
    addAndMakeVisible(manifestView);

    callLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(callLabel);
    callFnName.setTextToShowWhenEmpty("function name", juce::Colours::grey);
    addAndMakeVisible(callFnName);
    callFnArg.setTextToShowWhenEmpty("arg (optional)", juce::Colours::grey);
    addAndMakeVisible(callFnArg);
    callButton.onClick = [this] { callClicked(); };
    addAndMakeVisible(callButton);

    logLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(logLabel);
    outputLog.setMultiLine(true);
    outputLog.setReadOnly(true);
    outputLog.setScrollbarsShown(true);
    addAndMakeVisible(outputLog);
}

PluginsPanel::~PluginsPanel()
{
    for (auto& p : loaded) {
        if (p.handle) frust_plugin_unload(p.handle);
    }
}

void PluginsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void PluginsPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    headerLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(4);

    auto buttonRow = bounds.removeFromTop(28);
    loadButton.setBounds(buttonRow.removeFromLeft(120));
    buttonRow.removeFromLeft(4);
    reloadButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(4);
    unloadButton.setBounds(buttonRow.removeFromLeft(80));
    bounds.removeFromTop(6);

    pluginList.setBounds(bounds.removeFromTop(120));
    bounds.removeFromTop(6);

    manifestLabel.setBounds(bounds.removeFromTop(18));
    manifestView.setBounds(bounds.removeFromTop(90));
    bounds.removeFromTop(6);

    callLabel.setBounds(bounds.removeFromTop(18));
    auto callRow = bounds.removeFromTop(26);
    callFnName.setBounds(callRow.removeFromLeft(callRow.getWidth() / 2 - 4));
    callRow.removeFromLeft(8);
    callFnArg.setBounds(callRow.removeFromLeft(callRow.getWidth() - 68));
    callRow.removeFromLeft(4);
    callButton.setBounds(callRow);
    bounds.removeFromTop(6);

    logLabel.setBounds(bounds.removeFromTop(18));
    outputLog.setBounds(bounds);
}

int PluginsPanel::getNumRows()
{
    return static_cast<int>(loaded.size());
}

void PluginsPanel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected) g.fillAll(juce::Colour(0xff2d4a6a));
    if (rowNumber < 0 || rowNumber >= static_cast<int>(loaded.size())) return;
    g.setColour(juce::Colours::white);
    g.drawText(loaded[static_cast<size_t>(rowNumber)].displayName, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void PluginsPanel::selectedRowsChanged(int)
{
    refreshManifestView();
}

int PluginsPanel::selectedIndex() const
{
    return pluginList.getSelectedRow();
}

void PluginsPanel::loadPluginClicked()
{
    activeFileChooser = std::make_unique<juce::FileChooser>(
        "Load a .frust plugin...",
        juce::File::getCurrentWorkingDirectory(),
        "*.frust;*.fr");

    activeFileChooser->launchAsync(juce::FileBrowserComponent::openMode, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        activeFileChooser = nullptr;
        if (!file.existsAsFile()) return;

        FrustPluginHandle h = frust_plugin_load(file.getFullPathName().toRawUTF8());
        if (!h) {
            // This IDE is a windowed app with no console (juce_add_gui_app)
            // - stderr is genuinely invisible, so frust_plugin_last_error()
            // is the only way the user ever finds out why a load failed.
            log("FAILED to load: " + file.getFileName() + " - " + juce::String(frust_plugin_last_error()));
            return;
        }

        // Was missing entirely - on_init() was never called on load, so
        // nothing a plugin registers there (event handlers, services -
        // see docs/17-Plugin-Automation-Layer.md) ever actually took
        // effect when loaded through this panel, even though every CLI
        // verification harness for that machinery calls it correctly.
        frust_plugin_call_on_init(h);

        LoadedPlugin p;
        p.displayName = file.getFileName();
        p.sourceFile = file;
        p.handle = h;
        loaded.push_back(p);
        pluginList.updateContent();
        pluginList.selectRow(static_cast<int>(loaded.size()) - 1);
        log("Loaded: " + file.getFileName());
    });
}

void PluginsPanel::reloadSelectedClicked()
{
    int idx = selectedIndex();
    if (idx < 0 || idx >= static_cast<int>(loaded.size())) return;

    auto& p = loaded[static_cast<size_t>(idx)];
    FrustPluginHandle newHandle = frust_plugin_reload(p.handle);
    // frust_plugin_reload() tears down the old handle either way, even
    // on failure (see FrustPluginHost.h) - don't try to keep using it.
    // (frust_plugin_reload() itself now re-runs on_init() on a genuine
    // content-changed reload, so event/service registrations survive -
    // no separate call needed here, unlike loadPluginClicked's first load.)
    p.handle = newHandle;
    if (!newHandle) {
        log("Hot-reload FAILED for " + p.displayName + " - " + juce::String(frust_plugin_last_error()) + " (it is now unloaded)");
        loaded.erase(loaded.begin() + idx);
        pluginList.updateContent();
        refreshManifestView();
        return;
    }
    log("Hot-reloaded: " + p.displayName);
}

void PluginsPanel::unloadSelectedClicked()
{
    int idx = selectedIndex();
    if (idx < 0 || idx >= static_cast<int>(loaded.size())) return;

    auto& p = loaded[static_cast<size_t>(idx)];
    frust_plugin_unload(p.handle);
    log("Unloaded: " + p.displayName);
    loaded.erase(loaded.begin() + idx);
    pluginList.updateContent();
    refreshManifestView();
}

void PluginsPanel::refreshManifestView()
{
    int idx = selectedIndex();
    if (idx < 0 || idx >= static_cast<int>(loaded.size())) {
        manifestView.setText({});
        return;
    }
    auto& p = loaded[static_cast<size_t>(idx)];
    juce::File manifestFile = p.sourceFile.getSiblingFile(p.sourceFile.getFileNameWithoutExtension() + ".json");
    if (!manifestFile.existsAsFile()) {
        manifestFile = p.sourceFile.getSiblingFile("plugin.json");
    }
    if (!manifestFile.existsAsFile()) {
        manifestView.setText("(no plugin.json found next to " + p.sourceFile.getFileName() + ")");
        return;
    }

    FrustPluginManifestHandle m = frust_plugin_manifest_load(manifestFile.getFullPathName().toRawUTF8());
    if (!m) {
        manifestView.setText("(failed to parse " + manifestFile.getFileName() + ")");
        return;
    }

    juce::String text;
    text << "name: " << frust_plugin_manifest_name(m) << "\n";
    text << "version: " << frust_plugin_manifest_version(m) << "\n";
    const char* desc = frust_plugin_manifest_description(m);
    if (desc && *desc) text << "description: " << desc << "\n";
    int count = frust_plugin_manifest_entry_point_count(m);
    text << "entry points:";
    if (count == 0) text << " (none declared)";
    for (int i = 0; i < count; ++i) {
        text << "\n  - " << frust_plugin_manifest_entry_point(m, i);
    }
    manifestView.setText(text);
    frust_plugin_manifest_free(m);
}

void PluginsPanel::callClicked()
{
    int idx = selectedIndex();
    if (idx < 0 || idx >= static_cast<int>(loaded.size())) {
        log("Select a loaded plugin first.");
        return;
    }
    auto& p = loaded[static_cast<size_t>(idx)];

    juce::String fnName = callFnName.getText().trim();
    if (fnName.isEmpty()) {
        log("Enter a function name to call.");
        return;
    }

    void* rawFn = frust_plugin_get_fn(p.handle, fnName.toRawUTF8());
    if (!rawFn) {
        log("No such function '" + fnName + "' in " + p.displayName);
        return;
    }

    juce::String argText = callFnArg.getText().trim();
    if (argText.isEmpty()) {
        auto fn = reinterpret_cast<int64_t(*)()>(rawFn);
        int64_t result = fn();
        log(p.displayName + "." + fnName + "() => " + juce::String(result));
    } else {
        int64_t arg = argText.getLargeIntValue();
        auto fn = reinterpret_cast<int64_t(*)(int64_t)>(rawFn);
        int64_t result = fn(arg);
        log(p.displayName + "." + fnName + "(" + juce::String(arg) + ") => " + juce::String(result));
    }
}

void PluginsPanel::log(const juce::String& msg)
{
    outputLog.moveCaretToEnd();
    outputLog.insertTextAtCaret(msg + "\n");
}
