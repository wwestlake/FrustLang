#pragma once

#include <JuceHeader.h>

// A bidirectional shell process. JUCE ChildProcess is output-only, so it
// cannot preserve shell state between commands.
class PersistentShellSession final
{
public:
    PersistentShellSession() = default;
    ~PersistentShellSession();

    bool start(const juce::String& shell, const juce::File& workingDirectory, juce::String& error);
    bool send(const juce::String& command, juce::String& error);
    juce::String readAvailableOutput();
    void stop();
    bool isRunning() const noexcept;

private:
#if JUCE_WINDOWS
    void* processHandle = nullptr;
    void* threadHandle = nullptr;
    void* inputWriteHandle = nullptr;
    void* outputReadHandle = nullptr;
#endif
};
