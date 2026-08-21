#pragma once

#include <JuceHeader.h>
#include <frust_plugin_host/FrustPluginHost.h>
#include <frust_plugin_host/FrustPluginManifest.h>
#include <memory>
#include <vector>

// The IDE as a real frust_plugin_host consumer, not just a library that
// happens to exist in the repo. Load any .frust file as an isolated,
// hot-reloadable plugin; if a plugin.json sits next to it, show its
// declared name/version/entry points. Call any function by name with an
// optional single i64 argument - deliberately generic (matches
// frust_plugin_get_fn's own "the host knows what to expect, this
// library doesn't" design), not tied to the on_init/on_event/on_unload
// convention specifically, though those work fine through the same Call
// box.
class PluginsPanel : public juce::Component,
                      private juce::ListBoxModel
{
public:
    PluginsPanel();
    ~PluginsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct LoadedPlugin {
        juce::String displayName;
        juce::File sourceFile;
        FrustPluginHandle handle = nullptr;
    };

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void loadPluginClicked();
    void reloadSelectedClicked();
    void unloadSelectedClicked();
    void callClicked();
    void refreshManifestView();
    void log(const juce::String& msg);

    int selectedIndex() const;

    std::vector<LoadedPlugin> loaded;

    juce::Label headerLabel { "Header", "Plugins (frust_plugin_host)" };
    juce::TextButton loadButton { "Load Plugin..." };
    juce::TextButton reloadButton { "Hot-Reload" };
    juce::TextButton unloadButton { "Unload" };
    juce::ListBox pluginList { "PluginList", this };

    juce::Label manifestLabel { "ManifestLabel", "Manifest (plugin.json)" };
    juce::TextEditor manifestView;

    juce::Label callLabel { "CallLabel", "Call function (i64 arg optional)" };
    juce::TextEditor callFnName;
    juce::TextEditor callFnArg;
    juce::TextButton callButton { "Call" };

    juce::Label logLabel { "LogLabel", "Log" };
    juce::TextEditor outputLog;

    std::unique_ptr<juce::FileChooser> activeFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginsPanel)
};
