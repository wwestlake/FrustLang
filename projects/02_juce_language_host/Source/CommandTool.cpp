#include "CommandTool.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <tlhelp32.h>
#endif

namespace command_tool
{
namespace
{
juce::StringArray words(const juce::String& text)
{
    juce::StringArray result;
    result.addTokens(text, " \t\r\n", "\"'");
    result.removeEmptyStrings();
    for (auto& w : result)
        w = w.unquoted();
    return result;
}

bool hasWord(const juce::StringArray& list, std::initializer_list<const char*> candidates)
{
    for (const auto& w : list)
        for (auto* c : candidates)
            if (w.equalsIgnoreCase(c))
                return true;
    return false;
}

bool anyWordStartsWith(const juce::StringArray& list, std::initializer_list<const char*> candidates)
{
    for (const auto& w : list)
        for (auto* c : candidates)
            if (w.startsWithIgnoreCase(c))
                return true;
    return false;
}

// A parallel-build flag other than the single-core one.
bool hasParallelFlag(const juce::StringArray& list)
{
    for (int i = 0; i < list.size(); ++i)
    {
        const auto lower = list[i].toLowerCase();
        const auto next = i + 1 < list.size() ? list[i + 1] : juce::String();
        // A flag and its count as two words: "-j 1" is single-core, "-j 8" or a bare "-j" is not.
        if (lower == "-j" || lower == "--jobs" || lower == "--parallel")
        {
            if (next != "1")
                return true;
            ++i;
            continue;
        }
        if (lower == "/m" || lower == "-m")
            return true;
        if ((lower.startsWith("/m:") || lower.startsWith("-m:") || lower.startsWith("/maxcpucount") || lower.startsWith("-maxcpucount")
             || lower.startsWith("--parallel=") || (lower.startsWith("-j") && lower.length() > 2))
            && ! (lower.endsWith(":1") || lower.endsWith("=1") || lower == "-j1"))
            return true;
    }
    return false;
}

bool pointsOutside(const juce::String& word)
{
    return juce::File::isAbsolutePath(word.unquoted()) || word.contains("..") || word.startsWith("~") || word.startsWith("\\\\");
}

struct Part
{
    Verdict verdict = Verdict::ask;
    juce::String reason;
    bool build = false, test = false, readOnly = false, alwaysAsk = false, runsProgram = false;
    juce::String text;   // possibly with a flag added
};

// Judges one command of a command line.
Part judge(juce::String text, const juce::StringArray& allowedPrefixes, juce::StringArray& amendments)
{
    Part part;
    text = text.trim();
    part.text = text;
    const auto w = words(text);
    if (w.isEmpty())
    {
        part.verdict = Verdict::allow;
        part.readOnly = true;
        return part;
    }
    const auto lower = text.toLowerCase();
    auto program = w[0].toLowerCase().upToLastOccurrenceOf(".exe", false, false);
    program = program.fromLastOccurrenceOf("\\", false, false).fromLastOccurrenceOf("/", false, false);
    const auto second = w.size() > 1 ? w[1].toLowerCase() : juce::String();

    auto deny = [&part](const juce::String& why) { part.verdict = Verdict::deny; part.reason = why; return part; };

    // ---- Never ----
    if (lower.contains("vcpkg") && hasWord(w, { "install", "x-set-installed", "upgrade" }))
        return deny("vcpkg install is never run from the IDE: on this machine it can start a full LLVM rebuild. Ask the user.");
    if (lower.contains("llvm") && anyWordStartsWith(w, { "cmake", "msbuild", "ninja", "build", "install", "configure", "vcpkg", "--build" }))
        return deny("LLVM is never built, rebuilt or installed. Ask the user if you think it needs to be.");
    for (auto* banned : { "shutdown", "restart-computer", "stop-computer", "format", "format-volume", "diskpart", "bcdedit",
                          "reg", "set-executionpolicy", "cipher", "takeown", "icacls", "clear-disk", "initialize-disk" })
        if (program == banned)
            return deny("'" + program + "' changes the machine, not the project, and is never run from the IDE.");
    if (hasWord(w, { "-encodedcommand", "-enc", "-ec" }))
        return deny("An encoded command cannot be read, so it is never run. Write the command out.");
    if (program == "git" && ((second == "push" && hasWord(w, { "--force", "-f", "--force-with-lease", "--mirror", "--delete" }))
                             || (second == "reset" && hasWord(w, { "--hard" })) || second == "clean"
                             || (second == "branch" && hasWord(w, { "-d", "-D" })) || (second == "checkout" && hasWord(w, { "--", "." }))))
        return deny("That git command discards work and is never run from the IDE. Ask the user.");
    for (auto* remover : { "rm", "del", "erase", "rmdir", "rd", "remove-item", "ri" })
        if (program == remover)
        {
            for (int i = 1; i < w.size(); ++i)
                if (! w[i].startsWith("-") && ! w[i].startsWith("/") && pointsOutside(w[i]))
                    return deny("Deleting outside the open project is never allowed.");
            part.alwaysAsk = true;
            part.reason = "It deletes files.";
        }

    // ---- Builds and tests: the machine's rules ----
    if (program == "cmake" && second == "--build")
    {
        part.build = true;
        if (! hasWord(w, { "--target", "-t" }))
            return deny("Give an explicit --target: on this machine a build always names the target it builds.");
        if (hasParallelFlag(w))
            return deny("Builds on this machine are single-core: remove --parallel / -j / /m.");
    }
    else if (program == "msbuild")
    {
        part.build = true;
        if (hasParallelFlag(w))
            return deny("Builds on this machine are single-core: remove /m (MSBuild uses one process without it).");
        if (! anyWordStartsWith(w, { "/t:", "-t:", "/target:", "-target:" }))
            return deny("Give an explicit /t:<target>: on this machine a build always names the target it builds.");
    }
    else if (program == "dotnet" && second == "run")
    {
        part.runsProgram = true;   // not a build: it starts the program, which may wait for input
    }
    else if (program == "dotnet" && (second == "build" || second == "test" || second == "publish" || second == "pack"))
    {
        part.build = second != "test";
        part.test = second == "test";
        if (hasParallelFlag(w))
            return deny("Builds on this machine are single-core: use -m:1.");
        if (! anyWordStartsWith(w, { "-m:1", "/m:1", "-maxcpucount:1", "/maxcpucount:1" }))
        {
            part.text << " -m:1";
            amendments.add("added -m:1 to '" + w[0] + " " + w[1] + "' (builds on this machine are single-core)");
        }
    }
    else if (program == "cargo" && (second == "build" || second == "test" || second == "check" || second == "run" || second == "clippy"))
    {
        part.build = second != "test" && second != "run";
        part.test = second == "test";
        part.runsProgram = second == "run";
        if (! anyWordStartsWith(w, { "-j", "--jobs" }))
        {
            part.text << " -j 1";
            amendments.add("added -j 1 to 'cargo " + second + "' (builds on this machine are single-core)");
        }
        else if (hasParallelFlag(w))
            return deny("Builds on this machine are single-core: use -j 1.");
    }
    else if (program == "ninja")
    {
        part.build = true;
        if (! anyWordStartsWith(w, { "-j" }))
        {
            part.text << " -j 1";
            amendments.add("added -j 1 to ninja (builds on this machine are single-core)");
        }
        else if (hasParallelFlag(w))
            return deny("Builds on this machine are single-core: use -j 1.");
    }
    else if (program == "ctest" || (program == "python" && lower.contains("pytest")) || program == "pytest")
    {
        part.test = true;
    }
    else if (program == "frate" && (second == "build" || second == "test"))
    {
        part.build = second == "build";
        part.test = second == "test";
    }

    // ---- Always asked: they reach beyond the project or cannot be undone ----
    if ((program == "git" && (second == "push" || second == "commit" || second == "merge" || second == "rebase" || second == "tag"))
        || program == "curl" || program == "wget" || program == "invoke-webrequest" || program == "iwr" || program == "invoke-restmethod"
        || program == "irm" || program == "start-process" || program == "invoke-expression" || program == "iex"
        || program == "npm" || program == "pip" || program == "winget" || program == "choco" || program == "nuget"
        || (program == "dotnet" && (second == "add" || second == "tool" || second == "nuget" || second == "restore")))
    {
        part.alwaysAsk = true;
        part.reason = "It reaches beyond the project (publishes, downloads, installs or starts programs).";
    }
    if (lower.contains("$(") || lower.contains("`"))
    {
        part.alwaysAsk = true;
        part.reason = "It contains a sub-expression, which the rules cannot read.";
    }

    // ---- Only looks ----
    static const char* const readOnlyCommands[] = { "git status", "git diff", "git log", "git show", "git branch", "git rev-parse",
        "git ls-files", "dir", "ls", "get-childitem", "gci", "type", "cat", "get-content", "gc", "where", "where.exe", "get-command",
        "pwd", "get-location", "echo", "write-output", "write-host", "findstr", "select-string", "rg", "cmake --version",
        "dotnet --info", "dotnet --version", "dotnet --list-sdks", "test-path", "resolve-path" };
    if (! part.alwaysAsk && ! part.build && ! part.test)
        for (auto* ro : readOnlyCommands)
        {
            const juce::String prefix(ro);
            if (lower == prefix || lower.startsWith(prefix + " "))
            {
                // `git branch` with an argument makes or moves a branch.
                if (prefix == "git branch" && w.size() > 2)
                    break;
                // Looking at something outside the project (another folder, a credentials file) is asked, not assumed.
                bool outside = false;
                for (int i = 1; i < w.size(); ++i)
                    if (! w[i].startsWith("-") && pointsOutside(w[i]))
                        outside = true;
                if (outside)
                {
                    part.reason = "It reads outside the open project.";
                    part.alwaysAsk = true;   // a saved rule never covers reading outside the project
                    break;
                }
                part.readOnly = true;
                part.verdict = Verdict::allow;
                return part;
            }
        }

    if (part.alwaysAsk)
    {
        part.verdict = Verdict::ask;
        return part;
    }

    const auto prefix = prefixOf(text).toLowerCase();
    for (const auto& allowed : allowedPrefixes)
    {
        const auto a = allowed.toLowerCase().trim();
        if (a.isNotEmpty() && (prefix == a || lower == a || lower.startsWith(a + " ")))
        {
            part.verdict = Verdict::allow;
            return part;
        }
    }

    part.verdict = Verdict::ask;
    part.reason = "No rule allows it yet.";
    return part;
}
}

juce::StringArray splitCommands(const juce::String& command)
{
    juce::StringArray parts;
    juce::String current;
    juce::juce_wchar quote = 0;
    const auto text = command.replace("\r\n", "\n");
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (quote != 0)
        {
            current += c;
            if (c == quote)
                quote = 0;
            continue;
        }
        if (c == '"' || c == '\'')
        {
            quote = c;
            current += c;
            continue;
        }
        const bool pairAnd = c == '&' && i + 1 < text.length() && text[i + 1] == '&';
        const bool pairOr = c == '|' && i + 1 < text.length() && text[i + 1] == '|';
        if (pairAnd || pairOr)
        {
            parts.add(current);
            current.clear();
            ++i;
            continue;
        }
        if (c == ';' || c == '|' || c == '\n')
        {
            parts.add(current);
            current.clear();
            continue;
        }
        current += c;
    }
    parts.add(current);
    for (auto& p : parts)
        p = p.trim();
    parts.removeEmptyStrings();
    return parts;
}

juce::String prefixOf(const juce::String& singleCommand)
{
    const auto w = words(singleCommand.trim());
    if (w.isEmpty())
        return {};
    auto program = w[0].fromLastOccurrenceOf("\\", false, false).fromLastOccurrenceOf("/", false, false);
    if (program.endsWithIgnoreCase(".exe"))
        program = program.dropLastCharacters(4);
    program = program.toLowerCase();
    if (w.size() < 2)
        return program;

    // The next word counts when it is a subcommand ("build", "--build"), not a path, a value or a flag with a value.
    const auto next = w[1];
    auto body = next;
    while (body.startsWith("-"))
        body = body.substring(1);
    const bool subcommand = body.isNotEmpty() && juce::CharacterFunctions::isLetter(body[0]) && ! next.containsAnyOf("\\/:=.")
                            && (next.startsWith("--") || ! next.startsWith("-"));
    return subcommand ? program + " " + next.toLowerCase() : program;
}

Assessment assess(const juce::String& command, const juce::StringArray& allowedPrefixes)
{
    Assessment result;
    const auto parts = splitCommands(command);
    if (parts.isEmpty())
    {
        result.verdict = Verdict::deny;
        result.reason = "The command is empty.";
        return result;
    }

    // A flag the host adds is spliced into the command where that part is, so the separators (&&, ;, |) keep their meaning.
    juce::String amended = command.trim();
    int searchFrom = 0;
    bool everyPartAllowed = true;
    bool everyPartReadOnly = true;
    for (const auto& text : parts)
    {
        auto part = judge(text, allowedPrefixes, result.amendments);
        const int at = amended.indexOf(searchFrom, text);
        if (at >= 0)
        {
            if (part.text != text)
                amended = amended.replaceSection(at, text.length(), part.text);
            searchFrom = at + part.text.length();
        }
        result.build = result.build || part.build;
        result.test = result.test || part.test;
        result.alwaysAsk = result.alwaysAsk || part.alwaysAsk;
        result.runsProgram = result.runsProgram || part.runsProgram;
        everyPartReadOnly = everyPartReadOnly && part.readOnly;
        if (part.verdict == Verdict::deny)
        {
            result.verdict = Verdict::deny;
            result.reason = part.reason + (parts.size() > 1 ? " (in: " + text + ")" : juce::String());
            result.command = command;
            return result;
        }
        if (part.verdict != Verdict::allow)
        {
            everyPartAllowed = false;
            if (result.reason.isEmpty())
                result.reason = part.reason;
        }
    }

    result.readOnly = everyPartReadOnly;
    result.command = amended;
    result.verdict = everyPartAllowed ? Verdict::allow : Verdict::ask;
    if (everyPartAllowed)
        result.reason.clear();
    result.rulePrefix = (parts.size() == 1 && ! result.alwaysAsk) ? prefixOf(parts[0]) : juce::String();
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------------

RuleStore::RuleStore(juce::File projectRoot, juce::File storageFolder)
    : root(std::move(projectRoot)),
      folder(storageFolder != juce::File() ? storageFolder
                                           : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                                 .getChildFile("LagDaemonResearchIDE").getChildFile("command-rules"))
{
}

juce::File RuleStore::file() const
{
    const auto key = root.getFullPathName().toLowerCase();
    return folder.getChildFile(juce::SHA256(key.toRawUTF8(), (size_t) key.getNumBytesAsUTF8()).toHexString().substring(0, 32) + ".json");
}

juce::StringArray RuleStore::allowedPrefixes() const
{
    juce::StringArray result;
    const auto parsed = juce::JSON::parse(file().loadFileAsString());
    if (auto* list = parsed.getProperty("allowedPrefixes", {}).getArray())
        for (const auto& v : *list)
            result.addIfNotAlreadyThere(v.toString().trim());
    result.removeEmptyStrings();
    return result;
}

bool RuleStore::allow(const juce::String& prefix)
{
    if (prefix.trim().isEmpty() || root == juce::File())
        return false;
    auto list = allowedPrefixes();
    list.addIfNotAlreadyThere(prefix.trim().toLowerCase());
    juce::Array<juce::var> values;
    for (const auto& p : list)
        values.add(p);
    auto* object = new juce::DynamicObject();
    object->setProperty("schema", "frustide-command-rules");
    object->setProperty("projectRoot", root.getFullPathName());
    object->setProperty("allowedPrefixes", values);
    if (! folder.createDirectory())
        return false;
    return file().replaceWithText(juce::JSON::toString(juce::var(object), true));
}

// ---------------------------------------------------------------------------------------------------------------------------

#if JUCE_WINDOWS
namespace
{
std::wstring findShell(juce::String& name)
{
    // PowerShell 7 understands && and ||; Windows PowerShell 5.1 is always present.
    for (auto* candidate : { L"C:\\Program Files\\PowerShell\\7\\pwsh.exe", L"C:\\Program Files\\PowerShell\\7-preview\\pwsh.exe" })
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES)
        {
            name = "PowerShell 7";
            return candidate;
        }
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    name = "Windows PowerShell 5.1";
    return std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
}

std::wstring encodedCommand(const juce::String& command)
{
    // The script is passed encoded (UTF-16LE, base64) so no quoting in it can break the command line. It reports the exit
    // code of the last program that failed, or 1 when a PowerShell command failed.
    const juce::String script = "$ProgressPreference = 'SilentlyContinue'\n"
                                "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8\n"
                                "$global:LASTEXITCODE = 0\n"
                                + command + "\n"
                                "$__ok = $?\n"
                                "if ($LASTEXITCODE) { exit $LASTEXITCODE }\n"
                                "if (-not $__ok) { exit 1 }\n"
                                "exit 0\n";
    const auto wide = script.toWideCharPointer();
    std::string bytes;
    for (auto p = wide; *p != 0; ++p)
    {
        const auto ch = (unsigned) *p;
        if (ch > 0xFFFF)   // outside the BMP: write the UTF-16 surrogate pair
        {
            const auto v = ch - 0x10000;
            const auto hi = (wchar_t) (0xD800 + (v >> 10)), lo = (wchar_t) (0xDC00 + (v & 0x3FF));
            bytes.push_back((char) (hi & 0xFF)); bytes.push_back((char) (hi >> 8));
            bytes.push_back((char) (lo & 0xFF)); bytes.push_back((char) (lo >> 8));
        }
        else
        {
            bytes.push_back((char) (ch & 0xFF));
            bytes.push_back((char) (ch >> 8));
        }
    }
    const auto base64 = juce::Base64::toBase64(bytes.data(), bytes.size());
    return base64.toWideCharPointer();
}
}

RunResult run(const juce::String& command, const juce::File& workingDirectory, int timeoutSeconds, const juce::File& logFolder,
              int headBytes, int tailBytes, const std::function<bool()>& shouldStop, const Progress& progress)
{
    RunResult result;
    const auto started = std::chrono::steady_clock::now();
    const auto shell = findShell(result.shell);

    SECURITY_ATTRIBUTES inherit { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (! CreatePipe(&readEnd, &writeEnd, &inherit, 0))
    {
        result.error = "Could not create the output pipe.";
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    std::wstring commandLine = L"\"" + shell + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand "
                               + encodedCommand(command);
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(0);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job != nullptr)
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    PROCESS_INFORMATION process {};
    const auto folder = workingDirectory.getFullPathName();
    const BOOL created = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                        folder.toWideCharPointer(), &startup, &process);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (! created)
    {
        CloseHandle(readEnd);
        if (job != nullptr)
            CloseHandle(job);
        result.error = "Could not start " + result.shell + " (Windows error " + juce::String((int) GetLastError()) + ").";
        return result;
    }
    if (job != nullptr)
        AssignProcessToJobObject(job, process.hProcess);
    ResumeThread(process.hThread);
    result.started = true;

    std::unique_ptr<juce::FileOutputStream> log;
    if (logFolder != juce::File() && logFolder.createDirectory())
    {
        result.logFile = logFolder.getChildFile(juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S-")
                                                + juce::String(juce::Random::getSystemRandom().nextInt(100000)) + ".log");
        log = result.logFile.createOutputStream();
        if (log != nullptr)
            *log << "> " << command << "\n\n";
    }

    std::string head;
    std::deque<char> tail;
    std::string lastLine, partialLine;
    auto lastOutput = std::chrono::steady_clock::now();
    // The full log is capped: a program stuck printing in a loop once wrote half a gigabyte.
    constexpr juce::int64 maxLogBytes = 20 * 1024 * 1024;
    juce::int64 loggedBytes = 0;
    auto keep = [&](const char* data, DWORD count)
    {
        result.totalBytes += count;
        lastOutput = std::chrono::steady_clock::now();
        for (DWORD i = 0; i < count; ++i)
        {
            const char c = data[i];
            if (c == '\n' || c == '\r')
            {
                if (! partialLine.empty())
                    lastLine = partialLine;
                partialLine.clear();
            }
            else if (partialLine.size() < 300)
                partialLine.push_back(c);
        }
        if (log != nullptr && loggedBytes < maxLogBytes)
        {
            const auto room = (size_t) std::min<juce::int64>((juce::int64) count, maxLogBytes - loggedBytes);
            log->write(data, room);
            loggedBytes += (juce::int64) room;
            if (loggedBytes >= maxLogBytes)
                *log << "\n\n[the log stops here at 20 MB; the command went on printing]\n";
        }
        for (DWORD i = 0; i < count; ++i)
        {
            if ((int) head.size() < headBytes)
                head.push_back(data[i]);
            else
            {
                tail.push_back(data[i]);
                if ((int) tail.size() > tailBytes)
                    tail.pop_front();
            }
        }
    };

    const auto deadline = started + std::chrono::seconds(timeoutSeconds);
    auto lastProgress = started;
    char buffer[8192];
    bool finished = false;
    while (true)
    {
        const auto now = std::chrono::steady_clock::now();
        if (progress && now - lastProgress >= std::chrono::seconds(1))
        {
            lastProgress = now;
            const auto& shown = partialLine.empty() ? lastLine : partialLine;
            progress(std::chrono::duration<double>(now - started).count(),
                     juce::String::fromUTF8(shown.data(), (int) shown.size()).trim(),
                     std::chrono::duration<double>(now - lastOutput).count());
        }
        if (! finished && shouldStop && shouldStop())
        {
            result.stopped = true;
            if (job != nullptr)
                TerminateJobObject(job, 1);
            else
                TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            finished = true;
        }
        DWORD available = 0;
        while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            DWORD got = 0;
            if (! ReadFile(readEnd, buffer, (DWORD) std::min<size_t>(sizeof(buffer), available), &got, nullptr) || got == 0)
                break;
            keep(buffer, got);
        }
        if (finished)
            break;
        if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0)
        {
            finished = true;   // one more pass to drain what is left in the pipe
            continue;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            result.timedOut = true;
            if (job != nullptr)
                TerminateJobObject(job, 1);
            else
                TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            finished = true;
        }
    }

    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    result.exitCode = (int) code;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readEnd);
    if (job != nullptr)
        CloseHandle(job);   // kills anything the command left running
    log.reset();

    result.truncated = ! tail.empty() && result.totalBytes > (juce::int64) (head.size() + tail.size());
    juce::String text = juce::String::fromUTF8(head.data(), (int) head.size());
    if (! tail.empty())
    {
        const std::string tailText(tail.begin(), tail.end());
        if (result.truncated)
            text << "\n\n... [" << juce::String(result.totalBytes - (juce::int64) head.size() - (juce::int64) tail.size())
                 << " bytes left out; the full output is in the log] ...\n\n";
        text << juce::String::fromUTF8(tailText.data(), (int) tailText.size());
    }
    result.output = text;
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

namespace
{
// The programs launch() started. Each has a job, so its whole tree can be ended; the jobs end with the IDE (kill on close).
struct Launched
{
    int processId = 0;
    HANDLE job = nullptr;
    HANDLE process = nullptr;
    juce::String command;
};

std::mutex& launchedLock()
{
    static std::mutex m;
    return m;
}

std::vector<Launched>& launched()
{
    static std::vector<Launched> list;
    return list;
}

bool stillRunning(HANDLE process)
{
    DWORD code = 0;
    return process != nullptr && GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
}
}

int launch(const juce::String& command, const juce::File& workingDirectory, juce::String& error)
{
    juce::String shellName;
    const auto shell = findShell(shellName);
    // -NoExit keeps the window, so the user can read what the program printed last.
    std::wstring commandLine = L"\"" + shell + L"\" -NoLogo -NoProfile -NoExit -ExecutionPolicy Bypass -EncodedCommand "
                               + encodedCommand("$Host.UI.RawUI.WindowTitle = 'FrustIDE: ' + " + juce::String("'")
                                                + command.replace("'", "''") + "'\n" + command);
    std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
    mutableLine.push_back(0);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job != nullptr)
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    const auto folder = workingDirectory.getFullPathName();
    if (! CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, FALSE,
                         CREATE_NEW_CONSOLE | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
                         folder.toWideCharPointer(), &startup, &process))
    {
        if (job != nullptr)
            CloseHandle(job);
        error = "Could not open a window for it (Windows error " + juce::String((int) GetLastError()) + ").";
        return 0;
    }
    if (job != nullptr)
        AssignProcessToJobObject(job, process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    std::lock_guard<std::mutex> lock(launchedLock());
    launched().push_back({ (int) process.dwProcessId, job, process.hProcess, command });
    return (int) process.dwProcessId;
}

bool stopLaunched(int processId)
{
    std::lock_guard<std::mutex> lock(launchedLock());
    auto& list = launched();
    for (auto it = list.begin(); it != list.end(); ++it)
        if (it->processId == processId)
        {
            const bool wasRunning = stillRunning(it->process);
            if (it->job != nullptr)
            {
                TerminateJobObject(it->job, 1);
                CloseHandle(it->job);
            }
            else if (it->process != nullptr)
                TerminateProcess(it->process, 1);
            if (it->process != nullptr)
                CloseHandle(it->process);
            list.erase(it);
            return wasRunning;
        }
    return false;
}

juce::StringArray listLaunched()
{
    std::lock_guard<std::mutex> lock(launchedLock());
    juce::StringArray lines;
    for (const auto& l : launched())
        if (stillRunning(l.process))
            lines.add(juce::String(l.processId) + ": " + l.command);
    return lines;
}

bool otherBuildRunning(juce::String& which)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry { sizeof(entry) };
    bool found = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && ! found; more = Process32NextW(snapshot, &entry))
    {
        const juce::String name(entry.szExeFile);
        for (auto* builder : { "msbuild.exe", "cl.exe", "link.exe", "ninja.exe", "lld-link.exe", "clang-cl.exe" })
            if (name.equalsIgnoreCase(builder))
            {
                which = name;
                found = true;
                break;
            }
    }
    CloseHandle(snapshot);
    return found;
}
#else
RunResult run(const juce::String&, const juce::File&, int, const juce::File&, int, int, const std::function<bool()>&, const Progress&)
{
    RunResult result;
    result.error = "Commands are only supported on Windows.";
    return result;
}

int launch(const juce::String&, const juce::File&, juce::String& error) { error = "Only supported on Windows."; return 0; }
bool stopLaunched(int) { return false; }
juce::StringArray listLaunched() { return {}; }

bool otherBuildRunning(juce::String&) { return false; }
#endif
}
