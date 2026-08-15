#include "EditorTabComponent.h"

// ------------------------------------------------------------------------------
// Single Code Editor Tab
// ------------------------------------------------------------------------------
CodeEditorTab::CodeEditorTab(const juce::File& file)
    : targetFile(file)
{
    editor.setFont(juce::Font("Consolas", 14.0f, juce::Font::plain));
    editor.setColourScheme(tokeniser.getDefaultColourScheme());
    editor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour(0xff1e1e1e));
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour(0xff252526));
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour(0xff858585));
    editor.setTabSize(4, true);
    editor.setLineNumbersShown(true);

    if (targetFile.existsAsFile()) document.replaceAllContent(targetFile.loadFileAsString());

    document.addListener(this);
    addAndMakeVisible(editor);

    statusBar.setFont(juce::Font(12.0f, juce::Font::plain));
    statusBar.setColour(juce::Label::textColourId, juce::Colours::grey);
    statusBar.setText("File: " + displayName() + " | Ready", juce::dontSendNotification);
    addAndMakeVisible(statusBar);
}

juce::String CodeEditorTab::displayName() const
{
    return targetFile.getFullPathName().isNotEmpty() ? targetFile.getFileName() : juce::String("Untitled");
}

CodeEditorTab::~CodeEditorTab()
{
    document.removeListener(this);
}

void CodeEditorTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void CodeEditorTab::resized()
{
    auto bounds = getLocalBounds();
    statusBar.setBounds(bounds.removeFromBottom(20));
    editor.setBounds(bounds);
}

void CodeEditorTab::saveFile()
{
    if (targetFile.getFullPathName().isEmpty()) return; // untitled - caller should route to saveAs() instead

    if (targetFile.existsAsFile() || targetFile.create()) {
        targetFile.replaceWithText(document.getAllContent());
        statusBar.setText("File: " + displayName() + " | Saved", juce::dontSendNotification);
    }
}

void CodeEditorTab::saveAs(const juce::File& newFile)
{
    targetFile = newFile;
    saveFile();
}

void CodeEditorTab::codeDocumentTextInserted(const juce::String&, int)
{
    statusBar.setText("File: " + displayName() + " | Modified", juce::dontSendNotification);
}

void CodeEditorTab::codeDocumentTextDeleted(int, int)
{
    statusBar.setText("File: " + displayName() + " | Modified", juce::dontSendNotification);
}

// ------------------------------------------------------------------------------
// Multi-Tab Container
// ------------------------------------------------------------------------------
EditorTabComponent::EditorTabComponent()
{
    tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xff333333));
    tabs.setColour(juce::TabbedButtonBar::tabTextColourId, juce::Colours::lightgrey);
    tabs.setColour(juce::TabbedButtonBar::frontTextColourId, juce::Colours::cyan);
    addAndMakeVisible(tabs);

    // An editor with zero tabs just looks broken/empty, not like an editor
    // at all - it should always have a live, editable buffer, exactly like
    // opening any real code editor for the first time.
    newUntitledTab();
}

void EditorTabComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff252526));
}

void EditorTabComponent::resized()
{
    tabs.setBounds(getLocalBounds());
}

void EditorTabComponent::openFile(const juce::File& file)
{
    // Check if tab already exists
    for (int i = 0; i < tabs.getNumTabs(); ++i) {
        if (auto* tabComp = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(i))) {
            if (tabComp->getFile() == file) {
                tabs.setCurrentTabIndex(i);
                return;
            }
        }
    }

    auto newTab = std::make_unique<CodeEditorTab>(file);
    tabs.addTab(file.getFileName(), juce::Colour(0xff2d2d2d), newTab.release(), true);
    tabs.setCurrentTabIndex(tabs.getNumTabs() - 1);

    if (onActiveFileChanged) onActiveFileChanged(file);
}

void EditorTabComponent::newUntitledTab()
{
    int untitledCount = 1;
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (tabs.getTabNames()[i].startsWith("Untitled")) ++untitledCount;

    auto label = untitledCount == 1 ? juce::String("Untitled") : "Untitled " + juce::String(untitledCount);

    auto newTab = std::make_unique<CodeEditorTab>(juce::File());
    tabs.addTab(label, juce::Colour(0xff2d2d2d), newTab.release(), true);
    tabs.setCurrentTabIndex(tabs.getNumTabs() - 1);
}

void EditorTabComponent::saveActiveFile()
{
    int current = tabs.getCurrentTabIndex();
    if (current >= 0) {
        if (auto* tabComp = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(current))) {
            tabComp->saveFile();
        }
    }
}

void EditorTabComponent::saveActiveFileAs(const juce::File& newFile)
{
    int current = tabs.getCurrentTabIndex();
    if (current < 0) return;

    if (auto* tabComp = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(current))) {
        tabComp->saveAs(newFile);
        tabs.getTabbedButtonBar().setTabName(current, newFile.getFileName());
    }
}

void EditorTabComponent::closeActiveTab()
{
    int current = tabs.getCurrentTabIndex();
    if (current >= 0) {
        tabs.removeTab(current);
    }
}

juce::String EditorTabComponent::getActiveFileContent() const
{
    int current = tabs.getCurrentTabIndex();
    if (current < 0) return {};
    if (auto* tabComp = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(current)))
        return tabComp->getContent();
    return {};
}

juce::File EditorTabComponent::getActiveFile() const
{
    int current = tabs.getCurrentTabIndex();
    if (current < 0) return {};
    if (auto* tabComp = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(current)))
        return tabComp->getFile();
    return {};
}
