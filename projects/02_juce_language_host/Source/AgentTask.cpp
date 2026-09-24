#include "AgentTask.h"

namespace
{
juce::String property(const juce::var& object, const juce::Identifier& name)
{
    return object.getProperty(name, {}).toString();
}

bool boolProperty(const juce::var& object, const juce::Identifier& name)
{
    return static_cast<bool>(object.getProperty(name, false));
}

juce::StringArray stringArrayProperty(const juce::var& object, const juce::Identifier& name)
{
    juce::StringArray result;
    if (auto* values = object.getProperty(name, {}).getArray())
        for (const auto& value : *values) result.add(value.toString());
    return result;
}

juce::var stringArrayJson(const juce::StringArray& values)
{
    juce::Array<juce::var> result;
    for (const auto& value : values) result.add(value);
    return result;
}

ai_provider::ToolDefinition definition(const char* name, const char* description,
                                       const char* parameters)
{
    return { name, description, parameters };
}

bool containsAny(const juce::String& text, std::initializer_list<const char*> needles)
{
    for (const auto* needle : needles)
        if (text.contains(needle)) return true;
    return false;
}

bool isReadOnlyEngineerTool(const std::string& name)
{
    return name == "workspace_list" || name == "workspace_read"
        || name == "workspace_search" || name == "registry_search";
}

bool publicationRequested(const juce::String& text)
{
    if (!containsAny(text, { "publish", "deploy to the registry", "deploy to server" })) return false;
    return !containsAny(text, { "do not publish", "don't publish", "must not publish",
                                "without publishing", "not publish" });
}

bool testEvidenceRequested(const juce::String& text)
{
    if (!text.contains("test")) return false;
    if (containsAny(text, { "frate test", "run tests", "run the tests", "automated test",
                            "automated tests", "unit test", "unit tests", "test suite",
                            "test suites", "tests pass", "testing" }))
        return true;
    if (containsAny(text, { "smoke test", "smoke-test" }))
        return false;
    return text.contains("tests");
}

juce::String withoutComments(const juce::String& source)
{
    juce::String result;
    bool inString = false;
    bool escaped = false;
    bool lineComment = false;
    bool blockComment = false;
    for (int index = 0; index < source.length(); ++index)
    {
        const auto current = source[index];
        const auto next = index + 1 < source.length() ? source[index + 1] : juce::juce_wchar();
        if (lineComment)
        {
            if (current == '\n') { lineComment = false; result += current; }
            continue;
        }
        if (blockComment)
        {
            if (current == '*' && next == '/') { blockComment = false; ++index; }
            continue;
        }
        if (!inString && current == '/' && next == '/') { lineComment = true; ++index; continue; }
        if (!inString && current == '/' && next == '*') { blockComment = true; ++index; continue; }
        result += current;
        if (inString && current == '\\' && !escaped) { escaped = true; continue; }
        if (current == '"' && !escaped) inString = !inString;
        escaped = false;
    }
    return result;
}

bool hasCommentOnlyFunction(const juce::String& source)
{
    const auto code = withoutComments(source);
    int position = 0;
    while ((position = code.indexOf(position, "fn")) >= 0)
    {
        const bool leftBoundary = position == 0
            || !(juce::CharacterFunctions::isLetterOrDigit(code[position - 1]) || code[position - 1] == '_');
        const int afterFn = position + 2;
        const bool rightBoundary = afterFn >= code.length()
            || !(juce::CharacterFunctions::isLetterOrDigit(code[afterFn]) || code[afterFn] == '_');
        if (!leftBoundary || !rightBoundary) { position = afterFn; continue; }

        const int openingBrace = code.indexOfChar(afterFn, '{');
        const int semicolon = code.indexOfChar(afterFn, ';');
        if (openingBrace < 0 || (semicolon >= 0 && semicolon < openingBrace))
        {
            position = semicolon >= 0 ? semicolon + 1 : afterFn;
            continue;
        }
        int depth = 1;
        int closingBrace = openingBrace + 1;
        for (; closingBrace < code.length() && depth > 0; ++closingBrace)
        {
            if (code[closingBrace] == '{') ++depth;
            else if (code[closingBrace] == '}') --depth;
        }
        if (depth == 0 && code.substring(openingBrace + 1, closingBrace - 1).trim().isEmpty())
            return true;
        position = closingBrace;
    }
    return false;
}
}

bool AgentTask::proposedWriteHasUnresolvedImplementation(const ai_provider::ToolCall& call)
{
    if (call.name != "workspace_create_file" && call.name != "workspace_write_file"
        && call.name != "workspace_replace_text")
        return false;
    const auto arguments = juce::JSON::parse(juce::String(call.argumentsJson));
    const auto propertyName = call.name == "workspace_replace_text" ? "new_text" : "content";
    const auto content = arguments.getProperty(propertyName, {}).toString();
    const auto lower = content.toLowerCase();
    if (containsAny(lower, { "implementation will go here", "further implementation will go here",
                             "placeholder implementation", "add functionality here",
                             "implement using core functions when available",
                             "basic implementation to illustrate", "logic to validate",
                             "validation logic here", "test logic here", "setup test fixtures",
                             "placeholder for actual" }))
        return true;
    const auto path = arguments.getProperty("path", {}).toString().toLowerCase();
    return path.endsWith(".fr") && hasCommentOnlyFunction(content);
}

AgentTask AgentTask::begin(const juce::String& conversation,
                           const juce::String& requestedGoal,
                           const juce::String& requestedMode,
                           bool planRequired,
                           bool writeRequired,
                           bool verificationRequired,
                           const juce::StringArray& initialPlan)
{
    AgentTask task;
    task.taskId = juce::Uuid().toString();
    task.conversationId = conversation;
    task.goal = requestedGoal.trim();
    task.mode = requestedMode;
    task.requiresPlan = planRequired;
    task.requiresWrite = writeRequired;
    task.requiresVerification = verificationRequired;
    task.plan = initialPlan;
    task.capabilitiesAssessed = !initialPlan.isEmpty();
    task.requiresBuildEvidence = containsAny(task.goal.toLowerCase(), { "build", "compile", "package" });
    task.requiresTestEvidence = testEvidenceRequested(task.goal.toLowerCase());
    task.requiresPublishEvidence = publicationRequested(task.goal.toLowerCase());
    return task;
}

bool AgentTask::load(const juce::File& folder, const juce::String& conversation,
                     AgentTask& task)
{
    const auto file = folder.getChildFile(".agent-state").getChildFile(conversation + ".json");
    if (!file.existsAsFile()) return false;
    return fromJson(juce::JSON::parse(file.loadFileAsString()), task);
}

bool AgentTask::save(const juce::File& folder) const
{
    auto stateFolder = folder.getChildFile(".agent-state");
    if (!stateFolder.createDirectory()) return false;
    return stateFolder.getChildFile(conversationId + ".json")
        .replaceWithText(juce::JSON::toString(toJson(), true));
}

bool AgentTask::resume()
{
    if (!isResumable()) return false;
    status = "running";
    summary.clear();
    pendingQuestion.clear();
    repeatedFailure.clear();
    repeatedFailureCount = 0;
    return true;
}

bool AgentTask::continuePlanAsExecution(bool verificationRequired)
{
    if (!planApproved || mode != "plan" || plan.isEmpty() || !capabilitiesAssessed) return false;
    taskId = juce::Uuid().toString();
    mode = "execute";
    status = "running";
    summary.clear();
    pendingQuestion.clear();
    requiresWrite = true;
    requiresVerification = verificationRequired;
    const auto lowerGoal = goal.toLowerCase();
    requiresBuildEvidence = containsAny(lowerGoal, { "build", "compile", "package" });
    requiresTestEvidence = testEvidenceRequested(lowerGoal);
    requiresPublishEvidence = publicationRequested(lowerGoal);
    changed = false;
    acted = false;
    verified = false;
    buildVerified = false;
    testsVerified = false;
    published = false;
    repeatedFailure.clear();
    repeatedFailureCount = 0;
    return true;
}

std::vector<ai_provider::ToolDefinition> AgentTask::controlDefinitions() const
{
    return {
        definition("agent_assess_capabilities",
            "Record the prerequisite capabilities and evidence found after inspecting the project. Missing prerequisites "
            "pause the task for a user decision before implementation begins. The requested feature itself is not a prerequisite. "
            "Workspace write access is host-granted in Execute mode; an inspection or planning gate is not missing access. "
            "Implementation tools are temporarily hidden during this phase and become available after the plan; never list a "
            "workspace_* tool, run_command, file creation, or file editing as missing.",
            R"({"type":"object","additionalProperties":false,"properties":{"required":{"type":"array","items":{"type":"string","minLength":1}},"available":{"type":"array","items":{"type":"string","minLength":1}},"missing":{"type":"array","items":{"type":"string","minLength":1}},"evidence":{"type":"array","minItems":1,"items":{"type":"string","minLength":1}},"options":{"type":"array","items":{"type":"string","minLength":1}}},"required":["required","available","missing","evidence","options"]})"),
        definition("agent_set_plan",
            "Set the concrete execution plan for the current assigned task after inspecting the project. "
            "A professional plan explains the purpose, rationale, approach, risks, ordered work, constraints, "
            "and acceptance tests. This records the plan in host-owned run state; it does not edit project files.",
            R"({"type":"object","additionalProperties":false,"properties":{"goal":{"type":"string","minLength":1},"purpose":{"type":"string","minLength":1},"rationale":{"type":"string","minLength":1},"approach":{"type":"string","minLength":1},"steps":{"type":"array","minItems":2,"items":{"type":"string","minLength":1}},"constraints":{"type":"array","minItems":1,"items":{"type":"string","minLength":1}},"risks":{"type":"array","minItems":1,"items":{"type":"string","minLength":1}},"acceptance_tests":{"type":"array","minItems":1,"items":{"type":"string","minLength":1}}},"required":["goal","purpose","rationale","approach","steps","constraints","risks","acceptance_tests"]})"),
        definition("agent_complete_task",
            "Request completion of the assigned task. The host rejects this until required inspection, "
            "changes, and verification have actually succeeded.",
            R"({"type":"object","additionalProperties":false,"properties":{"summary":{"type":"string","minLength":1}},"required":["summary"]})"),
        definition("agent_request_user",
            "Pause only for information or judgment that cannot be obtained from the project or tools. "
            "Do not use this for implementation choices you can make yourself.",
            R"({"type":"object","additionalProperties":false,"properties":{"question":{"type":"string","minLength":1},"reason":{"type":"string","minLength":1}},"required":["question","reason"]})")
    };
}

AgentTask::ControlResult AgentTask::executeControl(const ai_provider::ToolCall& call)
{
    if (call.name != "agent_assess_capabilities" && call.name != "agent_set_plan" && call.name != "agent_complete_task"
        && call.name != "agent_request_user")
        return {};

    const auto arguments = juce::JSON::parse(juce::String(call.argumentsJson));
    if (!arguments.isObject())
        return { true, false, false, "Error: Control-tool arguments were not a JSON object." };

    if (call.name == "agent_assess_capabilities")
    {
        if (!inspected)
            return { true, false, false, "Error: Inspect the current project before assessing capabilities." };
        const auto lowerGoal = goal.toLowerCase();
        const bool namesExternalEvidence = goal.contains(":\\") || goal.contains(":/");
        bool readEvidence = false;
        bool searchEvidence = false;
        bool registryEvidence = false;
        for (const auto& observation : observations)
        {
            readEvidence = readEvidence || observation.startsWith("workspace_read:")
                || observation.startsWith("workspace_search:");
            searchEvidence = searchEvidence || observation.startsWith("workspace_search:");
            registryEvidence = registryEvidence || observation.startsWith("registry_search:");
        }
        if ((namesExternalEvidence && !readEvidence)
            || (lowerGoal.contains("registry") && !registryEvidence))
        {
            inspected = false;
            return { true, false, false,
                "Error: Capability assessment is premature. Inspect the external evidence and every explicitly "
                "requested capability source, including the pod registry, before deciding what is missing." };
        }
        const auto proposedMissing = stringArrayProperty(arguments, "missing");
        auto isHostGrantedCapability = [](const juce::String& missing) {
            const auto lower = missing.toLowerCase();
            return lower == "write" || containsAny(lower,
                { "write access", "write permission", "permission to write", "workspace write",
                  "project file access", "workspace_", "run_command", "create file", "file creation",
                  "edit file", "file editing" });
        };
        bool hasGenuineMissingClaim = false;
        for (const auto& missing : proposedMissing)
            hasGenuineMissingClaim = hasGenuineMissingClaim || !isHostGrantedCapability(missing);
        if (hasGenuineMissingClaim && (!searchEvidence || !registryEvidence))
        {
            inspected = false;
            return { true, false, false,
                "Error: A missing-capability claim needs absence evidence from both a workspace_search of the "
                "local code or documentation and a registry_search. Return to inspection and search for the "
                "claimed capability before asking the user." };
        }
        const auto evidence = stringArrayProperty(arguments, "evidence");
        if (evidence.isEmpty())
            return { true, false, false, "Error: Capability assessment requires concrete project or tool evidence." };
        requiredCapabilities = stringArrayProperty(arguments, "required");
        availableCapabilities = stringArrayProperty(arguments, "available");
        capabilityEvidence = evidence;
        missingCapabilities = proposedMissing;
        if (requiresWrite)
        {
            juce::StringArray genuineMissing;
            for (const auto& missing : missingCapabilities)
            {
                if (isHostGrantedCapability(missing))
                {
                    if (!availableCapabilities.contains(missing))
                        availableCapabilities.add(missing);
                }
                else
                    genuineMissing.add(missing);
            }
            missingCapabilities = std::move(genuineMissing);
        }
        capabilitiesAssessed = true;
        if (!missingCapabilities.isEmpty())
        {
            const auto options = stringArrayProperty(arguments, "options");
            pendingQuestion = "I found missing prerequisite capabilities before implementation: " + missingCapabilities.joinIntoString(", ") + ".";
            if (!options.isEmpty()) pendingQuestion << " Available paths: " << options.joinIntoString("; ") << ".";
            pendingQuestion << " Which path should I take?";
            observations.add("Capability blocker: " + missingCapabilities.joinIntoString(", "));
            status = "waiting-for-user";
            return { true, true, true, "Capability assessment recorded; task paused for a user decision." };
        }
        return { true, true, false, "Capability assessment recorded; prerequisites are available." };
    }

    if (call.name == "agent_set_plan")
    {
        if (!inspected)
            return { true, false, false,
                     "Error: Inspect the current project before setting the implementation plan." };
        if (!capabilitiesAssessed)
            return { true, false, false, "Error: Call agent_assess_capabilities before setting the plan." };
        auto proposed = stringArrayProperty(arguments, "steps");
        auto proposedConstraints = stringArrayProperty(arguments, "constraints");
        auto proposedRisks = stringArrayProperty(arguments, "risks");
        auto proposedTests = stringArrayProperty(arguments, "acceptance_tests");
        auto proposedPurpose = property(arguments, "purpose").trim();
        auto proposedRationale = property(arguments, "rationale").trim();
        auto proposedApproach = property(arguments, "approach").trim();
        if (proposed.size() < 2)
            return { true, false, false, "Error: A professional task plan needs at least two concrete steps." };
        if (proposedPurpose.isEmpty() || proposedRationale.isEmpty() || proposedApproach.isEmpty()
            || proposedConstraints.isEmpty() || proposedRisks.isEmpty() || proposedTests.isEmpty())
            return { true, false, false,
                "Error: The plan must record purpose, rationale, approach, constraints, risks, and executable acceptance tests." };
        const auto proposedGoal = property(arguments, "goal").trim();
        if (proposedGoal.isNotEmpty()) goal = proposedGoal;
        planPurpose = std::move(proposedPurpose);
        planRationale = std::move(proposedRationale);
        planApproach = std::move(proposedApproach);
        plan = std::move(proposed);
        constraints = std::move(proposedConstraints);
        planRisks = std::move(proposedRisks);
        acceptanceTests = std::move(proposedTests);
        approvedPlanMarkdown.clear();
        planApproved = false;
        postPlanReadOnlyActions = 0;
        status = "waiting-for-plan-approval";
        pendingQuestion = "Review the proposed plan, amend it if needed, then approve or deny it in the Plan Review panel.";
        return { true, true, true, "Plan recorded with " + juce::String(plan.size())
            + " steps and is waiting for user approval." };
    }

    if (call.name == "agent_complete_task")
    {
        const auto blocker = completionBlocker();
        if (blocker.isNotEmpty())
            return { true, false, false, "Error: Task cannot be completed yet. " + blocker };
        summary = property(arguments, "summary").trim();
        if (summary.isEmpty())
            return { true, false, false, "Error: Completion summary must not be empty." };
        status = "completed";
        return { true, true, true, "Task completion accepted by the host." };
    }

    if (!inspected)
        return { true, false, false,
                 "Error: Inspect the project before asking the user; the answer may already be in the workspace." };
    pendingQuestion = property(arguments, "question").trim();
    const auto reason = property(arguments, "reason").trim();
    if (pendingQuestion.isEmpty() || reason.isEmpty())
        return { true, false, false, "Error: Both a question and a concrete blocking reason are required." };
    status = "waiting-for-user";
    observations.add("Blocked: " + reason);
    return { true, true, true, "Task paused for user input." };
}

juce::String AgentTask::engineerToolPreflight(const ai_provider::ToolCall& call) const
{
    if (phase() == "implement" && isReadOnlyEngineerTool(call.name)
        && postPlanReadOnlyActions >= 4)
    {
        return "Error: The post-plan investigation allowance is exhausted. Use the evidence already gathered "
               "and take an implementation action now: create or edit a file, run the requested check/build/test, "
               "ask a specific blocking question, or request completion if no implementation is required.";
    }
    return {};
}

void AgentTask::recordEngineerResult(const std::string& name, const EngineerTools::Result& result)
{
    ++toolCalls;
    const auto phaseBeforeResult = phase();
    if (result.ok && name == "workspace_list")
    {
        const bool goalNamesExternalEvidence = goal.contains(":\\") || goal.contains(":/");
        inspected = !goalNamesExternalEvidence;
    }
    if (result.ok && name == "workspace_read" && !inspected
        && (goal.contains(":\\") || goal.contains(":/")))
        inspected = true;
    if (result.ok && name == "registry_search" && !inspected)
    {
        const bool goalNamesExternalEvidence = goal.contains(":\\") || goal.contains(":/");
        bool readEvidence = false;
        for (const auto& observation : observations)
            readEvidence = readEvidence || observation.startsWith("workspace_read:")
                || observation.startsWith("workspace_search:");
        inspected = !goalNamesExternalEvidence || readEvidence;
    }
    const auto commandLine = result.message.upToFirstOccurrenceOf("\n", false, false).toLowerCase();
    const bool releaseCommand = name == "run_command"
        && containsAny(commandLine, { "frate package", "frate install-local", "frate publish",
                                      "frate.exe package", "frate.exe install-local", "frate.exe publish" });
    const bool sourceOrProjectEdit = result.workspaceChanged && name != "run_command";
    if (result.ok && sourceOrProjectEdit && !releaseCommand)
    {
        changed = true;
        verified = false;
        buildVerified = false;
        testsVerified = false;
        published = false;
    }
    if (result.ok && (result.workspaceChanged || result.verificationPerformed || name == "launch_program" || name == "user_test"))
        acted = true;
    if (result.verificationPerformed)
    {
        verified = result.ok;
        latestVerification = result.message.upToFirstOccurrenceOf("\n", false, false);
        if (result.ok)
            verificationFailureCount = 0;
        else
            ++verificationFailureCount;
    }
    if (result.ok && name == "run_command") recordCommandEvidence(result.message);
    if (result.ok && result.verificationPerformed)
    {
        repeatedFailure.clear();
        repeatedFailureCount = 0;
    }
    else if (!result.ok)
    {
        const auto fingerprint = failureFingerprint(result.message);
        if (fingerprint.isNotEmpty())
        {
            if (fingerprint == repeatedFailure) ++repeatedFailureCount;
            else
            {
                repeatedFailure = fingerprint;
                repeatedFailureCount = 1;
            }
            if (repeatedFailureCount >= 3 && status == "running")
            {
                status = "waiting-for-user";
                pendingQuestion = "The same underlying failure has occurred three times despite attempted fixes: "
                    + repeatedFailure + ". This now looks like a toolchain or missing-capability blocker, not another local edit. "
                    "Should I repair the underlying system, change the approach, or stop this task?";
                observations.add("Convergence stop: " + repeatedFailure);
            }
        }
    }
    if (verificationFailureCount >= 3 && status == "running")
    {
        status = "waiting-for-user";
        pendingQuestion = "Verification has failed three times despite attempted repairs. "
            "The task has been stopped to prevent an edit-and-check loop. Latest result: "
            + latestVerification;
        observations.add("Verification loop stopped after "
            + juce::String(verificationFailureCount) + " failed checks.");
    }
    if (result.ok)
    {
        if (phaseBeforeResult == "implement" && isReadOnlyEngineerTool(name)
            && !result.workspaceChanged && !result.verificationPerformed)
            ++postPlanReadOnlyActions;
        else if (result.workspaceChanged || result.verificationPerformed || name == "run_command"
                 || name == "launch_program" || name == "user_test")
            postPlanReadOnlyActions = 0;
    }
    observations.add(juce::String(name) + ": "
        + result.message.upToFirstOccurrenceOf("\n", false, false));
    while (observations.size() > 20) observations.remove(0);
}

void AgentTask::recordProviderUsage(const ai_provider::ChatResponse& response)
{
    ++providerCalls;
    lastInputTokens = response.inputTokens;
    inputTokens += response.inputTokens;
    outputTokens += response.outputTokens;
    totalTokens += response.totalTokens;
}

juce::String AgentTask::budgetBeforeProviderCall(int maxProviderCalls, int maxToolCalls,
                                                 int maxTotalTokens) const
{
    if (const auto hardLimit = budgetExceeded(maxProviderCalls, maxToolCalls, maxTotalTokens);
        hardLimit.isNotEmpty())
        return hardLimit;

    if (maxTotalTokens > 0 && providerCalls > 0)
    {
        const auto estimatedNextRequest = juce::jmax(4096, lastInputTokens) + 4096;
        if (totalTokens + estimatedNextRequest > maxTotalTokens)
            return "Task budget reserve reached before another model request: "
                + juce::String(totalTokens) + " tokens used, approximately "
                + juce::String(estimatedNextRequest) + " needed, limit "
                + juce::String(maxTotalTokens) + ".";
    }
    return {};
}

juce::String AgentTask::budgetExceeded(int maxProviderCalls, int maxToolCalls,
                                       int maxTotalTokens) const
{
    if (maxProviderCalls > 0 && providerCalls >= maxProviderCalls)
        return "Task budget reached: " + juce::String(providerCalls)
            + " model requests (limit " + juce::String(maxProviderCalls) + ").";
    if (maxToolCalls > 0 && toolCalls >= maxToolCalls)
        return "Task budget reached: " + juce::String(toolCalls)
            + " tool calls (limit " + juce::String(maxToolCalls) + ").";
    if (maxTotalTokens > 0 && totalTokens >= maxTotalTokens)
        return "Task budget reached: " + juce::String(totalTokens)
            + " tokens (limit " + juce::String(maxTotalTokens) + ").";
    return {};
}

juce::String AgentTask::failureFingerprint(const juce::String& message)
{
    juce::StringArray lines;
    lines.addLines(message.toLowerCase());
    juce::StringArray diagnostics;
    for (auto line : lines)
    {
        line = line.trim();
        if (!containsAny(line, { "error", "fatal", "failed", "unresolved", "duplicate", "unknown", "not recognized" }))
            continue;
        while (line.contains("  ")) line = line.replace("  ", " ");
        diagnostics.add(line.substring(0, 240));
        if (diagnostics.size() == 3) break;
    }
    if (diagnostics.isEmpty()) diagnostics.add(message.upToFirstOccurrenceOf("\n", false, false).trim().toLowerCase());
    return diagnostics.joinIntoString(" | ");
}

void AgentTask::recordCommandEvidence(const juce::String& message)
{
    const auto command = message.upToFirstOccurrenceOf("\n", false, false).toLowerCase();
    if (containsAny(command, { "frate build", "frate.exe build", "frate package", "cmake --build", "msbuild ", "dotnet build", "cargo build" }))
        buildVerified = true;
    if (containsAny(command, { "frate run", "frate.exe run", "frate test", "frate.exe test", "ctest", "dotnet test", "cargo test", "agenttasktests", "tests\\", "tests/" }))
        testsVerified = true;
    if (containsAny(command, { "frate publish", "frate.exe publish" }))
        published = true;
}

void AgentTask::fail(const juce::String& reason)
{
    status = "failed";
    summary = reason;
}

bool AgentTask::approvePlanMarkdown(const juce::String& markdown)
{
    if (plan.isEmpty() || !capabilitiesAssessed) return false;
    const auto trimmed = markdown.trim();
    if (trimmed.isEmpty()) return false;
    approvedPlanMarkdown = trimmed;
    planApproved = true;
    if (status == "waiting-for-plan-approval")
        status = mode == "plan" ? "completed" : "running";
    pendingQuestion.clear();
    summary = "Plan approved.";
    return true;
}

void AgentTask::denyPlan(const juce::String& reason)
{
    status = "waiting-for-user";
    pendingQuestion = reason.isNotEmpty()
        ? "Plan denied: " + reason
        : "Plan denied. Provide revised direction before implementation continues.";
    planApproved = false;
    approvedPlanMarkdown.clear();
}

juce::String AgentTask::phase() const
{
    if (status != "running") return status;
    if (!inspected) return "inspect";
    if (requiresPlan && !capabilitiesAssessed) return "assess";
    if (requiresPlan && plan.isEmpty()) return "plan";
    if (requiresWrite && !acted) return "implement";
    if (requiresVerification && !verified) return "verify";
    if (requiresBuildEvidence && !buildVerified) return "build";
    if (requiresTestEvidence && !testsVerified) return "test";
    if (requiresPublishEvidence && !published) return "publish";
    return "finish";
}

juce::String AgentTask::completionBlocker() const
{
    if (!inspected) return "No project inspection has succeeded.";
    if (requiresPlan && !capabilitiesAssessed) return "No prerequisite capability assessment has been recorded.";
    if (requiresPlan && plan.isEmpty()) return "No execution plan has been recorded.";
    if (requiresPlan && !planApproved) return "The execution plan has not been approved by the user.";
    if (requiresWrite && !acted)
        return "Nothing has been done yet: no change, build, test, command or program launch has succeeded.";
    if (requiresVerification && !verified)
        return "The changed code has not passed verification since the last change (a Frust check, or a build or test "
               "run with run_command).";
    if (requiresBuildEvidence && !buildVerified) return "The requested build has not succeeded.";
    if (requiresTestEvidence && !testsVerified) return "The requested tests have not succeeded.";
    if (requiresPublishEvidence && !published) return "The requested registry publication has not succeeded.";
    return {};
}

juce::String AgentTask::contextMessage() const
{
    juce::String text = "HOST-OWNED AGENT RUN STATE (authoritative)\n"
        "Task ID: " + taskId + "\nGoal: " + goal + "\nStatus: " + status
        + "\nMode: " + mode + "\nCurrent phase: " + phase() + "\n";
    if (!plan.isEmpty())
    {
        if (planPurpose.isNotEmpty()) text << "Plan purpose: " << planPurpose << "\n";
        if (planRationale.isNotEmpty()) text << "Plan rationale: " << planRationale << "\n";
        if (planApproach.isNotEmpty()) text << "Plan approach: " << planApproach << "\n";
        text << "Plan:\n";
        for (int i = 0; i < plan.size(); ++i)
            text << juce::String(i + 1) << ". " << plan[i] << "\n";
    }
    if (!constraints.isEmpty()) text << "Constraints: " << constraints.joinIntoString("; ") << "\n";
    if (!planRisks.isEmpty()) text << "Risks: " << planRisks.joinIntoString("; ") << "\n";
    if (!acceptanceTests.isEmpty()) text << "Acceptance tests: " << acceptanceTests.joinIntoString("; ") << "\n";
    if (approvedPlanMarkdown.isNotEmpty())
        text << "Approved plan markdown (authoritative; user may have amended it):\n"
             << approvedPlanMarkdown << "\n";
    if (!requiredCapabilities.isEmpty()) text << "Required prerequisites: " << requiredCapabilities.joinIntoString("; ") << "\n";
    if (!availableCapabilities.isEmpty()) text << "Available prerequisites: " << availableCapabilities.joinIntoString("; ") << "\n";
    if (!missingCapabilities.isEmpty()) text << "Previously identified missing prerequisites: " << missingCapabilities.joinIntoString("; ") << "\n";
    if (!capabilityEvidence.isEmpty()) text << "Capability evidence: " << capabilityEvidence.joinIntoString("; ") << "\n";
    text << "Observed project: " << (inspected ? "yes" : "no")
         << "\nCapabilities assessed: " << (capabilitiesAssessed ? "yes" : "no")
         << "\nPlan approved by user: " << (planApproved ? "yes" : "no")
         << "\nWorkspace changed: " << (changed ? "yes" : "no")
         << "\nAction taken (change, build, test, command or launch): " << (acted ? "yes" : "no")
         << "\nVerification passed after latest change: " << (verified ? "yes" : "no") << "\n"
         << "Build evidence: " << (buildVerified ? "passed" : requiresBuildEvidence ? "required" : "not required")
         << "; test evidence: " << (testsVerified ? "passed" : requiresTestEvidence ? "required" : "not required")
         << "; publication evidence: " << (published ? "passed" : requiresPublishEvidence ? "required" : "not required") << "\n"
         << "Required behavior: work on this goal until agent_complete_task is accepted or a real "
            "blocker requires agent_request_user. In inspect phase begin with workspace_list on the open "
            "project root, then read the relevant files. In assess phase call agent_assess_capabilities with concrete "
            "evidence and stop for a decision when a prerequisite is missing. Host implementation tools are deliberately "
            "hidden during assess and plan; they become available in implement, so never report a workspace tool or file "
            "operation as a missing prerequisite. In plan phase call agent_set_plan with "
            "purpose, rationale, approach, constraints, risks, and executable acceptance tests, then stop: the host "
            "will display the plan for user review. In implement phase follow the approved plan markdown exactly, "
            "including any user amendments, and make focused edits with workspace tools; "
            "A host message requiring inspection, capability assessment, or a plan is a sequencing instruction, not "
            "evidence that workspace access is missing. Follow the requested sequence and retry. "
            "PowerShell is for inspection, builds, and tests, never for rewriting source files. Preserve the requested "
            "implementation technology and ask before substituting another. In verify "
            "phase call workspace_check_frust for Frust, or build and test with run_command for any other language, and "
            "repair failures. When the user should try a program, open it with user_test and say what to check: their "
            "Pass is verification, their Fail is a bug report to fix. In review mode inspect and report findings "
            "without editing. Never claim completion in ordinary text.";
    if (phase() == "implement")
        text << "\nImplementation liveness: after the plan, read-only investigation is limited. "
             << "Read-only actions used since the plan or last implementation action: "
             << juce::String(postPlanReadOnlyActions) << "/4. When this reaches 4, the next tool action must "
             << "implement, verify/build/test, ask a specific blocker question, or request completion.";
    return text;
}

juce::String AgentTask::statusLine() const
{
    auto shortGoal = goal.substring(0, 90);
    if (goal.length() > shortGoal.length()) shortGoal << "...";
    return "Task [" + mode + "]: " + shortGoal + "  |  " + phase()
        + "  |  " + juce::String(providerCalls) + " requests, "
        + juce::String(totalTokens) + " tokens";
}

juce::String AgentTask::finalMessage() const
{
    if (status == "completed")
    {
        juce::String text = "**Task completed**\n\n" + summary;
        if (mode == "plan" && approvedPlanMarkdown.isNotEmpty())
            text << "\n\n:::details Approved plan\n" << approvedPlanMarkdown << "\n:::";
        if (latestVerification.isNotEmpty()) text << "\n\n**Verification:** " << latestVerification;
        return text;
    }
    if (status == "waiting-for-user") return pendingQuestion;
    if (status == "waiting-for-plan-approval")
        return "**Plan ready for review**\n\nReview the proposed plan in the Plan Review panel. You can edit the markdown there; the approved text becomes the execution plan.";
    return "**Task stopped**\n\n" + summary;
}

bool AgentTask::isTerminal() const
{
    return status == "completed" || status == "waiting-for-user"
        || status == "waiting-for-plan-approval" || status == "failed";
}

bool AgentTask::isCompleted() const
{
    return status == "completed";
}

bool AgentTask::isResumable() const
{
    return status == "waiting-for-user" || status == "waiting-for-plan-approval"
        || status == "failed";
}

bool AgentTask::canWrite() const
{
    return requiresWrite && status == "running" && inspected && capabilitiesAssessed
        && (!requiresPlan || (!plan.isEmpty() && planApproved));
}

bool AgentTask::isWaitingForPlanApproval() const
{
    return status == "waiting-for-plan-approval";
}

juce::String AgentTask::planMarkdown() const
{
    if (approvedPlanMarkdown.isNotEmpty())
        return approvedPlanMarkdown;

    juce::String markdown = "# Plan\n\n";
    markdown << "## Goal\n\n" << goal << "\n\n";
    if (planPurpose.isNotEmpty())
        markdown << "## Purpose\n\n" << planPurpose << "\n\n";
    if (planRationale.isNotEmpty())
        markdown << "## Rationale\n\n" << planRationale << "\n\n";
    if (planApproach.isNotEmpty())
        markdown << "## Approach\n\n" << planApproach << "\n\n";
    if (!plan.isEmpty())
    {
        markdown << "## Steps\n\n";
        for (int i = 0; i < plan.size(); ++i)
            markdown << juce::String(i + 1) << ". " << plan[i] << "\n";
        markdown << "\n";
    }
    if (!constraints.isEmpty())
    {
        markdown << "## Constraints\n\n";
        for (const auto& item : constraints)
            markdown << "- " << item << "\n";
        markdown << "\n";
    }
    if (!planRisks.isEmpty())
    {
        markdown << "## Risks\n\n";
        for (const auto& item : planRisks)
            markdown << "- " << item << "\n";
        markdown << "\n";
    }
    if (!acceptanceTests.isEmpty())
    {
        markdown << "## Acceptance Tests\n\n";
        for (const auto& item : acceptanceTests)
            markdown << "- " << item << "\n";
        markdown << "\n";
    }
    if (!capabilityEvidence.isEmpty())
    {
        markdown << "## Evidence\n\n";
        for (const auto& item : capabilityEvidence)
            markdown << "- " << item << "\n";
    }
    return markdown.trim();
}

const juce::StringArray& AgentTask::planSteps() const { return plan; }
const juce::String& AgentTask::taskGoal() const { return goal; }
const juce::String& AgentTask::taskMode() const { return mode; }
juce::String AgentTask::currentPhase() const { return phase(); }

juce::var AgentTask::evaluationSnapshot() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("taskId", taskId);
    object->setProperty("conversationId", conversationId);
    object->setProperty("status", status);
    object->setProperty("mode", mode);
    object->setProperty("phase", phase());
    object->setProperty("toolCalls", toolCalls);
    object->setProperty("providerCalls", providerCalls);
    object->setProperty("inputTokens", inputTokens);
    object->setProperty("outputTokens", outputTokens);
    object->setProperty("totalTokens", totalTokens);
    object->setProperty("lastInputTokens", lastInputTokens);
    object->setProperty("inspected", inspected);
    object->setProperty("capabilitiesAssessed", capabilitiesAssessed);
    object->setProperty("changed", changed);
    object->setProperty("verified", verified);
    object->setProperty("buildVerified", buildVerified);
    object->setProperty("testsVerified", testsVerified);
    object->setProperty("published", published);
    object->setProperty("repeatedFailureCount", repeatedFailureCount);
    object->setProperty("verificationFailureCount", verificationFailureCount);
    object->setProperty("postPlanReadOnlyActions", postPlanReadOnlyActions);
    object->setProperty("planStepCount", plan.size());
    object->setProperty("observationCount", observations.size());
    return juce::var(object);
}

juce::var AgentTask::toJson() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("schema", "frust-ide-agent-task");
    object->setProperty("schemaVersion", 2);
    object->setProperty("taskId", taskId);
    object->setProperty("conversationId", conversationId);
    object->setProperty("goal", goal);
    object->setProperty("mode", mode);
    object->setProperty("status", status);
    object->setProperty("summary", summary);
    object->setProperty("pendingQuestion", pendingQuestion);
    object->setProperty("latestVerification", latestVerification);
    object->setProperty("repeatedFailure", repeatedFailure);
    object->setProperty("planPurpose", planPurpose);
    object->setProperty("planRationale", planRationale);
    object->setProperty("planApproach", planApproach);
    object->setProperty("plan", stringArrayJson(plan));
    object->setProperty("constraints", stringArrayJson(constraints));
    object->setProperty("acceptanceTests", stringArrayJson(acceptanceTests));
    object->setProperty("planRisks", stringArrayJson(planRisks));
    object->setProperty("requiredCapabilities", stringArrayJson(requiredCapabilities));
    object->setProperty("availableCapabilities", stringArrayJson(availableCapabilities));
    object->setProperty("missingCapabilities", stringArrayJson(missingCapabilities));
    object->setProperty("capabilityEvidence", stringArrayJson(capabilityEvidence));
    object->setProperty("observations", stringArrayJson(observations));
    object->setProperty("approvedPlanMarkdown", approvedPlanMarkdown);
    object->setProperty("planApproved", planApproved);
    object->setProperty("requiresWrite", requiresWrite);
    object->setProperty("requiresPlan", requiresPlan);
    object->setProperty("requiresVerification", requiresVerification);
    object->setProperty("requiresBuildEvidence", requiresBuildEvidence);
    object->setProperty("requiresTestEvidence", requiresTestEvidence);
    object->setProperty("requiresPublishEvidence", requiresPublishEvidence);
    object->setProperty("inspected", inspected);
    object->setProperty("capabilitiesAssessed", capabilitiesAssessed);
    object->setProperty("changed", changed);
    object->setProperty("acted", acted);
    object->setProperty("verified", verified);
    object->setProperty("buildVerified", buildVerified);
    object->setProperty("testsVerified", testsVerified);
    object->setProperty("published", published);
    object->setProperty("repeatedFailureCount", repeatedFailureCount);
    object->setProperty("verificationFailureCount", verificationFailureCount);
    object->setProperty("postPlanReadOnlyActions", postPlanReadOnlyActions);
    object->setProperty("toolCalls", toolCalls);
    object->setProperty("providerCalls", providerCalls);
    object->setProperty("inputTokens", inputTokens);
    object->setProperty("outputTokens", outputTokens);
    object->setProperty("totalTokens", totalTokens);
    object->setProperty("lastInputTokens", lastInputTokens);
    return juce::var(object);
}

bool AgentTask::fromJson(const juce::var& value, AgentTask& task)
{
    if (!value.isObject() || property(value, "schema") != "frust-ide-agent-task") return false;
    task.taskId = property(value, "taskId");
    task.conversationId = property(value, "conversationId");
    task.goal = property(value, "goal");
    task.mode = property(value, "mode");
    if (task.mode.isEmpty()) task.mode = "execute";
    task.status = property(value, "status");
    task.summary = property(value, "summary");
    task.pendingQuestion = property(value, "pendingQuestion");
    task.latestVerification = property(value, "latestVerification");
    task.repeatedFailure = property(value, "repeatedFailure");
    task.planPurpose = property(value, "planPurpose");
    task.planRationale = property(value, "planRationale");
    task.planApproach = property(value, "planApproach");
    task.plan = stringArrayProperty(value, "plan");
    task.constraints = stringArrayProperty(value, "constraints");
    task.acceptanceTests = stringArrayProperty(value, "acceptanceTests");
    task.planRisks = stringArrayProperty(value, "planRisks");
    task.requiredCapabilities = stringArrayProperty(value, "requiredCapabilities");
    task.availableCapabilities = stringArrayProperty(value, "availableCapabilities");
    task.missingCapabilities = stringArrayProperty(value, "missingCapabilities");
    task.capabilityEvidence = stringArrayProperty(value, "capabilityEvidence");
    task.observations = stringArrayProperty(value, "observations");
    task.approvedPlanMarkdown = property(value, "approvedPlanMarkdown");
    task.planApproved = boolProperty(value, "planApproved");
    task.requiresWrite = boolProperty(value, "requiresWrite");
    task.requiresPlan = value.hasProperty("requiresPlan")
        ? boolProperty(value, "requiresPlan") : true;
    task.requiresVerification = boolProperty(value, "requiresVerification");
    task.requiresBuildEvidence = boolProperty(value, "requiresBuildEvidence");
    task.requiresTestEvidence = boolProperty(value, "requiresTestEvidence");
    task.requiresPublishEvidence = boolProperty(value, "requiresPublishEvidence");
    task.inspected = boolProperty(value, "inspected");
    task.capabilitiesAssessed = value.hasProperty("capabilitiesAssessed")
        ? boolProperty(value, "capabilitiesAssessed") : !task.plan.isEmpty();
    task.changed = boolProperty(value, "changed");
    task.acted = value.hasProperty("acted") ? boolProperty(value, "acted") : task.changed;
    task.verified = boolProperty(value, "verified");
    task.buildVerified = boolProperty(value, "buildVerified");
    task.testsVerified = boolProperty(value, "testsVerified");
    task.published = boolProperty(value, "published");
    task.repeatedFailureCount = static_cast<int>(value.getProperty("repeatedFailureCount", 0));
    task.verificationFailureCount = static_cast<int>(value.getProperty("verificationFailureCount", 0));
    task.postPlanReadOnlyActions = static_cast<int>(value.getProperty("postPlanReadOnlyActions", 0));
    task.toolCalls = static_cast<int>(value.getProperty("toolCalls", 0));
    task.providerCalls = static_cast<int>(value.getProperty("providerCalls", 0));
    task.inputTokens = static_cast<int>(value.getProperty("inputTokens", 0));
    task.outputTokens = static_cast<int>(value.getProperty("outputTokens", 0));
    task.totalTokens = static_cast<int>(value.getProperty("totalTokens", 0));
    task.lastInputTokens = static_cast<int>(value.getProperty("lastInputTokens", 0));
    return task.taskId.isNotEmpty() && task.conversationId.isNotEmpty() && task.goal.isNotEmpty();
}
