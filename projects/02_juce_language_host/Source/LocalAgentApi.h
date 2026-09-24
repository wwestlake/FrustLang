#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

class LocalAgentApi : private juce::Thread
{
public:
    using Completion = std::function<void(bool, const juce::String&, const juce::var&)>;
    using MessageHandler = std::function<void(const juce::String&, Completion)>;
    using SessionHandler = std::function<void(const juce::var&, Completion)>;
    using PlanSnapshotHandler = std::function<void(Completion)>;
    using PlanDecisionHandler = std::function<void(const juce::var&, Completion)>;
    using CancelHandler = std::function<void()>;

    explicit LocalAgentApi(juce::File discoveryFileOverride = {});
    ~LocalAgentApi() override;

    bool start();
    void stop();

    MessageHandler onMessage;
    SessionHandler onSession;
    PlanSnapshotHandler onPlanSnapshot;
    PlanDecisionHandler onPlanApprove;
    PlanDecisionHandler onPlanDeny;
    CancelHandler onCancel;

    static juce::File getDiscoveryFile();

private:
    struct State;
    struct HttpRequest;

    void run() override;
    void handleConnection(juce::StreamingSocket& socket);
    static bool readRequest(juce::StreamingSocket& socket, HttpRequest& request);
    static bool writeJson(juce::StreamingSocket& socket, int statusCode,
                          const juce::String& statusText, const juce::var& body);

    juce::StreamingSocket listener;
    juce::String token;
    std::shared_ptr<State> state;
    juce::File discoveryOverride;

    juce::File activeDiscoveryFile() const;
};
