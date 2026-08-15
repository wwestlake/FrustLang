#include "ConsolePanel.h"

const juce::String ConsolePanel::bannerText = "LagDaemon Language Research Console v0.1.0\nReady.\n";
const juce::String ConsolePanel::promptText = "fr-> ";

ConsolePanel::ConsolePanel()
    : replSession(std::make_unique<frust::ReplSession>())
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::lightcyan);
    addAndMakeVisible(headerLabel);

    // One scrolling, editable view - a real console, not a separate output
    // pane + input box. Return submits the current line (via
    // setReturnKeyStartsNewLine(false), which routes it to onReturnKey
    // instead of inserting a newline) rather than starting a new one.
    consoleText.setMultiLine(true, true);
    consoleText.setReturnKeyStartsNewLine(false);
    consoleText.setReadOnly(false);
    consoleText.setCaretVisible(true);
    consoleText.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    consoleText.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141414));
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(0xff00ff66));
    consoleText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    consoleText.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    consoleText.setText(bannerText + promptText);
    consoleText.onReturnKey = [this] { evaluateInput(); };
    addAndMakeVisible(consoleText);

    clearButton.onClick = [this] { clearConsole(); };
    addAndMakeVisible(clearButton);

    resetButton.onClick = [this] { resetSession(); };
    addAndMakeVisible(resetButton);

    saveButton.onClick = [this] { saveSessionToFile(); };
    addAndMakeVisible(saveButton);

    loadButton.onClick = [this] { loadSessionFromFile(); };
    addAndMakeVisible(loadButton);
}

ConsolePanel::~ConsolePanel() = default;

void ConsolePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void ConsolePanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto topBar = bounds.removeFromTop(24);
    loadButton.setBounds(topBar.removeFromRight(50));
    topBar.removeFromRight(4);
    saveButton.setBounds(topBar.removeFromRight(50));
    topBar.removeFromRight(4);
    resetButton.setBounds(topBar.removeFromRight(55));
    topBar.removeFromRight(4);
    clearButton.setBounds(topBar.removeFromRight(50));
    headerLabel.setBounds(topBar);

    bounds.removeFromTop(4);
    consoleText.setBounds(bounds);
}

void ConsolePanel::evaluateInput()
{
    // Not tracked via caret position - deliberately just takes everything
    // after the last prompt in the buffer, so it still does the right
    // thing even if the user clicked around before pressing Return.
    auto fullText = consoleText.getText();
    auto lastPromptIndex = fullText.lastIndexOfIgnoreCase(promptText);
    if (lastPromptIndex < 0) return;

    auto input = fullText.substring(lastPromptIndex + promptText.length()).trim();

    juce::String output;
    if (input.isNotEmpty()) {
        // JIT-compiling and running is fast (microseconds for expressions
        // this small) but still real work - do it synchronously for now
        // rather than adding a background-thread/callback path before
        // there's evidence it's needed on the message thread.
        auto result = replSession->evaluate(input.toStdString());
        if (!result.empty()) output = "\n  => " + juce::String(result);
        if (onSessionChanged) onSessionChanged();
    }

    consoleText.setText(fullText + output + "\n" + promptText);
    consoleText.moveCaretToEnd();
}

void ConsolePanel::runScript(const juce::String& source, const juce::String& label)
{
    if (source.trim().isEmpty()) return;

    logMessage("(running " + label + " in the console session)");

    auto results = replSession->runScript(source.toStdString());
    for (auto& result : results) logMessage("  => " + juce::String(result));

    if (onSessionChanged) onSessionChanged();
}

void ConsolePanel::resetSession()
{
    replSession->reset();
    logMessage("(session reset - all bound variables forgotten)");
    if (onSessionChanged) onSessionChanged();
}

juce::File ConsolePanel::getSessionFile() const
{
    auto root = getProjectRoot ? getProjectRoot() : juce::File();
    if (!root.isDirectory())
        root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    return root.getChildFile(".frust").getChildFile("repl_session.json");
}

void ConsolePanel::saveSessionToFile()
{
    auto file = getSessionFile();
    file.getParentDirectory().createDirectory();

    if (file.replaceWithText(juce::String(replSession->exportAsJson())))
        logMessage("(session saved to " + file.getFullPathName() + ")");
    else
        logMessage("(failed to save session to " + file.getFullPathName() + ")");
}

void ConsolePanel::loadSessionFromFile()
{
    auto file = getSessionFile();
    if (!file.existsAsFile()) {
        logMessage("(no saved session at " + file.getFullPathName() + ")");
        return;
    }

    if (replSession->importFromJson(file.loadFileAsString().toStdString())) {
        logMessage("(session loaded from " + file.getFullPathName() + ")");
        if (onSessionChanged) onSessionChanged();
    } else
        logMessage("(failed to parse session file " + file.getFullPathName() + ")");
}

void ConsolePanel::logMessage(const juce::String& message)
{
    // Inserted before the live prompt line so it doesn't get swallowed into
    // whatever the user is mid-typing.
    auto fullText = consoleText.getText();
    auto lastPromptIndex = fullText.lastIndexOfIgnoreCase(promptText);
    auto insertAt = lastPromptIndex >= 0 ? lastPromptIndex : fullText.length();

    consoleText.setText(fullText.substring(0, insertAt) + message + "\n" + fullText.substring(insertAt));
    consoleText.moveCaretToEnd();
}

void ConsolePanel::clearConsole()
{
    consoleText.setText(bannerText + promptText);
    consoleText.moveCaretToEnd();
}
