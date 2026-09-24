#pragma once

#include <JuceHeader.h>
#include <frust_plugin_host/FrustPluginHost.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

struct PluginRuntimeState {
    FrustPluginHandle handle = nullptr;
    bool alive = true;
    juce::String displayName;
};

class PluginUiPanel : public juce::Component
{
public:
    PluginUiPanel(juce::String panelIdIn,
                  juce::String titleIn,
                  juce::var panelManifestIn,
                  std::shared_ptr<PluginRuntimeState> runtimeIn,
                  std::function<void(const juce::String&)> logFnIn);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void buildControls();
    void handleButtonClick(const juce::String& controlId);
    juce::var collectValues() const;
    void setStatus(const juce::String& text);

    juce::String panelId;
    juce::String title;
    juce::var panelManifest;
    std::shared_ptr<PluginRuntimeState> runtime;
    std::function<void(const juce::String&)> logFn;

    juce::Label titleLabel;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::map<juce::String, juce::TextEditor*> valueEditors;
    juce::TextEditor statusView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginUiPanel)
};
