#include "EngineerTools.h"

#include <filesystem>

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

EngineerTools::Result failure(const juce::String& message)
{
    return { false, false, "Error: " + message };
}
}

EngineerTools::EngineerTools(juce::File projectRoot, AccessLevel accessLevel)
    : root(std::move(projectRoot)), access(accessLevel)
{
}

juce::String EngineerTools::accessName(AccessLevel level)
{
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
        const auto required = stringProperty(tool, "access");
        if (required == "workspace" && access != AccessLevel::workspace)
            continue;

        ai_provider::ToolDefinition definition;
        definition.name = stringProperty(tool, "name").toStdString();
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

juce::File EngineerTools::resolveProjectPath(const juce::String& suppliedPath,
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

    std::error_code rootError;
    const auto canonicalRootPath = std::filesystem::weakly_canonical(
        std::filesystem::path(root.getFullPathName().toStdString()), rootError);
    const auto canonicalRoot = rootError
        ? root : juce::File(juce::String(canonicalRootPath.c_str()));

    if (candidate != canonicalRoot && !candidate.isAChildOf(canonicalRoot))
    {
        error = "The requested path is outside the open project.";
        return {};
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
    if (name == "workspace_create_directory") return createDirectory(arguments);
    if (name == "workspace_create_file") return createFile(arguments);
    if (name == "workspace_replace_text") return replaceText(arguments);
    return failure("Unknown tool: " + name);
}

EngineerTools::Result EngineerTools::list(const juce::var& arguments) const
{
    juce::String error;
    const auto directory = resolveProjectPath(stringProperty(arguments, "path", "."), error);
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
        const auto relative = entry.getRelativePathFrom(root).replaceCharacter('\\', '/');
        if (isIgnored(relative)) continue;
        output << (entry.isDirectory() ? "directory  " : "file       ") << relative << "\n";
        if (++emitted >= limit) break;
    }
    return { true, false, "Listed " + juce::String(emitted) + " entries.\n" + output.trimEnd() };
}

EngineerTools::Result EngineerTools::read(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), error);
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
    return { true, false, file.getRelativePathFrom(root).replaceCharacter('\\', '/')
        + " (" + juce::String(lines.size()) + " lines)\n" + output.trimEnd() };
}

EngineerTools::Result EngineerTools::search(const juce::var& arguments) const
{
    const auto query = stringProperty(arguments, "query");
    if (query.isEmpty()) return failure("Search query must not be empty.");
    juce::String error;
    const auto directory = resolveProjectPath(stringProperty(arguments, "path", "."), error);
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
        const auto relative = file.getRelativePathFrom(root).replaceCharacter('\\', '/');
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

EngineerTools::Result EngineerTools::createDirectory(const juce::var& arguments) const
{
    juce::String error;
    const auto directory = resolveProjectPath(stringProperty(arguments, "path"), error);
    if (error.isNotEmpty()) return failure(error);
    const auto result = directory.createDirectory();
    if (result.failed()) return failure(result.getErrorMessage());
    return { true, true, "Directory is ready: "
        + directory.getRelativePathFrom(root).replaceCharacter('\\', '/') };
}

EngineerTools::Result EngineerTools::createFile(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), error);
    if (error.isNotEmpty()) return failure(error);
    if (file.exists()) return failure("File already exists; creation will not overwrite it.");
    const auto parentResult = file.getParentDirectory().createDirectory();
    if (parentResult.failed()) return failure(parentResult.getErrorMessage());
    if (!file.replaceWithText(stringProperty(arguments, "content")))
        return failure("Could not write the new file.");
    return { true, true, "Created " + file.getRelativePathFrom(root).replaceCharacter('\\', '/') };
}

EngineerTools::Result EngineerTools::replaceText(const juce::var& arguments) const
{
    juce::String error;
    const auto file = resolveProjectPath(stringProperty(arguments, "path"), error);
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
    return { true, true, "Updated " + file.getRelativePathFrom(root).replaceCharacter('\\', '/') };
}
