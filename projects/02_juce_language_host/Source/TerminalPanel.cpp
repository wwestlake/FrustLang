#include "TerminalPanel.h"

TerminalPanel::TerminalPanel(juce::ApplicationProperties* props)
    : appProperties(props)
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::lightcyan);
    addAndMakeVisible(headerLabel);

#if JUCE_WINDOWS
    shellSelector.addItem("PowerShell", 1);
    shellSelector.addItem("WSL (Ubuntu)", 2);

    defaultShell = "powershell";
    if (appProperties) {
        auto* settings = appProperties->getUserSettings();
        defaultShell = settings->getValue("defaultTerminalShell", "powershell");
    }

    shellSelector.setSelectedId(defaultShell == "powershell" ? 1 : 2, juce::dontSendNotification);

    shellSelector.onChange = [this] {
        defaultShell = shellSelector.getSelectedId() == 1 ? "powershell" : "wsl";
        if (appProperties) {
            appProperties->getUserSettings()->setValue("defaultTerminalShell", defaultShell);
            appProperties->getUserSettings()->saveIfNeeded();
        }
    };
    addAndMakeVisible(shellSelector);
#else
    // No PowerShell, no WSL on Linux - bash is the only real option, so
    // there's nothing meaningful for a selector to choose between. Kept
    // as a single-item combo (not hidden) so the layout in resized()
    // doesn't need a platform-specific branch too.
    shellSelector.addItem("bash", 1);
    defaultShell = "bash";
    shellSelector.setSelectedId(1, juce::dontSendNotification);
    shellSelector.setEnabled(false);
    addAndMakeVisible(shellSelector);
#endif

    newTerminalButton.onClick = [this] { addNewTerminal(defaultShell); };
    addAndMakeVisible(newTerminalButton);

    closeTerminalButton.onClick = [this] { closeActiveTerminal(); };
    addAndMakeVisible(closeTerminalButton);

    tabbedComponent.setOutline(0);
    tabbedComponent.setTabBarDepth(24);
    addAndMakeVisible(tabbedComponent);
    
    // Auto-create default terminal
    addNewTerminal(defaultShell);
}

TerminalPanel::~TerminalPanel()
{
}

void TerminalPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void TerminalPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto topBar = bounds.removeFromTop(24);
    closeTerminalButton.setBounds(topBar.removeFromRight(24));
    topBar.removeFromRight(4);
    newTerminalButton.setBounds(topBar.removeFromRight(24));
    topBar.removeFromRight(4);
    shellSelector.setBounds(topBar.removeFromRight(120));
    headerLabel.setBounds(topBar);

    bounds.removeFromTop(4);
    tabbedComponent.setBounds(bounds);
}

void TerminalPanel::addNewTerminal(const juce::String& type)
{
#if JUCE_WINDOWS
    juce::String label = (type == "powershell" ? "PS" : "WSL");
#else
    juce::ignoreUnused(type);
    juce::String label = "bash";
#endif
    juce::String name = label + juce::String(" #") + juce::String(terminalCounter++);
    auto* term = new TerminalInstance(type, [this] { return getProjectRoot ? getProjectRoot() : juce::File(); });
    tabbedComponent.addTab(name, juce::Colour(0xff2d2d2d), term, true);
    tabbedComponent.setCurrentTabIndex(tabbedComponent.getNumTabs() - 1);
}

void TerminalPanel::closeActiveTerminal()
{
    int index = tabbedComponent.getCurrentTabIndex();
    if (index >= 0) {
        tabbedComponent.removeTab(index);
    }
}

void TerminalPanel::setProjectRoot(const juce::File& root)
{
    for (int i = 0; i < tabbedComponent.getNumTabs(); ++i) {
        if (auto* term = dynamic_cast<TerminalInstance*>(tabbedComponent.getTabContentComponent(i))) {
            term->setProjectRoot(root);
        }
    }
}
