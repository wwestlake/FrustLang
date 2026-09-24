#include "EditorTabComponent.h"

// ------------------------------------------------------------------------------
// Single Code Editor Tab
// ------------------------------------------------------------------------------
CodeEditorTab::CodeEditorTab(const juce::File& file)
    : targetFile(file)
{
    editor.setFont(juce::Font("Consolas", editorFontSize, juce::Font::plain));
    editor.onZoom = [this](float direction) {
        editorFontSize = juce::jlimit(9.0f, 30.0f, editorFontSize + direction);
        editor.setFont(juce::Font("Consolas", editorFontSize, juce::Font::plain));
    };
    editor.setColourScheme(tokeniser.getDefaultColourScheme());
    editor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour(0xff1e1e1e));
    editor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour(0xff252526));
    editor.setColour(juce::CodeEditorComponent::lineNumberTextId, juce::Colour(0xff858585));
    editor.setTabSize(4, true);
    editor.setLineNumbersShown(true);

    if (targetFile.existsAsFile()) {
        suppressDirtyTracking = true;
        document.replaceAllContent(targetFile.loadFileAsString());
        suppressDirtyTracking = false;
        lastKnownModTime = targetFile.getLastModificationTime();
    }

    document.addListener(this);
    addAndMakeVisible(editor);

    statusBar.setFont(juce::Font(12.0f, juce::Font::plain));
    statusBar.setColour(juce::Label::textColourId, juce::Colours::grey);
    statusBar.setText("File: " + displayName() + " | Ready", juce::dontSendNotification);
    addAndMakeVisible(statusBar);

    startTimer(1000);
}

juce::String CodeEditorTab::displayName() const
{
    return targetFile.getFullPathName().isNotEmpty() ? targetFile.getFileName() : juce::String("Untitled");
}

CodeEditorTab::~CodeEditorTab()
{
    stopTimer();
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
        lastKnownModTime = targetFile.getLastModificationTime();
        isDirty = false;
        statusBar.setText("File: " + displayName() + " | Saved", juce::dontSendNotification);
    }
}

void CodeEditorTab::retarget(const juce::File& newFile)
{
    targetFile = newFile;
    lastKnownModTime = newFile.getLastModificationTime();
    statusBar.setText("File: " + displayName() + " | Renamed", juce::dontSendNotification);
}

void CodeEditorTab::saveAs(const juce::File& newFile)
{
    targetFile = newFile;
    saveFile();
}

void CodeEditorTab::goToLocation(int line, int column)
{
    if (line <= 0) return;
    const auto lineIndex = juce::jlimit(0, juce::jmax(0, document.getNumLines() - 1), line - 1);
    juce::CodeDocument::Position position(document, lineIndex, juce::jmax(0, column - 1));
    editor.moveCaretTo(position, false);
    editor.scrollToLine(lineIndex);
    editor.grabKeyboardFocus();
}

void CodeEditorTab::codeDocumentTextInserted(const juce::String&, int)
{
    if (!suppressDirtyTracking) isDirty = true;
    statusBar.setText("File: " + displayName() + " | Modified", juce::dontSendNotification);
}

void CodeEditorTab::codeDocumentTextDeleted(int, int)
{
    if (!suppressDirtyTracking) isDirty = true;
    statusBar.setText("File: " + displayName() + " | Modified", juce::dontSendNotification);
}

void CodeEditorTab::timerCallback()
{
    if (targetFile.getFullPathName().isEmpty() || !targetFile.existsAsFile()) return;
    if (isDirty) return; // never clobber unsaved local edits

    auto diskModTime = targetFile.getLastModificationTime();
    if (diskModTime == lastKnownModTime) return;

    suppressDirtyTracking = true;
    document.replaceAllContent(targetFile.loadFileAsString());
    suppressDirtyTracking = false;
    lastKnownModTime = diskModTime;
    statusBar.setText("File: " + displayName() + " | Reloaded (changed on disk)", juce::dontSendNotification);
}

// ------------------------------------------------------------------------------
// Multi-Tab Container
// ------------------------------------------------------------------------------
void ClosableEditorTabs::popupMenuClickOnTab(int tabIndex, const juce::String&)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Close");
    menu.showMenuAsync(juce::PopupMenu::Options(),
        [safeThis = juce::Component::SafePointer<ClosableEditorTabs>(this), tabIndex](int result) {
            if (safeThis != nullptr && result == 1 && safeThis->onCloseRequested)
                safeThis->onCloseRequested(tabIndex);
        });
}

EditorTabComponent::EditorTabComponent()
{
    tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xff333333));
    tabs.setColour(juce::TabbedButtonBar::tabTextColourId, juce::Colours::lightgrey);
    tabs.setColour(juce::TabbedButtonBar::frontTextColourId, juce::Colours::cyan);
    tabs.onCloseRequested = [this](int index) { closeTab(index); };
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
    addCloseButton(tabs.getNumTabs() - 1);
    tabs.setCurrentTabIndex(tabs.getNumTabs() - 1);

    if (onActiveFileChanged) onActiveFileChanged(file);
}

void EditorTabComponent::openFileAt(const juce::File& file, int line, int column)
{
    openFile(file);
    const auto current = tabs.getCurrentTabIndex();
    if (current >= 0)
        if (auto* tab = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(current)))
            tab->goToLocation(line, column);
}

void EditorTabComponent::newUntitledTab()
{
    int untitledCount = 1;
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (tabs.getTabNames()[i].startsWith("Untitled")) ++untitledCount;

    auto label = untitledCount == 1 ? juce::String("Untitled") : "Untitled " + juce::String(untitledCount);

    auto newTab = std::make_unique<CodeEditorTab>(juce::File());
    tabs.addTab(label, juce::Colour(0xff2d2d2d), newTab.release(), true);
    addCloseButton(tabs.getNumTabs() - 1);
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

void EditorTabComponent::fileMoved(const juce::File& from, const juce::File& to)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* tab = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(i)))
        {
            const auto file = tab->getFile();
            if (file == from || file.isAChildOf(from))
            {
                const auto moved = file == from ? to : to.getChildFile(file.getRelativePathFrom(from));
                tab->retarget(moved);
                tabs.getTabbedButtonBar().setTabName(i, moved.getFileName());
            }
        }
}

void EditorTabComponent::fileDeleted(const juce::File& file)
{
    for (int i = tabs.getNumTabs(); --i >= 0;)
        if (auto* tab = dynamic_cast<CodeEditorTab*>(tabs.getTabContentComponent(i)))
        {
            const auto open = tab->getFile();
            if ((open == file || open.isAChildOf(file)) && ! tab->hasUnsavedChanges())
                closeTab(i);
        }
}

void EditorTabComponent::closeActiveTab()
{
    closeTab(tabs.getCurrentTabIndex());
}

void EditorTabComponent::addCloseButton(int tabIndex)
{
    auto* tabButton = tabs.getTabbedButtonBar().getTabButton(tabIndex);
    if (tabButton == nullptr) return;

    auto* closeButton = new juce::TextButton("x");
    closeButton->setTooltip("Close tab");
    closeButton->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    closeButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff593333));
    closeButton->setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
    closeButton->onClick = [this, tabButton] {
        closeTab(tabs.getTabbedButtonBar().indexOfTabButton(tabButton));
    };
    tabButton->setExtraComponent(closeButton, juce::TabBarButton::afterText);
}

void EditorTabComponent::closeTab(int tabIndex)
{
    if (tabIndex >= 0 && tabIndex < tabs.getNumTabs())
        tabs.removeTab(tabIndex);
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
