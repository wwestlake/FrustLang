#pragma once

#include <JuceHeader.h>
#include <frust_plugin_host/FrustPluginHost.h>
#include <frust_plugin_host/FrustPluginManifest.h>
#include <memory>
#include <vector>

// The IDE as a real frust_plugin_host consumer, not just a library that
// happens to exist in the repo. On construction, scans the built-in
// plugins/ folder (via frust_plugin_peek_manifest - reads each
// plugin's embedded manifest without linking it into the JIT or
// loading it) and lists what's AVAILABLE, with its real metadata and
// compatibility status - not a file-open dialog. A discovered plugin
// loads only when the user clicks Load, or automatically at startup if
// marked auto-load (a real, persisted per-plugin setting via
// juce::ApplicationProperties). Call any loaded plugin's function by
// name with an optional single i64 argument - deliberately generic
// (matches frust_plugin_get_fn's own "the host knows what to expect,
// this library doesn't" design).
class PluginsPanel : public juce::Component,
                      private juce::ListBoxModel
{
public:
    // appProperties is not owned - same instance WorkbenchComponent
    // already uses for lastOpenedFolder etc. May be nullptr (auto-load
    // marking then just doesn't persist across restarts).
    explicit PluginsPanel(juce::ApplicationProperties* appProperties);
    ~PluginsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct LoadedPlugin {
        juce::String displayName;
        juce::File sourceFile;
        FrustPluginHandle handle = nullptr;
    };

    // One entry discovered by scanning the built-in plugins/ folder -
    // metadata only (from frust_plugin_peek_manifest), not yet loaded.
    struct DiscoveredPlugin {
        juce::File sourceFile;
        juce::String name, version, description;
        bool isCompatible = false;
        juce::String incompatibilityReason;
        bool autoLoad = false;
    };

    // juce::ListBoxModel - the "currently loaded" list (unchanged
    // behavior from before this rework).
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // A second, small ListBoxModel for the "available/discovered" list
    // - juce::ListBoxModel has no built-in way for one model instance
    // to serve two independently-selectable lists, so this is its own
    // tiny adapter delegating back into PluginsPanel's own state,
    // rather than trying to overload PluginsPanel's existing model for
    // both lists at once.
    struct DiscoveredListModel : public juce::ListBoxModel {
        explicit DiscoveredListModel(PluginsPanel& ownerIn) : owner(ownerIn) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
        PluginsPanel& owner;
    };
    DiscoveredListModel discoveredListModel { *this };

    void scanBuiltInPluginsFolder();
    void loadSelectedDiscoveredClicked();
    void toggleAutoLoadClicked();
    void refreshDiscoveredDetailView();
    void loadAutoLoadMarkedPlugins();
    // Actually loads `d` (frust_plugin_load + on_init), appending it to
    // `loaded` - shared by the manual "Load Selected" button and
    // startup auto-load, same real load path either way.
    void loadDiscovered(const DiscoveredPlugin& d);
    int selectedDiscoveredIndex() const;

    void reloadSelectedClicked();
    void unloadSelectedClicked();
    void callClicked();
    void refreshManifestView();
    void log(const juce::String& msg);

    int selectedIndex() const;

    std::vector<LoadedPlugin> loaded;
    std::vector<DiscoveredPlugin> discovered;

    juce::ApplicationProperties* appProperties = nullptr;

    juce::Label headerLabel { "Header", "Plugins (frust_plugin_host)" };

    juce::Label availableLabel { "AvailableLabel", "Available Plugins" };
    juce::ListBox discoveredListBox { "DiscoveredList", &discoveredListModel };
    juce::TextButton rescanButton { "Rescan" };
    juce::TextButton loadSelectedButton { "Load Selected" };
    juce::TextButton toggleAutoLoadButton { "Toggle Auto-Load" };
    juce::TextEditor discoveredDetailView;

    juce::TextButton reloadButton { "Hot-Reload" };
    juce::TextButton unloadButton { "Unload" };
    juce::ListBox pluginList { "PluginList", this };

    juce::Label manifestLabel { "ManifestLabel", "Manifest" };
    juce::TextEditor manifestView;

    juce::Label callLabel { "CallLabel", "Call function (i64 arg optional)" };
    juce::TextEditor callFnName;
    juce::TextEditor callFnArg;
    juce::TextButton callButton { "Call" };

    juce::Label logLabel { "LogLabel", "Log" };
    juce::TextEditor outputLog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginsPanel)
};
