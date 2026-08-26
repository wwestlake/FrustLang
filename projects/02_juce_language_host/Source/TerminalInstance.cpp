#include "TerminalInstance.h"

namespace { constexpr auto outputBackground = 0xff0d0d0d; constexpr auto outputText = 0xffd4d4d4; }

TerminalInstance::TerminalInstance(const juce::String& shellType, std::function<juce::File()> getRoot)
    : getProjectRoot(std::move(getRoot)), currentShell(shellType),
      tokeniser(shellType == "bash" ? ShellTokeniser::Language::bash : ShellTokeniser::Language::powerShell)
{
    currentWorkingDirectory = getProjectRoot ? getProjectRoot() : juce::File();
    if (!currentWorkingDirectory.isDirectory()) currentWorkingDirectory = juce::File::getCurrentWorkingDirectory();
    output.setMultiLine(true, true); output.setReadOnly(true); output.setScrollbarsShown(true);
    output.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    output.setColour(juce::TextEditor::backgroundColourId, juce::Colour(outputBackground));
    output.setColour(juce::TextEditor::textColourId, juce::Colour(outputText));
    output.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(output);
    prompt.setFont(juce::Font("Consolas", 13.0f, juce::Font::bold)); addAndMakeVisible(prompt);
    inputEditor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour(outputBackground));
    inputEditor.setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colour(outputText));
    inputEditor.setColourScheme(tokeniser.getDefaultColourScheme()); inputEditor.addKeyListener(this); addAndMakeVisible(inputEditor);
    juce::String error;
    if (session.start(currentShell, currentWorkingDirectory, error)) { appendOutput(juce::String(currentShell == "bash" ? "Bash" : "PowerShell") + " session started.\n"); startTimer(30); }
    else appendOutput("Terminal error: " + error + "\n");
    updatePrompt();
}

TerminalInstance::~TerminalInstance() { stopTimer(); inputEditor.removeKeyListener(this); }
void TerminalInstance::paint(juce::Graphics& g) { g.fillAll(juce::Colour(outputBackground)); }
void TerminalInstance::resized() { auto bounds = getLocalBounds().reduced(6); auto inputArea = bounds.removeFromBottom(58); output.setBounds(bounds); prompt.setBounds(inputArea.removeFromLeft(230)); inputEditor.setBounds(inputArea); }
void TerminalInstance::setProjectRoot(const juce::File& root) { if (root.isDirectory()) { currentWorkingDirectory = root; updatePrompt(); } }
void TerminalInstance::updatePrompt() { const auto bash = currentShell == "bash"; prompt.setText(juce::String(bash ? "bash" : "PS") + " [" + currentWorkingDirectory.getFullPathName() + (bash ? "] $" : "] >"), juce::dontSendNotification); prompt.setColour(juce::Label::textColourId, bash ? juce::Colour(0xff4ec9b0) : juce::Colour(0xff569cd6)); }
void TerminalInstance::appendOutput(const juce::String& text) { output.moveCaretToEnd(); output.insertTextAtCaret(text.replace("\r", "")); output.moveCaretToEnd(); }
void TerminalInstance::updateWorkingDirectoryFromCommand(const juce::String& command) { auto trimmed = command.trim(); if (!trimmed.startsWithIgnoreCase("cd ") && !trimmed.startsWithIgnoreCase("set-location ")) return; auto target = trimmed.fromFirstOccurrenceOf(" ", false, false).trim().unquoted(); auto candidate = juce::File::isAbsolutePath(target) ? juce::File(target) : currentWorkingDirectory.getChildFile(target); if (candidate.isDirectory()) currentWorkingDirectory = candidate; updatePrompt(); }
void TerminalInstance::executeCommand() { const auto command = inputDocument.getAllContent().trimEnd(); if (command.isEmpty()) return; commandHistory.removeString(command); commandHistory.add(command); historyIndex = commandHistory.size(); appendOutput(prompt.getText() + " " + command + "\n"); juce::String error; if (!session.send(command + "\r\n", error)) appendOutput("Terminal error: " + error + "\n"); updateWorkingDirectoryFromCommand(command); inputDocument.replaceAllContent({}); }
void TerminalInstance::timerCallback() { const auto response = session.readAvailableOutput(); if (response.isNotEmpty()) appendOutput(response); if (!session.isRunning()) { appendOutput("\nTerminal session ended.\n"); stopTimer(); } }
bool TerminalInstance::keyPressed(const juce::KeyPress& key, juce::Component*) { if (key == juce::KeyPress::returnKey) { executeCommand(); return true; } if (key == juce::KeyPress::upKey && !commandHistory.isEmpty()) { historyIndex = juce::jmax(0, historyIndex - 1); inputDocument.replaceAllContent(commandHistory[historyIndex]); inputEditor.moveCaretToEnd(false); return true; } if (key == juce::KeyPress::downKey && !commandHistory.isEmpty()) { historyIndex = juce::jmin(commandHistory.size(), historyIndex + 1); inputDocument.replaceAllContent(historyIndex == commandHistory.size() ? juce::String() : commandHistory[historyIndex]); inputEditor.moveCaretToEnd(false); return true; } return false; }
