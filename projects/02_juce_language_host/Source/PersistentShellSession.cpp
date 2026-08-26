#include "PersistentShellSession.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

PersistentShellSession::~PersistentShellSession()
{
    stop();
}

bool PersistentShellSession::start(const juce::String& shell, const juce::File& workingDirectory, juce::String& error)
{
    stop();
#if JUCE_WINDOWS
    SECURITY_ATTRIBUTES attributes { sizeof(attributes), nullptr, TRUE };
    HANDLE outputRead = nullptr, outputWrite = nullptr, inputRead = nullptr, inputWrite = nullptr;
    if (!CreatePipe(&outputRead, &outputWrite, &attributes, 0) || !SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0)
        || !CreatePipe(&inputRead, &inputWrite, &attributes, 0) || !SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0)) {
        error = "Could not create terminal pipes.";
        if (outputRead) CloseHandle(outputRead); if (outputWrite) CloseHandle(outputWrite);
        if (inputRead) CloseHandle(inputRead); if (inputWrite) CloseHandle(inputWrite);
        return false;
    }

    STARTUPINFOW startup { sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    PROCESS_INFORMATION process {};
    const auto executable = shell == "bash" ? L"wsl.exe" : L"powershell.exe";
    juce::String command = shell == "bash" ? "wsl.exe -- bash -i" : "powershell.exe -NoLogo -NoProfile -NoExit";
    std::vector<wchar_t> mutableCommand(command.toWideCharPointer(), command.toWideCharPointer() + command.length() + 1);
    const auto directory = workingDirectory.getFullPathName().toWideCharPointer();
    if (!CreateProcessW(executable, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, directory, &startup, &process)) {
        error = "Could not start " + shell + ".";
        CloseHandle(outputRead); CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(inputWrite);
        return false;
    }
    CloseHandle(inputRead); CloseHandle(outputWrite);
    processHandle = process.hProcess; threadHandle = process.hThread;
    inputWriteHandle = inputWrite; outputReadHandle = outputRead;
    return true;
#else
    juce::ignoreUnused(shell, workingDirectory);
    error = "Persistent terminal sessions are currently implemented for Windows.";
    return false;
#endif
}

bool PersistentShellSession::send(const juce::String& command, juce::String& error)
{
#if JUCE_WINDOWS
    if (!isRunning()) { error = "Terminal session is not running."; return false; }
    const auto utf8 = command.toRawUTF8();
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(inputWriteHandle), utf8, static_cast<DWORD>(std::strlen(utf8)), &written, nullptr)) {
        error = "Could not send input to terminal."; return false;
    }
    return true;
#else
    juce::ignoreUnused(command); error = "Persistent terminal sessions are currently implemented for Windows."; return false;
#endif
}

juce::String PersistentShellSession::readAvailableOutput()
{
#if JUCE_WINDOWS
    if (outputReadHandle == nullptr) return {};
    juce::MemoryOutputStream output;
    DWORD available = 0;
    while (PeekNamedPipe(static_cast<HANDLE>(outputReadHandle), nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        char buffer[4096]; DWORD read = 0;
        const DWORD wanted = juce::jmin<DWORD>(available, sizeof(buffer));
        if (!ReadFile(static_cast<HANDLE>(outputReadHandle), buffer, wanted, &read, nullptr) || read == 0) break;
        output.write(buffer, read);
    }
    return output.toString();
#else
    return {};
#endif
}

void PersistentShellSession::stop()
{
#if JUCE_WINDOWS
    if (inputWriteHandle) { CloseHandle(static_cast<HANDLE>(inputWriteHandle)); inputWriteHandle = nullptr; }
    if (processHandle) { TerminateProcess(static_cast<HANDLE>(processHandle), 0); CloseHandle(static_cast<HANDLE>(processHandle)); processHandle = nullptr; }
    if (threadHandle) { CloseHandle(static_cast<HANDLE>(threadHandle)); threadHandle = nullptr; }
    if (outputReadHandle) { CloseHandle(static_cast<HANDLE>(outputReadHandle)); outputReadHandle = nullptr; }
#endif
}

bool PersistentShellSession::isRunning() const noexcept
{
#if JUCE_WINDOWS
    if (processHandle == nullptr) return false;
    return WaitForSingleObject(static_cast<HANDLE>(processHandle), 0) == WAIT_TIMEOUT;
#else
    return false;
#endif
}
