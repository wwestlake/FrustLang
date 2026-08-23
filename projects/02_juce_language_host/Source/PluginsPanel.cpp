#include "PluginsPanel.h"

PluginsPanel::PluginsPanel(juce::ApplicationProperties* appPropertiesIn)
    : appProperties(appPropertiesIn)
{
    headerLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(headerLabel);

    availableLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(availableLabel);
    discoveredListBox.setRowHeight(22);
    addAndMakeVisible(discoveredListBox);

    rescanButton.onClick = [this] { scanBuiltInPluginsFolder(); };
    addAndMakeVisible(rescanButton);
    loadSelectedButton.onClick = [this] { loadSelectedDiscoveredClicked(); };
    addAndMakeVisible(loadSelectedButton);
    toggleAutoLoadButton.onClick = [this] { toggleAutoLoadClicked(); };
    addAndMakeVisible(toggleAutoLoadButton);

    discoveredDetailView.setMultiLine(true);
    discoveredDetailView.setReadOnly(true);
    discoveredDetailView.setScrollbarsShown(true);
    addAndMakeVisible(discoveredDetailView);

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

    scanBuiltInPluginsFolder();
    loadAutoLoadMarkedPlugins();
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

    availableLabel.setBounds(bounds.removeFromTop(18));
    discoveredListBox.setBounds(bounds.removeFromTop(90));
    bounds.removeFromTop(4);

    auto discoveredButtonRow = bounds.removeFromTop(26);
    rescanButton.setBounds(discoveredButtonRow.removeFromLeft(80));
    discoveredButtonRow.removeFromLeft(4);
    loadSelectedButton.setBounds(discoveredButtonRow.removeFromLeft(110));
    discoveredButtonRow.removeFromLeft(4);
    toggleAutoLoadButton.setBounds(discoveredButtonRow.removeFromLeft(130));
    bounds.removeFromTop(4);

    discoveredDetailView.setBounds(bounds.removeFromTop(70));
    bounds.removeFromTop(8);

    auto buttonRow = bounds.removeFromTop(28);
    reloadButton.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(4);
    unloadButton.setBounds(buttonRow.removeFromLeft(80));
    bounds.removeFromTop(6);

    pluginList.setBounds(bounds.removeFromTop(90));
    bounds.removeFromTop(6);

    manifestLabel.setBounds(bounds.removeFromTop(18));
    manifestView.setBounds(bounds.removeFromTop(70));
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

// --- "Currently loaded" list (juce::ListBoxModel) ---

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

// --- "Available" (discovered-but-not-loaded) list ---

int PluginsPanel::DiscoveredListModel::getNumRows()
{
    return static_cast<int>(owner.discovered.size());
}

void PluginsPanel::DiscoveredListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected) g.fillAll(juce::Colour(0xff2d4a6a));
    if (rowNumber < 0 || rowNumber >= static_cast<int>(owner.discovered.size())) return;
    auto& d = owner.discovered[static_cast<size_t>(rowNumber)];
    g.setColour(d.isCompatible ? juce::Colours::white : juce::Colours::orange);
    juce::String line;
    if (d.autoLoad) line << "[AUTO] ";
    line << d.name << " v" << d.version;
    if (!d.isCompatible) line << "  (incompatible)";
    g.drawText(line, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void PluginsPanel::DiscoveredListModel::selectedRowsChanged(int)
{
    owner.refreshDiscoveredDetailView();
}

int PluginsPanel::selectedDiscoveredIndex() const
{
    return discoveredListBox.getSelectedRow();
}

void PluginsPanel::scanBuiltInPluginsFolder()
{
    // FRUST_IDE_SOURCE_DIR is a compile-time constant (CMakeLists.txt)
    // pointing at this project's own source directory - the built
    // .exe runs from bin/Debug/, nowhere near the source tree, so a
    // relative path or the working directory can't be trusted to find
    // plugins/ reliably.
    juce::File pluginsDir = juce::File(FRUST_IDE_SOURCE_DIR).getChildFile("plugins");

    discovered.clear();

    if (!pluginsDir.isDirectory()) {
        log("Built-in plugins folder not found: " + pluginsDir.getFullPathName());
        discoveredListBox.updateContent();
        return;
    }

    juce::StringArray autoLoadPaths;
    if (appProperties) {
        autoLoadPaths.addLines(appProperties->getUserSettings()->getValue("autoLoadPluginPaths"));
        autoLoadPaths.trim();
        autoLoadPaths.removeEmptyStrings();
    }

    juce::Array<juce::File> files;
    pluginsDir.findChildFiles(files, juce::File::findFiles, false, "*.frust;*.fr");

    for (auto& f : files) {
        // frust_plugin_peek_manifest reads the embedded manifest
        // (parse+codegen, no JIT link, no compatibility gate) without
        // committing to loading anything - lets an incompatible
        // plugin still show up in the browser with a real reason, not
        // silently vanish the way frust_plugin_load's refusal would.
        FrustPluginManifestHandle m = frust_plugin_peek_manifest(f.getFullPathName().toRawUTF8());
        if (!m) {
            log("Skipped (no readable manifest): " + f.getFileName() + " - " + juce::String(frust_plugin_last_error()));
            continue;
        }

        DiscoveredPlugin d;
        d.sourceFile = f;
        d.name = frust_plugin_manifest_name(m);
        d.version = frust_plugin_manifest_version(m);
        const char* desc = frust_plugin_manifest_description(m);
        d.description = (desc && *desc) ? juce::String(desc) : juce::String();
        d.isCompatible = frust_plugin_manifest_is_compatible(m) != 0;
        d.incompatibilityReason = d.isCompatible ? juce::String() : juce::String(frust_plugin_manifest_incompatibility_reason());
        d.autoLoad = autoLoadPaths.contains(f.getFullPathName());
        frust_plugin_manifest_free(m);

        discovered.push_back(std::move(d));
    }

    discoveredListBox.updateContent();
    log("Scanned " + pluginsDir.getFullPathName() + " - " + juce::String(discovered.size()) + " plugin(s) found.");
}

void PluginsPanel::loadDiscovered(const DiscoveredPlugin& d)
{
    FrustPluginHandle h = frust_plugin_load(d.sourceFile.getFullPathName().toRawUTF8());
    if (!h) {
        // This IDE is a windowed app with no console (juce_add_gui_app)
        // - stderr is genuinely invisible, so frust_plugin_last_error()
        // is the only way the user ever finds out why a load failed.
        log("FAILED to load: " + d.name + " - " + juce::String(frust_plugin_last_error()));
        return;
    }
    frust_plugin_call_on_init(h);

    LoadedPlugin p;
    p.displayName = d.sourceFile.getFileName();
    p.sourceFile = d.sourceFile;
    p.handle = h;
    loaded.push_back(p);
    pluginList.updateContent();
    pluginList.selectRow(static_cast<int>(loaded.size()) - 1);
    log("Loaded: " + d.name);
}

void PluginsPanel::loadSelectedDiscoveredClicked()
{
    int idx = selectedDiscoveredIndex();
    if (idx < 0 || idx >= static_cast<int>(discovered.size())) {
        log("Select an available plugin first.");
        return;
    }
    loadDiscovered(discovered[static_cast<size_t>(idx)]);
}

void PluginsPanel::toggleAutoLoadClicked()
{
    int idx = selectedDiscoveredIndex();
    if (idx < 0 || idx >= static_cast<int>(discovered.size())) {
        log("Select an available plugin first.");
        return;
    }
    auto& d = discovered[static_cast<size_t>(idx)];
    d.autoLoad = !d.autoLoad;

    if (appProperties) {
        juce::StringArray autoLoadPaths;
        for (auto& p : discovered) {
            if (p.autoLoad) autoLoadPaths.add(p.sourceFile.getFullPathName());
        }
        appProperties->getUserSettings()->setValue("autoLoadPluginPaths", autoLoadPaths.joinIntoString("\n"));
        appProperties->getUserSettings()->saveIfNeeded();
    }

    discoveredListBox.repaintRow(idx);
    refreshDiscoveredDetailView();
    log(juce::String(d.autoLoad ? "Auto-load ENABLED for " : "Auto-load disabled for ") + d.name);
}

void PluginsPanel::loadAutoLoadMarkedPlugins()
{
    for (auto& d : discovered) {
        if (d.autoLoad) loadDiscovered(d);
    }
}

void PluginsPanel::refreshDiscoveredDetailView()
{
    int idx = selectedDiscoveredIndex();
    if (idx < 0 || idx >= static_cast<int>(discovered.size())) {
        discoveredDetailView.setText({});
        return;
    }
    auto& d = discovered[static_cast<size_t>(idx)];

    juce::String text;
    text << "name: " << d.name << "\n";
    text << "version: " << d.version << "\n";
    if (d.description.isNotEmpty()) text << "description: " << d.description << "\n";
    text << "source: " << d.sourceFile.getFullPathName() << "\n";
    text << "compatible: " << (d.isCompatible ? "yes" : "no");
    if (!d.isCompatible) text << "  (" << d.incompatibilityReason << ")";
    text << "\n";
    text << "auto-load: " << (d.autoLoad ? "ON" : "off");
    discoveredDetailView.setText(text);
}

void PluginsPanel::reloadSelectedClicked()
{
    int idx = selectedIndex();
    if (idx < 0 || idx >= static_cast<int>(loaded.size())) return;

    auto& p = loaded[static_cast<size_t>(idx)];
    FrustPluginHandle newHandle = frust_plugin_reload(p.handle);
    // frust_plugin_reload() tears down the old handle either way, even
    // on failure (see FrustPluginHost.h) - don't try to keep using it.
    // (frust_plugin_reload() itself re-runs on_init() on a genuine
    // content-changed reload, so event/service registrations survive -
    // no separate call needed here, unlike loadDiscovered's first load.)
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

    // Reads the SAME manifest frust_plugin_load() itself already
    // parsed and verified for this handle - not a second,
    // independently-read copy (there's no companion file to read
    // anymore; the manifest lives embedded in the plugin's own source).
    FrustPluginManifestHandle m = frust_plugin_get_manifest(p.handle);
    if (!m) {
        manifestView.setText("(no manifest on this handle - should not happen for a loaded plugin)");
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
