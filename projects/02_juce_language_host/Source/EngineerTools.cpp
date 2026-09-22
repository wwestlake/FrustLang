#include "EngineerTools.h"

#include <CompilerApi.h>
#include <frate/FrateCache.h>
#include <frate/FrateConfig.h>

#include <filesystem>
#include <mutex>

namespace
{
constexpr int maxTextFileBytes = 2 * 1024 * 1024;

juce::File catalogFile()
{
    return juce::File(FRUST_REPO_ROOT_DIR)
        .getChildFile("projects")
        .getChildFile("frust-ide-agent")
        .getChildFile("ENGINEER_TOOLS.json");
}

juce::String stringProperty(const juce::var& object, const juce::Identifier& name,
                            const juce::String& fallback = {})
{
    const auto value = object.getProperty(name, {});
    return value.isVoid() ? fallback : value.toString();
}

int intProperty(const juce::var& object, const juce::Identifier& name, int fallback)
{
    const auto value = object.getProperty(name, {});
    return value.isVoid() ? fallback : static_cast<int>(value);
}

bool boolProperty(const juce::var& object, const juce::Identifier& name, bool fallback)
{
    const auto value = object.getProperty(name, {});
    return value.isVoid() ? fallback : static_cast<bool>(value);
}

bool isIgnored(const juce::String& relativePath)
{
    auto normal = relativePath.replaceCharacter('\\', '/');
    return normal == ".git" || normal.startsWith(".git/")
        || normal.contains("/node_modules/") || normal.startsWith("node_modules/")
        || normal.contains("/__pycache__/") || normal.startsWith("__pycache__/");
}

juce::String contentHash(const juce::String& text)
{
    return juce::SHA256(text.toRawUTF8(), static_cast<size_t>(text.getNumBytesAsUTF8()))
        .toHexString();
}

EngineerTools::Result failure(const juce::String& message)
{
    return { false, false, "Error: " + message };
}
}

EngineerTools::EngineerTools(juce::File projectRoot, AccessLevel accessLevel, bool writesAllowed)
    : root(std::move(projectRoot)), access(accessLevel), allowWrites(writesAllowed)
{
    if (root.isDirectory()) workspaceRoots.push_back({ root, false });
}

void EngineerTools::setReferenceRoots(std::vector<juce::File> roots)
{
    for (auto& folder : roots)
    {
        if (!folder.isDirectory()) continue;
        bool alreadyOpen = false;
        for (const auto& opened : workspaceRoots)
            alreadyOpen = alreadyOpen || opened.folder == folder;
        if (!alreadyOpen) workspaceRoots.push_back({ std::move(folder), true });
    }
}

juce::String EngineerTools::accessName(AccessLevel level)
{
    if (level == AccessLevel::full) return "Full Access";
    return level == AccessLevel::workspace ? "Workspace" : "Observe";
}

juce::var EngineerTools::loadCatalog() const
{
    if (!catalogFile().existsAsFile())
        return {};
    return juce::JSON::parse(catalogFile().loadFileAsString());
}

std::vector<ai_provider::ToolDefinition> EngineerTools::definitions() const
{
    std::vector<ai_provider::ToolDefinition> result;
    const auto catalog = loadCatalog();
    auto* tools = catalog.getProperty("tools", {}).getArray();
    if (tools == nullptr)
        return result;

    for (const auto& tool : *tools)
    {
        const auto name = stringProperty(tool, "name");
        if (!root.isDirectory() && name != "registry_search")
            continue;
        const auto required = stringProperty(tool, "access");
        if (required == "workspace" && (access == AccessLevel::observe || !allowWrites))
            continue;

        ai_provider::ToolDefinition definition;
        definition.name = name.toStdString();
        definition.description = (stringProperty(tool, "description") + " "
            + stringProperty(tool, "usage")).trim().toStdString();
        definition.parametersJson = juce::JSON::toString(
            tool.getProperty("parameters", {}), false).toStdString();
        if (!definition.name.empty())
            result.push_back(std::move(definition));
    }
    return result;
}

bool EngineerTools::toolIsAvailable(const juce::String& name) const
{
    for (const auto& definition : definitions())
        if (definition.name == name.toStdString())
            return true;
    return false;
}

const EngineerTools::WorkspaceRoot* EngineerTools::containingRoot(const juce::File& candidate) const
{
    for (const auto& opened : workspaceRoots)
        if (candidate == opened.folder || candidate.isAChildOf(opened.folder))
            return &opened;
    return nullptr;
}

juce::String EngineerTools::displayPath(const juce::File& file) const
{
    if (const auto* opened = containingRoot(file))
    {
        const auto relative = file.getRelativePathFrom(opened->folder).replaceCharacter('\\', '/');
        return "[" + opened->folder.getFileName() + "]/" + (relative == "." ? juce::String() : relative);
    }
    return file.getFullPathName();
}

juce::File EngineerTools::resolveProjectPath(const juce::String& suppliedPath,
                                             PathPurpose purpose,
                                             juce::String& error) const
{
    if (!root.isDirectory())
    {
        error = "No project folder is open in FrustIDE.";
        return {};
    }
    if (suppliedPath.trim().isEmpty())
    {
        error = "Path must not be empty.";
        return {};
    }

    auto candidate = juce::File::isAbsolutePath(suppliedPath)
        ? juce::File(suppliedPath) : root.getChildFile(suppliedPath);
    std::error_code ec;
    const auto canonicalPath = std::filesystem::weakly_canonical(
        std::filesystem::path(candidate.getFullPathName().toStdString()), ec);
    if (!ec)
        candidate = juce::File(juce::String(canonicalPath.c_str()));

    if (const auto* opened = containingRoot(candidate))
    {
        if (purpose == PathPurpose::write && opened->readOnly)
        {
            error = "The requested path is in a read-only reference folder: " + opened->folder.getFullPathName();
            return {};
        }
        return candidate;
    }

    if (purpose == PathPurpose::write)
    {
        if (!allowWrites)
        {
            error = "The current mode is read-only.";
            return {};
        }
        if (access == AccessLevel::full) return candidate;
        error = "The requested path is outside the writable project.";
        return {};
    }
    if (access == AccessLevel::full) return candidate;
    if (!juce::File::isAbsolutePath(suppliedPath))
    {
        error = "The requested path is outside the open workspace.";
        return {};
    }
    if (!commands.approveExternalRead)
    {
        error = "Reading outside the open workspace needs the user's approval.";
        return {};
    }

    const auto decision = commands.approveExternalRead(candidate);
    if (decision == ExternalReadDecision::deny)
    {
        error = "The user did not allow access to the external path.";
        return {};
    }
    if (decision == ExternalReadDecision::openReadOnly)
    {
        const auto folder = candidate.isDirectory() ? candidate : candidate.getParentDirectory();
        workspaceRoots.push_back({ folder, true });
    }
    return candidate;
}

EngineerTools::Result EngineerTools::execute(const ai_provider::ToolCall& call) const
{
    const auto name = juce::String(call.name);
    if (!toolIsAvailable(name))
        return failure("Tool is unavailable at the selected access level: " + name);

    const auto arguments = juce::JSON::parse(juce::String(call.argumentsJson));
    if (!arguments.isObject())
        return failure("Tool arguments were not a JSON object.");

    if (name == "workspace_list") return list(arguments);
    if (name == "workspace_read") return read(arguments);
    if (name == "workspace_search") return search(arguments);
    if (name == "registry_search") return searchRegistry(arguments);
    if (name == "workspace_create_directory") return createDirectory(arguments);
    if (name == "workspace_create_file") return createFile(arguments);
    if (name == "workspace_write_file") return writeFile(arguments);
    if (name == "workspace_replace_text") return replaceText(arguments);
    if (name == "workspace_check_frust") return checkFrust(arguments);
    if (name == "run_command") return runCommand(arguments);
    if (name == "launch_program") return launchProgram(arguments);
    if (name == "stop_program") return stopProgram(arguments);
    if (name == "user_test") return userTest(arguments);
    return failure("Unknown tool: " + name);
}

EngineerTools::Result EngineerTools::list(const juce::var& arguments) const
{
    juce::String error;
    auto suppliedPath = stringProperty(arguments, "path", ".").trim();
    if (suppliedPath.isEmpty()) suppliedPath = ".";
    const auto directory = resolveProjectPath(suppliedPath, PathPurpose::read, error);
    if (error.isNotEmpty()) return failure(error);
    if (!directory.isDirectory()) return failure("Directory does not exist.");

    const auto recursive = boolProperty(arguments, "recursive", false);
    const auto limit = juce::jlimit(1, 1000, intProperty(arguments, "limit", 200));
    juce::Array<juce::File> entries;
    directory.findChildFiles(entries, juce::File::findFilesAndDirectories, recursive, "*");

    juce::String output;
    int emitted = 0;
    for (const auto& entry : entries)
    {
        const auto relative = displayPath(entry);
        if (isIgnored(relative)) continue;
        output << (entry.isDirectory() ? "directory  " : "file       ") << relative << "\n";
        if (++emitted >= limit) break;
    }
    return { true, false, "Listed " + juce::String(emitted) + " entries.\n" + output.trimEnd() };
}

EngineerTools::Result EngineerTools::read(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), PathPurpose::read, error);
    if (error.isNotEmpty()) return failure(error);
    if (!file.existsAsFile()) return failure("File does not exist.");
    if (file.getSize() > maxTextFileBytes) return failure("File exceeds the 2 MB read limit.");

    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    const auto start = juce::jmax(1, intProperty(arguments, "start_line", 1));
    const auto end = juce::jmin(lines.size(), intProperty(arguments, "end_line", start + 399));
    if (start > end && !lines.isEmpty()) return failure("end_line is before start_line.");

    juce::String output;
    for (int index = start; index <= end; ++index)
        output << juce::String(index).paddedLeft(' ', 6) << "  " << lines[index - 1] << "\n";
    return { true, false, displayPath(file)
        + " (" + juce::String(lines.size()) + " lines, SHA-256: " + contentHash(file.loadFileAsString())
        + ")\n" + output.trimEnd() };
}

EngineerTools::Result EngineerTools::search(const juce::var& arguments) const
{
    const auto query = stringProperty(arguments, "query");
    if (query.isEmpty()) return failure("Search query must not be empty.");
    juce::String error;
    const auto directory = resolveProjectPath(stringProperty(arguments, "path", "."), PathPurpose::read, error);
    if (error.isNotEmpty()) return failure(error);
    if (!directory.isDirectory()) return failure("Search directory does not exist.");

    const auto pattern = stringProperty(arguments, "file_pattern", "*");
    const auto limit = juce::jlimit(1, 200, intProperty(arguments, "limit", 50));
    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, true, pattern);
    juce::String output;
    int matches = 0;
    for (const auto& file : files)
    {
        const auto relative = displayPath(file);
        if (isIgnored(relative) || file.getSize() > maxTextFileBytes) continue;
        juce::StringArray lines;
        lines.addLines(file.loadFileAsString());
        for (int index = 0; index < lines.size(); ++index)
        {
            if (!lines[index].containsIgnoreCase(query)) continue;
            output << relative << ":" << juce::String(index + 1) << ": "
                   << lines[index].trim().substring(0, 500) << "\n";
            if (++matches >= limit)
                return { true, false, "Found " + juce::String(matches)
                    + " matches (limit reached).\n" + output.trimEnd() };
        }
    }
    return { true, false, "Found " + juce::String(matches) + " matches.\n" + output.trimEnd() };
}

EngineerTools::Result EngineerTools::searchRegistry(const juce::var& arguments) const
{
    const auto query = stringProperty(arguments, "query").trim();
    const auto limit = juce::jlimit(1, 100, intProperty(arguments, "limit", 25));
    auto url = juce::URL("https://lagdaemon.com/djehuti/api/frate/pods");
    if (query.isNotEmpty())
        url = url.withParameter("q", query);

    int statusCode = 0;
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(15000)
            .withStatusCode(&statusCode));
    if (stream == nullptr)
        return failure("Could not reach the public Frate pod registry.");

    const auto response = stream->readEntireStreamAsString();
    if (statusCode != 200)
        return failure("Frate registry request failed with HTTP " + juce::String(statusCode) + ".");

    const auto parsed = juce::JSON::parse(response);
    auto* pods = parsed.getArray();
    if (pods == nullptr)
        return failure("The Frate registry returned an invalid response.");

    juce::Array<juce::var> selected;
    for (const auto& pod : *pods)
    {
        if (!pod.isObject()) continue;
        selected.add(pod);
        if (selected.size() >= limit) break;
    }

    auto* result = new juce::DynamicObject();
    result->setProperty("registry", "https://lagdaemon.com/djehuti/api/frate");
    result->setProperty("query", query);
    result->setProperty("matchCount", pods->size());
    result->setProperty("returnedCount", selected.size());
    result->setProperty("pods", selected);
    return { true, false, juce::JSON::toString(juce::var(result), true) };
}

EngineerTools::Result EngineerTools::createDirectory(const juce::var& arguments) const
{
    juce::String error;
    const auto directory = resolveProjectPath(stringProperty(arguments, "path"), PathPurpose::write, error);
    if (error.isNotEmpty()) return failure(error);
    const auto result = directory.createDirectory();
    if (result.failed()) return failure(result.getErrorMessage());
    return { true, true, "Directory is ready: "
        + displayPath(directory) };
}

EngineerTools::Result EngineerTools::createFile(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), PathPurpose::write, error);
    if (error.isNotEmpty()) return failure(error);
    if (file.exists()) return failure("File already exists; creation will not overwrite it.");
    const auto parentResult = file.getParentDirectory().createDirectory();
    if (parentResult.failed()) return failure(parentResult.getErrorMessage());
    if (!file.replaceWithText(stringProperty(arguments, "content")))
        return failure("Could not write the new file.");
    return { true, true, "Created " + displayPath(file) };
}

EngineerTools::Result EngineerTools::writeFile(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), PathPurpose::write, error);
    if (error.isNotEmpty()) return failure(error);
    if (!file.existsAsFile()) return failure("File does not exist; use workspace_create_file.");
    if (file.getSize() > maxTextFileBytes) return failure("File exceeds the 2 MB write limit.");

    const auto original = file.loadFileAsString();
    const auto expectedHash = stringProperty(arguments, "expected_sha256").trim().toLowerCase();
    const auto actualHash = contentHash(original);
    if (expectedHash != actualHash)
        return failure("File changed since it was read. Read it again and use its current SHA-256 value.");

    if (!file.replaceWithText(stringProperty(arguments, "content")))
        return failure("Could not write the file.");
    return { true, true, "Rewrote " + displayPath(file) };
}

EngineerTools::Result EngineerTools::replaceText(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), PathPurpose::write, error);
    if (error.isNotEmpty()) return failure(error);
    if (!file.existsAsFile()) return failure("File does not exist.");
    if (file.getSize() > maxTextFileBytes) return failure("File exceeds the 2 MB edit limit.");

    const auto oldText = stringProperty(arguments, "old_text");
    const auto newText = stringProperty(arguments, "new_text");
    if (oldText.isEmpty()) return failure("old_text must not be empty.");
    const auto original = file.loadFileAsString();
    const auto first = original.indexOf(oldText);
    if (first < 0) return failure("old_text was not found; read the current file before retrying.");
    const auto replaceAll = boolProperty(arguments, "replace_all", false);
    if (!replaceAll && original.indexOf(first + oldText.length(), oldText) >= 0)
        return failure("old_text occurs more than once; provide more surrounding context.");
    const auto updated = replaceAll
        ? original.replace(oldText, newText)
        : original.replaceSection(first, oldText.length(), newText);
    if (!file.replaceWithText(updated)) return failure("Could not write the edited file.");
    return { true, true, "Updated " + displayPath(file) };
}

EngineerTools::Result EngineerTools::checkFrust(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), PathPurpose::read, error);
    if (error.isNotEmpty()) return failure(error);
    if (!file.existsAsFile()) return failure("File does not exist.");
    if (!file.hasFileExtension("fr;frust"))
        return failure("'" + file.getFileName() + "' is not a Frust file, so the Frust check cannot say anything about it. "
                       "Build it with its own compiler through run_command (for example dotnet build, or cmake --build ... --target ...).");
    if (file.getSize() > maxTextFileBytes) return failure("File exceeds the 2 MB check limit.");

    frust::CompileRequest request;
    request.sources.push_back({ file.getFullPathName().toStdString(),
                                file.loadFileAsString().toStdString() });
    request.emitObject = false;
    const auto parent = file.getParentDirectory();
    request.siblingFiles = [parent](const std::string& requested, std::string& text) {
        const auto sibling = parent.getChildFile(juce::String(requested));
        if (!sibling.existsAsFile()) return false;
        text = sibling.loadFileAsString().toStdString();
        return true;
    };
    request.pods = [projectRoot = root](const std::string& name, const std::string& requested,
                                        frust::PodSource& pod) {
        frate::FrateConfig projectConfig;
        if (!projectConfig.load(projectRoot.getChildFile("frate.json"))) return false;

        std::string version = requested;
        if (version.empty() || version == "current")
        {
            version.clear();
            for (const auto& dependency : projectConfig.getDependencies())
                if (dependency.name == name) { version = dependency.version; break; }
        }
        if (version.empty()) return false;

        frate::FrateCache cache;
        cache.installBundledPodIfAvailable(name, version);
        auto podDirectory = cache.getCachedPodDir(name, version);

#if defined(FRUST_REPO_ROOT_DIR)
        if (!cache.isCached(name, version))
        {
            const auto sourcePod = juce::File(FRUST_REPO_ROOT_DIR)
                .getChildFile("projects/06_frust_library")
                .getChildFile(juce::String(name));
            frate::FrateConfig sourceConfig;
            if (sourceConfig.load(sourcePod.getChildFile("frate.json"))
                && sourceConfig.getMetadata().name == name
                && sourceConfig.getMetadata().version == version)
                podDirectory = sourcePod;
            else
                return false;
        }
#else
        if (!cache.isCached(name, version)) return false;
#endif

        frate::FrateConfig podConfig;
        if (!podConfig.load(podDirectory.getChildFile("frate.json"))) return false;
        pod.ns = podConfig.getMetadata().namespacePath;

        const auto sourceDirectory = podDirectory.getChildFile("src");
        juce::Array<juce::File> files;
        sourceDirectory.findChildFiles(files, juce::File::findFiles, true, "*.fr;*.frust");
        for (const auto& source : files)
        {
            frust::SourceFile podFile;
            podFile.name = source.getRelativePathFrom(sourceDirectory)
                .replaceCharacter('\\', '/').toStdString();
            podFile.text = source.loadFileAsString().toStdString();
            pod.sources.push_back(std::move(podFile));
        }
        return !pod.sources.empty();
    };

    const auto result = frust::Compile(request);
    juce::String output = result.ok ? "Frust check passed" : "Frust check failed";
    output << ": " << file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    for (const auto& diagnostic : result.diagnostics)
        output << "\n" << juce::String(frust::FormatDiagnostic(diagnostic));
    return { result.ok, false, output, true };
}

EngineerTools::Result EngineerTools::runCommand(const juce::var& arguments) const
{
    const auto command = stringProperty(arguments, "command").trim();
    if (command.isEmpty())
        return failure("Give the command to run.");
    const auto reason = stringProperty(arguments, "reason").trim();

    juce::String error;
    const auto folder = resolveProjectPath(stringProperty(arguments, "cwd", "."), PathPurpose::write, error);
    if (error.isNotEmpty())
        return failure(error);
    if (!folder.isDirectory())
        return failure("The folder to run in does not exist.");
    const auto relative = folder == root ? juce::String(".")
                                         : folder.getRelativePathFrom(root).replaceCharacter('\\', '/');

    command_tool::RuleStore rules(root, commands.rulesFolder);
    const auto verdict = command_tool::assess(command, rules.allowedPrefixes());
    if (verdict.verdict == command_tool::Verdict::deny)
        return failure("The host refused this command: " + verdict.reason);

    if (verdict.verdict == command_tool::Verdict::ask && access != AccessLevel::full)
    {
        if (!commands.approve)
            return failure("This command needs the user's approval and there is no one to ask here (" + verdict.reason + ").");
        command_tool::ApprovalRequest request { verdict.command, relative, reason, verdict.reason,
                                                verdict.alwaysAsk ? juce::String() : verdict.rulePrefix };
        if (commands.progress)
            commands.progress("Waiting for your approval to run: " + verdict.command);
        const auto decision = commands.approve(request);
        if (commands.shouldStop && commands.shouldStop())
            return failure("Stopped by the user before the command ran.");
        if (decision == command_tool::Approval::deny)
            return failure("The user did not allow this command. Do not try to get around that: if you need it, say why and "
                           "ask with agent_request_user.");
        if (decision == command_tool::Approval::always && request.rulePrefix.isNotEmpty())
            rules.allow(request.rulePrefix);
    }

    // One build at a time on this machine: not while another build runs anywhere, and never two from this IDE.
    static std::mutex buildLock;
    std::unique_lock<std::mutex> lock(buildLock, std::defer_lock);
    if (verdict.build || verdict.test)
    {
        juce::String which;
        if (command_tool::otherBuildRunning(which))
            return failure("Another build is running on this machine (" + which + "). Only one build runs at a time: "
                           "try again when it has finished, or ask the user.");
        lock.lock();
    }

    // A program the project runs gets a short limit: here it has no keyboard and no screen, so one that waits for input never ends.
    const int defaultTimeout = verdict.runsProgram ? 60 : (verdict.build || verdict.test) ? 900 : 120;
    const int timeout = juce::jlimit(5, 1800, intProperty(arguments, "timeout_seconds", defaultTimeout));
    const auto shown = verdict.command.length() > 70 ? verdict.command.substring(0, 67) + "..." : verdict.command;
    const auto progress = [this, shown](double seconds, const juce::String& lastLine, double quietFor) {
        if (!commands.progress)
            return;
        const int s = (int) seconds;
        juce::String line = "Running " + shown + " (" + juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2) + ")";
        if (quietFor >= 20.0)
            line << ", no output for " << (int) quietFor << " s";
        if (lastLine.isNotEmpty())
            line << "\n" << (lastLine.length() > 160 ? lastLine.substring(0, 157) + "..." : lastLine);
        commands.progress(line);
    };
    if (commands.progress)
        commands.progress("Running " + shown);
    const auto ran = command_tool::run(verdict.command, folder, timeout, commands.logFolder, 12000, 12000,
                                       commands.shouldStop, progress);
    if (!ran.started)
        return failure(ran.error);

    juce::String text;
    text << "Ran in " << ran.shell << " in " << relative << ": " << verdict.command << "\n";
    for (const auto& amendment : verdict.amendments)
        text << "Note: the host " << amendment << ".\n";
    if (ran.stopped)
        text << "The user stopped it after " << juce::String(ran.seconds, 1) << " s (the whole process tree was ended).\n";
    else if (ran.timedOut)
    {
        text << "It was stopped at the time limit of " << timeout << " seconds (the whole process tree was ended).\n";
        if (verdict.runsProgram)
            text << "The program was still running. This shell has no keyboard and no screen, so a program that waits for "
                    "input (a REPL, a menu, a game, a server) never finishes here. Check it by building it and running its "
                    "tests, or give it its input on the command line, for example: \"help`nquit\" | dotnet run\n";
    }
    else
        text << "Exit code " << ran.exitCode << " after " << juce::String(ran.seconds, 1) << " s.\n";
    if (ran.truncated && ran.logFile != juce::File())
        text << "The output was long; the start and the end are below, the whole of it is in " << ran.logFile.getFullPathName() << "\n";
    text << "\n" << (ran.output.trim().isEmpty() ? juce::String("(no output)") : ran.output);

    const bool ok = ran.exitCode == 0 && !ran.timedOut && !ran.stopped;
    // A build or a test run is verification evidence. Anything else that is not read-only may have changed the project.
    return { ok, !verdict.readOnly && !verdict.build && !verdict.test, text, verdict.build || verdict.test };
}

EngineerTools::Result EngineerTools::launchProgram(const juce::var& arguments) const
{
    const auto command = stringProperty(arguments, "command").trim();
    if (command.isEmpty())
        return failure("Give the command that starts the program.");
    const auto reason = stringProperty(arguments, "reason").trim();

    juce::String error;
    const auto folder = resolveProjectPath(stringProperty(arguments, "cwd", "."), PathPurpose::write, error);
    if (error.isNotEmpty())
        return failure(error);
    if (!folder.isDirectory())
        return failure("The folder to start it in does not exist.");
    const auto relative = folder == root ? juce::String(".")
                                         : folder.getRelativePathFrom(root).replaceCharacter('\\', '/');

    // The same rules as run_command decide what may never run. Opening a window on the user's screen is always asked, unless the
    // user chose "Always allow" for launching this kind of program here.
    command_tool::RuleStore rules(root, commands.rulesFolder);
    const auto verdict = command_tool::assess(command, {});
    if (verdict.verdict == command_tool::Verdict::deny)
        return failure("The host refused this command: " + verdict.reason);
    const auto launchRule = verdict.alwaysAsk || verdict.rulePrefix.isEmpty() ? juce::String() : "launch " + verdict.rulePrefix;
    if (access != AccessLevel::full
        && (launchRule.isEmpty() || !rules.allowedPrefixes().contains(launchRule.toLowerCase())))
    {
        if (!commands.approve)
            return failure("Opening a program window needs the user's approval and there is no one to ask here.");
        command_tool::ApprovalRequest request { command, relative, reason,
                                                "It opens a program in its own window on your screen.", launchRule };
        if (commands.progress)
            commands.progress("Waiting for your approval to open: " + command);
        const auto decision = commands.approve(request);
        if (commands.shouldStop && commands.shouldStop())
            return failure("Stopped by the user before the program was opened.");
        if (decision == command_tool::Approval::deny)
            return failure("The user did not allow opening this program. Ask with agent_request_user if you need it.");
        if (decision == command_tool::Approval::always && launchRule.isNotEmpty())
            rules.allow(launchRule);
    }

    const int processId = command_tool::launch(command, folder, error);
    if (processId == 0)
        return failure(error);
    juce::String text;
    text << "Opened in its own window on the user's screen (process " << processId << "), in " << relative << ": " << command << "\n"
         << "You cannot see or type into that window. To learn what it shows, ask the user (agent_request_user); to check its "
            "behaviour yourself, build it and run its tests with run_command. It stays open until the user closes it or you call "
            "stop_program; it is closed when the IDE closes.";
    return { true, false, text, false };
}

EngineerTools::Result EngineerTools::stopProgram(const juce::var& arguments) const
{
    const int processId = intProperty(arguments, "process_id", 0);
    if (processId <= 0)
    {
        const auto running = command_tool::listLaunched();
        return { true, false, running.isEmpty() ? juce::String("No program opened by the Engineer is running.")
                                                : "Programs the Engineer opened that are running:\n" + running.joinIntoString("\n"),
                 false };
    }
    if (!command_tool::stopLaunched(processId))
        return failure("Process " + juce::String(processId) + " is not a program the Engineer opened, or it has already ended.");
    return { true, false, "Closed the program (process " + juce::String(processId) + ") and everything it started.", false };
}

EngineerTools::Result EngineerTools::userTest(const juce::var& arguments) const
{
    const auto instructions = stringProperty(arguments, "instructions").trim();
    if (instructions.isEmpty())
        return failure("Say what the user should try and what they should see (instructions).");
    if (!commands.askTest)
        return failure("There is no one to ask for a test verdict here.");

    // Open it the same way launch_program does (the same rules, and the user is asked first).
    const auto opened = launchProgram(arguments);
    if (!opened.ok)
        return opened;

    if (commands.progress)
        commands.progress("Waiting for you to test it: the card below has Pass and Fail");
    const auto verdict = commands.askTest({ stringProperty(arguments, "command").trim(), instructions });
    const auto said = verdict.comment.isNotEmpty() ? "\nWhat the user said: " + verdict.comment : juce::String("\n(No comment.)");

    switch (verdict.outcome)
    {
        case command_tool::TestVerdict::Outcome::pass:
            return { true, false, "The user tried it and said PASS." + said, true };
        case command_tool::TestVerdict::Outcome::fail:
            return { false, false, "The user tried it and said FAIL." + said
                                       + "\nFix what they describe, build it, and ask them to test again with user_test.", true };
        case command_tool::TestVerdict::Outcome::noAnswer:
        default:
            return failure("The user did not give a verdict (the run was stopped).");
    }
}
