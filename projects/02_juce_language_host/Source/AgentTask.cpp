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

std::vector<ai_provider::ToolDefinition> AgentTask::controlDefinitions() const
{
    return {
        definition("agent_set_plan",
            "Set the concrete execution plan for the current assigned task after inspecting the project. "
            "This records the plan in host-owned run state; it does not edit project files.",
            R"({"type":"object","additionalProperties":false,"properties":{"goal":{"type":"string","minLength":1},"steps":{"type":"array","minItems":2,"items":{"type":"string","minLength":1}}},"required":["goal","steps"]})"),
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
    if (call.name != "agent_set_plan" && call.name != "agent_complete_task"
        && call.name != "agent_request_user")
        return {};

    const auto arguments = juce::JSON::parse(juce::String(call.argumentsJson));
    if (!arguments.isObject())
        return { true, false, false, "Error: Control-tool arguments were not a JSON object." };

    if (call.name == "agent_set_plan")
    {
        if (!inspected)
            return { true, false, false,
                     "Error: Inspect the current project before setting the implementation plan." };
        auto proposed = stringArrayProperty(arguments, "steps");
        if (proposed.size() < 2)
            return { true, false, false, "Error: A professional task plan needs at least two concrete steps." };
        const auto proposedGoal = property(arguments, "goal").trim();
        if (proposedGoal.isNotEmpty()) goal = proposedGoal;
        plan = std::move(proposed);
        return { true, true, false, "Plan recorded with " + juce::String(plan.size()) + " steps." };
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

void AgentTask::recordEngineerResult(const std::string& name, const EngineerTools::Result& result)
{
    ++toolCalls;
    if (result.ok && name == "workspace_list")
        inspected = true;
    if (result.ok && result.workspaceChanged)
    {
        changed = true;
        verified = false;
    }
    if (result.ok && (result.workspaceChanged || result.verificationPerformed || name == "launch_program" || name == "user_test"))
        acted = true;
    if (result.verificationPerformed)
    {
        verified = result.ok;
        latestVerification = result.message.upToFirstOccurrenceOf("\n", false, false);
    }
    observations.add(juce::String(name) + ": "
        + result.message.upToFirstOccurrenceOf("\n", false, false));
    while (observations.size() > 20) observations.remove(0);
}

void AgentTask::fail(const juce::String& reason)
{
    status = "failed";
    summary = reason;
}

juce::String AgentTask::phase() const
{
    if (status != "running") return status;
    if (!inspected) return "inspect";
    if (requiresPlan && plan.isEmpty()) return "plan";
    if (requiresWrite && !acted) return "implement";
    if (requiresVerification && !verified) return "verify";
    return "finish";
}

juce::String AgentTask::completionBlocker() const
{
    if (!inspected) return "No project inspection has succeeded.";
    if (requiresPlan && plan.isEmpty()) return "No execution plan has been recorded.";
    if (requiresWrite && !acted)
        return "Nothing has been done yet: no change, build, test, command or program launch has succeeded.";
    if (requiresVerification && !verified)
        return "The changed code has not passed verification since the last change (a Frust check, or a build or test "
               "run with run_command).";
    return {};
}

juce::String AgentTask::contextMessage() const
{
    juce::String text = "HOST-OWNED AGENT RUN STATE (authoritative)\n"
        "Task ID: " + taskId + "\nGoal: " + goal + "\nStatus: " + status
        + "\nMode: " + mode + "\nCurrent phase: " + phase() + "\n";
    if (!plan.isEmpty())
    {
        text << "Plan:\n";
        for (int i = 0; i < plan.size(); ++i)
            text << juce::String(i + 1) << ". " << plan[i] << "\n";
    }
    text << "Observed project: " << (inspected ? "yes" : "no")
         << "\nWorkspace changed: " << (changed ? "yes" : "no")
         << "\nAction taken (change, build, test, command or launch): " << (acted ? "yes" : "no")
         << "\nVerification passed after latest change: " << (verified ? "yes" : "no") << "\n"
         << "Required behavior: work on this goal until agent_complete_task is accepted or a real "
            "blocker requires agent_request_user. In inspect phase begin with workspace_list on the open "
            "project root, then read the relevant files. In plan phase "
            "call agent_set_plan. In implement phase make the edits, not a prose code sample. In verify "
            "phase call workspace_check_frust for Frust, or build and test with run_command for any other language, and "
            "repair failures. When the user should try a program, open it with user_test and say what to check: their "
            "Pass is verification, their Fail is a bug report to fix. In review mode inspect and report findings "
            "without editing. Never claim completion in ordinary text.";
    return text;
}

juce::String AgentTask::statusLine() const
{
    auto shortGoal = goal.substring(0, 90);
    if (goal.length() > shortGoal.length()) shortGoal << "...";
    return "Task [" + mode + "]: " + shortGoal + "  |  " + phase();
}

juce::String AgentTask::finalMessage() const
{
    if (status == "completed")
    {
        juce::String text = "**Task completed**\n\n" + summary;
        if (latestVerification.isNotEmpty()) text << "\n\n**Verification:** " << latestVerification;
        return text;
    }
    if (status == "waiting-for-user") return pendingQuestion;
    return "**Task stopped**\n\n" + summary;
}

bool AgentTask::isTerminal() const
{
    return status == "completed" || status == "waiting-for-user" || status == "failed";
}

bool AgentTask::isCompleted() const
{
    return status == "completed";
}

bool AgentTask::canWrite() const
{
    return requiresWrite && status == "running" && inspected && (!requiresPlan || !plan.isEmpty());
}

const juce::StringArray& AgentTask::planSteps() const { return plan; }
const juce::String& AgentTask::taskGoal() const { return goal; }
const juce::String& AgentTask::taskMode() const { return mode; }

juce::var AgentTask::toJson() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("schema", "frust-ide-agent-task");
    object->setProperty("schemaVersion", 1);
    object->setProperty("taskId", taskId);
    object->setProperty("conversationId", conversationId);
    object->setProperty("goal", goal);
    object->setProperty("mode", mode);
    object->setProperty("status", status);
    object->setProperty("summary", summary);
    object->setProperty("pendingQuestion", pendingQuestion);
    object->setProperty("latestVerification", latestVerification);
    object->setProperty("plan", stringArrayJson(plan));
    object->setProperty("observations", stringArrayJson(observations));
    object->setProperty("requiresWrite", requiresWrite);
    object->setProperty("requiresPlan", requiresPlan);
    object->setProperty("requiresVerification", requiresVerification);
    object->setProperty("inspected", inspected);
    object->setProperty("changed", changed);
    object->setProperty("acted", acted);
    object->setProperty("verified", verified);
    object->setProperty("toolCalls", toolCalls);
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
    task.plan = stringArrayProperty(value, "plan");
    task.observations = stringArrayProperty(value, "observations");
    task.requiresWrite = boolProperty(value, "requiresWrite");
    task.requiresPlan = value.hasProperty("requiresPlan")
        ? boolProperty(value, "requiresPlan") : true;
    task.requiresVerification = boolProperty(value, "requiresVerification");
    task.inspected = boolProperty(value, "inspected");
    task.changed = boolProperty(value, "changed");
    task.acted = value.hasProperty("acted") ? boolProperty(value, "acted") : task.changed;
    task.verified = boolProperty(value, "verified");
    task.toolCalls = static_cast<int>(value.getProperty("toolCalls", 0));
    return task.taskId.isNotEmpty() && task.conversationId.isNotEmpty() && task.goal.isNotEmpty();
}
