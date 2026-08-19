#pragma once

#include <JuceHeader.h>
#include <memory>

// juce::Component already inherits juce::MouseListener internally, so
// addMouseListener(this, ...) below works without listing it explicitly
// as a base class too (that would just be redundant multiple inheritance
// of the same interface Component already provides).
class TerminalInstance : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    TerminalInstance(const juce::String& shellType, std::function<juce::File()> getRoot);
    ~TerminalInstance() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setProjectRoot(const juce::File& root);
    juce::String getShellType() const { return currentShell; }

private:
    void executeCommand();
    void logMessage(const juce::String& message);
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void updatePromptText();

    // Appends `text` to consoleText, interpreting ANSI SGR colour escapes
    // (\x1b[...m) rather than letting them leak into the visible text as
    // garbage bytes - most CLI tools (ls --color, git, grep --color, ...)
    // emit these. Other CSI sequences (cursor movement, clear-line, etc.)
    // are silently stripped, not interpreted. Insertion is incremental
    // (insertTextAtCaret), not the old whole-buffer setText() rebuild -
    // required for per-run colour to actually work in a JUCE TextEditor.
    void appendAnsiColouredText(const juce::String& text);

    // Inserts plain text at a single fixed colour (used for our own
    // messages/prompts, as opposed to process output which goes through
    // appendAnsiColouredText).
    void appendPlainText(const juce::String& text, juce::uint32 argbColour);

    // Applies fontSize to all existing text and future insertions - Ctrl+=
    // / Ctrl+- / Ctrl+0, matching standard terminal/editor zoom conventions.
    void setFontSize(float newSize);

    // Frust-aware prompt segment, analogous to a git-aware shell prompt's
    // branch/dirty-state segment: reads frate.json in the current
    // directory (if any) via the same FrateConfig/PodMetadata this IDE
    // already links for the Frate panel, and reports pod name/version
    // plus a build-freshness indicator (compares build/<name>.o's mtime
    // against the newest source file's, the same "built vs stale vs never
    // built" question git's dirty-state answers for commits).
    juce::String buildFrustPromptSegment(juce::uint32& outColour) const;

    juce::TextEditor consoleText;

    std::function<juce::File()> getProjectRoot;
    juce::File currentWorkingDirectory;
    juce::String promptText;
    juce::String currentShell;
    float fontSize = 13.0f;

    std::unique_ptr<juce::ChildProcess> activeProcess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TerminalInstance)
};
