#pragma once

#include <JuceHeader.h>
#include "Auth/DesktopAuthSession.h"
#include <frate/FrateConfig.h>
#include <frate/FrateRegistryClient.h>

class ConsumerView : public juce::Component, public juce::ListBoxModel
{
public:
    ConsumerView();
    ~ConsumerView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setProjectRoot(const juce::File& root);

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

private:
    void reloadConfig();
    void addDependency();
    void resolveAll();

    juce::File projectRoot;
    frate::FrateConfig currentConfig;
    frate::FrateRegistryClient registryClient;

    juce::ListBox dependencyList { "Dependencies", this };
    
    juce::Label addLabel { {}, "Add Dependency:" };
    juce::TextEditor nameInput;
    juce::TextEditor versionInput;
    juce::TextButton addButton { "Add" };
    juce::TextButton resolveButton { "Resolve All" };
    
    std::map<std::string, juce::String> resolveStatuses;
};

class ProducerView : public juce::Component
{
public:
    ProducerView(DesktopAuthSession* session);
    ~ProducerView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setProjectRoot(const juce::File& root);

    std::function<void()> onFileSystemChanged;

private:
    void createPod();
    void packagePod();
    void installToCache();
    void publishToRegistry();

    juce::File projectRoot;
    DesktopAuthSession* authSession;
    frate::FrateRegistryClient registryClient;

    juce::Label createLabel { {}, "Create New Pod:" };
    juce::TextEditor nameInput;
    juce::TextEditor versionInput;
    juce::TextEditor descInput;
    juce::TextButton createButton { "Scaffold Pod" };
    
    juce::Label actionsLabel { {}, "Actions on Current Directory:" };
    juce::TextButton packageButton { "Package (.frpod)..." };
    juce::TextButton installButton { "Install to Local Cache" };
    juce::TextButton publishButton { "Publish to Registry" };
    
    std::unique_ptr<juce::FileChooser> fileChooser;
};

class FratePanel : public juce::Component
{
public:
    FratePanel(DesktopAuthSession* authSession);
    ~FratePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void setProjectRoot(const juce::File& root);
    
    std::function<void()> onFileSystemChanged;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<ConsumerView> consumerView;
    std::unique_ptr<ProducerView> producerView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FratePanel)
};
