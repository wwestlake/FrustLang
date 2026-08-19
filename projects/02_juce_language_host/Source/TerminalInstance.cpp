#include "TerminalInstance.h"
#include <frate/FrateConfig.h>

namespace {
constexpr juce::uint32 kDefaultTextColour = 0xffd4d4d4U;
constexpr juce::uint32 kLogMessageColour  = 0xffd7ba7dU; // amber - IDE's own messages, distinct from real output
constexpr juce::uint32 kFrustBuiltColour  = 0xff6a9955U; // green - built and up to date
constexpr juce::uint32 kFrustStaleColour  = 0xffd7ba7dU; // amber - source newer than build
constexpr juce::uint32 kFrustMissingColour = 0xffcc6666U; // red - never built

// Standard ANSI SGR foreground colours (30-37, 90-97 bright), tuned to
// stay readable against this terminal's near-black background rather
// than using raw web-safe values.
constexpr juce::uint32 kAnsiColours[8] = {
    0xff5a5a5aU, 0xfff14c4cU, 0xff23d18bU, 0xfff5f543U,
    0xff3b8eeaU, 0xffd670d6U, 0xff29b8dbU, 0xffe5e5e5U,
};
constexpr juce::uint32 kAnsiBrightColours[8] = {
    0xff808080U, 0xffff6e6eU, 0xff4fffb0U, 0xffffff6bU,
    0xff6ab5ffU, 0xffff9dffU, 0xff5ee6ffU, 0xffffffffU,
};
} // namespace

TerminalInstance::TerminalInstance(const juce::String& shellType, std::function<juce::File()> getRoot)
    : getProjectRoot(std::move(getRoot)), currentShell(shellType)
{
    if (getProjectRoot) {
        currentWorkingDirectory = getProjectRoot();
    }
    if (!currentWorkingDirectory.isDirectory()) {
        currentWorkingDirectory = juce::File::getCurrentWorkingDirectory();
    }

    consoleText.setMultiLine(true, true);
    consoleText.setReturnKeyStartsNewLine(false);
    consoleText.setReadOnly(false);
    consoleText.setCaretVisible(true);
    consoleText.setFont(juce::Font("Consolas", fontSize, juce::Font::plain));
    consoleText.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0d0d));
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(kDefaultTextColour));
    consoleText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    consoleText.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    updatePromptText();
    appendPlainText(promptText, kDefaultTextColour);

    consoleText.onReturnKey = [this] { executeCommand(); };
    consoleText.addKeyListener(this);
    // TextEditor consumes wheel events itself for scrolling and doesn't
    // forward them to the parent Component's mouseWheelMove - registering
    // as a MouseListener on it directly is the correct JUCE mechanism to
    // still observe (and conditionally intercept, for Ctrl+wheel zoom)
    // wheel events targeting a child that would otherwise swallow them.
    consoleText.addMouseListener(this, false);
    addAndMakeVisible(consoleText);
}

TerminalInstance::~TerminalInstance()
{
    stopTimer();
    consoleText.removeKeyListener(this);
    consoleText.removeMouseListener(this);
    if (activeProcess) activeProcess->kill();
}

void TerminalInstance::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d0d0d));
}

void TerminalInstance::resized()
{
    consoleText.setBounds(getLocalBounds());
}

void TerminalInstance::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown()) {
        setFontSize(fontSize + (wheel.deltaY > 0.0f ? 1.0f : -1.0f));
        return;
    }
    consoleText.mouseWheelMove(event, wheel);
}

void TerminalInstance::setProjectRoot(const juce::File& root)
{
    if (root.isDirectory()) {
        currentWorkingDirectory = root;
        updatePromptText();
        appendPlainText(promptText, kDefaultTextColour);
        consoleText.moveCaretToEnd();
    }
}

juce::String TerminalInstance::buildFrustPromptSegment(juce::uint32& outColour) const
{
    juce::File frateJson = currentWorkingDirectory.getChildFile("frate.json");
    if (!frateJson.existsAsFile()) {
        outColour = 0;
        return {};
    }

    frate::FrateConfig config;
    if (!config.load(frateJson)) {
        outColour = 0;
        return {};
    }
    const auto& meta = config.getMetadata();
    if (meta.name.empty()) {
        outColour = 0;
        return {};
    }

    // Build-freshness, the same "clean vs dirty" question a git prompt
    // answers for commits vs working tree: compare build/<name>.o's
    // mtime against the newest .fr file directly under src/ - matches
    // frate's own buildPod() convention (non-recursive src/ scan, same
    // build/<name>.o output path).
    juce::File buildObj = currentWorkingDirectory.getChildFile("build").getChildFile(juce::String(meta.name) + ".o");
    juce::File srcDir = currentWorkingDirectory.getChildFile("src");

    juce::Time newestSource;
    if (srcDir.isDirectory()) {
        for (auto& f : srcDir.findChildFiles(juce::File::findFiles, false, "*.fr")) {
            auto t = f.getLastModificationTime();
            if (t > newestSource) newestSource = t;
        }
    }

    juce::String stateGlyph;
    if (!buildObj.existsAsFile()) {
        stateGlyph = "!";
        outColour = kFrustMissingColour;
    } else if (newestSource.toMilliseconds() > 0 && buildObj.getLastModificationTime() < newestSource) {
        stateGlyph = "~";
        outColour = kFrustStaleColour;
    } else {
        stateGlyph = "+";
        outColour = kFrustBuiltColour;
    }

    return "[" + juce::String(meta.name) + " v" + juce::String(meta.version) + " " + stateGlyph + "] ";
}

void TerminalInstance::updatePromptText()
{
    juce::String dirName = currentWorkingDirectory.getFileName();
    if (dirName.isEmpty()) dirName = currentWorkingDirectory.getFullPathName();

    juce::String shellPart;
#if JUCE_WINDOWS
    if (currentShell == "powershell") {
        shellPart = "PS [" + dirName + "]> ";
    } else {
        shellPart = "wsl [" + dirName + "]$ ";
    }
#else
    shellPart = "bash [" + dirName + "]$ ";
#endif

    juce::uint32 frustColour = 0;
    juce::String frustSegment = buildFrustPromptSegment(frustColour);

    // promptText stays the plain concatenation - executeCommand()/
    // keyPressed() locate it verbatim in the buffer to find where user
    // input begins, so it must exactly match what appendPlainText() below
    // renders, character for character.
    promptText = frustSegment + shellPart;
}

void TerminalInstance::executeCommand()
{
    if (activeProcess && activeProcess->isRunning()) {
        logMessage("A command is already running.");
        return;
    }

    auto fullText = consoleText.getText();
    auto lastPromptIndex = fullText.lastIndexOfIgnoreCase(promptText);
    if (lastPromptIndex < 0) return;

    auto input = fullText.substring(lastPromptIndex + promptText.length()).trim();
    if (input.isEmpty()) {
        appendPlainText("\n" + promptText, kDefaultTextColour);
        consoleText.moveCaretToEnd();
        return;
    }

    appendPlainText("\n", kDefaultTextColour);
    consoleText.moveCaretToEnd();

    if (input.startsWithIgnoreCase("cd ")) {
        auto target = input.substring(3).trim();
        if (target.startsWith("\"") && target.endsWith("\"")) {
            target = target.substring(1, target.length() - 1);
        }

        juce::File newDir;
        if (juce::File::isAbsolutePath(target)) {
            newDir = juce::File(target);
        } else {
            newDir = currentWorkingDirectory.getChildFile(target);
        }

        if (newDir.isDirectory()) {
            currentWorkingDirectory = newDir;
            updatePromptText();
        } else {
            logMessage("cd: cannot change directory to " + target + ": No such file or directory");
        }

        appendPlainText(promptText, kDefaultTextColour);
        consoleText.moveCaretToEnd();
        return;
    } else if (input.trim() == "cd") {
        if (getProjectRoot) {
            juce::File root = getProjectRoot();
            if (root.isDirectory()) {
                currentWorkingDirectory = root;
            }
        }
        updatePromptText();
        appendPlainText(promptText, kDefaultTextColour);
        consoleText.moveCaretToEnd();
        return;
    }

    juce::StringArray args;
#if JUCE_WINDOWS
    if (currentShell == "powershell") {
        args.add("powershell");
        args.add("-NoProfile");
        args.add("-Command");
        args.add("Push-Location -LiteralPath '" + currentWorkingDirectory.getFullPathName() + "'; " + input);
    } else {
        // "wsl" shell option: shells out through WSL to reach bash - a
        // Windows-only mechanism, so this whole branch only exists under
        // JUCE_WINDOWS. Native Linux (see #else below) runs bash directly,
        // no WSL involved, since it isn't a WSL guest to begin with.
        juce::String wslPath = currentWorkingDirectory.getFullPathName().replace("\\", "/");
        if (wslPath.length() >= 2 && wslPath[1] == ':') {
            juce::String drive = wslPath.substring(0, 1).toLowerCase();
            wslPath = "/mnt/" + drive + wslPath.substring(2);
        }

        args.add("wsl");
        args.add("--");
        args.add("bash");
        args.add("-c");
        args.add("cd '" + wslPath + "'; " + input);
    }
#else
    // Linux (and any other non-Windows target): there's no WSL/PowerShell
    // concept here - just run bash directly against the real path, no
    // translation needed since it's already a native Unix path.
    juce::ignoreUnused(currentShell);
    args.add("/bin/bash");
    args.add("-c");
    args.add("cd '" + currentWorkingDirectory.getFullPathName() + "'; " + input);
#endif

    activeProcess = std::make_unique<juce::ChildProcess>();

    juce::uint32 flags = juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr;
    if (activeProcess->start(args, flags)) {
        startTimer(50);
    } else {
        logMessage("Failed to start shell process.");
        appendPlainText(promptText, kDefaultTextColour);
        consoleText.moveCaretToEnd();
        activeProcess.reset();
    }
}

void TerminalInstance::timerCallback()
{
    if (activeProcess) {
        juce::String output = activeProcess->readAllProcessOutput();
        if (output.isNotEmpty()) {
            appendAnsiColouredText(output);
            consoleText.moveCaretToEnd();
        }

        if (!activeProcess->isRunning()) {
            stopTimer();
            updatePromptText(); // build-freshness may have just changed (e.g. this command was `frate build`)
            appendPlainText("\n" + promptText, kDefaultTextColour);
            consoleText.moveCaretToEnd();
            activeProcess.reset();
        }
    }
}

void TerminalInstance::logMessage(const juce::String& message)
{
    appendPlainText(message + "\n", kLogMessageColour);
    consoleText.moveCaretToEnd();
}

void TerminalInstance::appendPlainText(const juce::String& text, juce::uint32 argbColour)
{
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(argbColour));
    consoleText.moveCaretToEnd();
    consoleText.insertTextAtCaret(text);
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(kDefaultTextColour));
}

void TerminalInstance::appendAnsiColouredText(const juce::String& text)
{
    consoleText.moveCaretToEnd();

    juce::uint32 currentColour = kDefaultTextColour;
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(currentColour));

    juce::String plain;
    auto flush = [&] {
        if (plain.isNotEmpty()) {
            consoleText.insertTextAtCaret(plain);
            plain.clear();
        }
    };

    int i = 0;
    const int len = text.length();
    while (i < len) {
        if (text[i] == '\x1b' && i + 1 < len && text[i + 1] == '[') {
            int j = i + 2;
            while (j < len && !juce::CharacterFunctions::isLetter(text[j])) ++j;
            if (j < len) {
                if (text[j] == 'm') {
                    flush();
                    juce::String codesStr = text.substring(i + 2, j);
                    for (auto codeToken : juce::StringArray::fromTokens(codesStr, ";", "")) {
                        int code = codeToken.getIntValue();
                        if (code == 0) currentColour = kDefaultTextColour;
                        else if (code >= 30 && code <= 37) currentColour = kAnsiColours[static_cast<size_t>(code - 30)];
                        else if (code >= 90 && code <= 97) currentColour = kAnsiBrightColours[static_cast<size_t>(code - 90)];
                        // Other SGR codes (bold, background colours, underline,
                        // ...) are intentionally not interpreted in v1 - only
                        // the reset/foreground-colour codes affect currentColour.
                    }
                    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(currentColour));
                }
                // Non-'m' CSI sequences (cursor movement, clear-line, etc.)
                // are stripped silently either way - don't let raw escape
                // bytes leak into the visible text.
                i = j + 1;
                continue;
            }
        }
        plain += text[i];
        ++i;
    }
    flush();
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(kDefaultTextColour));
}

void TerminalInstance::setFontSize(float newSize)
{
    fontSize = juce::jlimit(6.0f, 40.0f, newSize);
    juce::Font newFont("Consolas", fontSize, juce::Font::plain);
    consoleText.applyFontToAllText(newFont);
    consoleText.setFont(newFont);
}

bool TerminalInstance::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    if (originatingComponent != &consoleText) return false;

    // Zoom: Ctrl/Cmd +, Ctrl/Cmd -, Ctrl/Cmd 0 to reset - same convention
    // as VSCode's own terminal and most editors.
    if (key.getModifiers().isCommandDown()) {
        auto ch = key.getTextCharacter();
        if (ch == '+' || ch == '=') { setFontSize(fontSize + 1.0f); return true; }
        if (ch == '-' || ch == '_') { setFontSize(fontSize - 1.0f); return true; }
        if (ch == '0') { setFontSize(13.0f); return true; }
    }

    auto fullText = consoleText.getText();
    auto promptIndex = fullText.lastIndexOfIgnoreCase(promptText);
    if (promptIndex < 0) return false;

    int inputStartIndex = promptIndex + promptText.length();
    int caretPos = consoleText.getCaretPosition();
    auto selection = consoleText.getHighlightedRegion();

    // Allow copying
    if ((key.getKeyCode() == 'c' || key.getKeyCode() == 'C') && key.getModifiers().isCommandDown()) {
        return false;
    }

    bool isModifyingKey = key.getTextCharacter() != 0 ||
                          key == juce::KeyPress::backspaceKey ||
                          key == juce::KeyPress::deleteKey ||
                          ((key.getKeyCode() == 'v' || key.getKeyCode() == 'V') && key.getModifiers().isCommandDown()) ||
                          ((key.getKeyCode() == 'x' || key.getKeyCode() == 'X') && key.getModifiers().isCommandDown());

    if (isModifyingKey) {
        // Prevent backspace from deleting the prompt
        if (key == juce::KeyPress::backspaceKey && caretPos <= inputStartIndex && selection.isEmpty()) {
            return true;
        }

        // If trying to modify read-only text, jump to the end
        if (caretPos < inputStartIndex || selection.getStart() < inputStartIndex) {
            consoleText.setCaretPosition(fullText.length());

            // Consume destructive keys entirely to prevent accidents
            if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey ||
               ((key.getKeyCode() == 'x' || key.getKeyCode() == 'X') && key.getModifiers().isCommandDown())) {
                return true;
            }
        }
    }

    return false;
}
