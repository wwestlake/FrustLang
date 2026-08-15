#pragma once

#include <JuceHeader.h>
#include <ReplSession.h>
#include <functional>
#include <memory>

class ConsolePanel : public juce::Component
{
public:
    ConsolePanel();
    ~ConsolePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void logMessage(const juce::String& message);
    void clearConsole();

    // F#-Interactive-style "run this into the live session" - evaluates
    // every top-level statement in `source` against the same ReplSession
    // the console's own prompt uses (so bindings a script makes are
    // visible afterward at the prompt, and vice versa), echoing each
    // result into the transcript. `label` is just what gets printed in the
    // header line (e.g. the file name) - purely cosmetic.
    void runScript(const juce::String& source, const juce::String& label);

    // Lets the owner (WorkbenchComponent) tell this panel where "the
    // project" is, so Save/Load can write next to the user's actual files
    // instead of some fixed app-data path. Unset (or returning an invalid
    // File) falls back to the user's documents folder.
    std::function<juce::File()> getProjectRoot;

    // For a companion panel (e.g. ContextPanel) to read the live session's
    // bound variables - this owns the one real ReplSession instance, other
    // panels just look at it, they don't get their own.
    frust::ReplSession* getReplSession() const { return replSession.get(); }

    // Fired after evaluate()/reset()/load() actually change the bound-
    // variable set, so a companion panel knows to re-pull and redraw.
    std::function<void()> onSessionChanged;

private:
    void evaluateInput();
    void resetSession();
    void saveSessionToFile();
    void loadSessionFromFile();
    juce::File getSessionFile() const;

    juce::Label headerLabel { "Header", "Output & REPL Console" };
    juce::TextEditor consoleText;
    juce::TextButton clearButton { "Clear" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };

    static const juce::String bannerText;
    static const juce::String promptText;

    // In-process Frust evaluation - no subprocess, no marshalling, matching
    // the language's own "embedded dynamic code creation" design (see
    // FRUST_LANG_SPEC.md). `let` bindings persist across lines within this
    // session (see ReplSession's header comment for how) and can be saved
    // to / loaded from a JSON file via saveSessionToFile()/loadSessionFromFile().
    std::unique_ptr<frust::ReplSession> replSession;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsolePanel)
};
