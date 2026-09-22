#include "LocalAgentApi.h"

#include <map>
#include <mutex>

namespace
{
constexpr int maxRequestBytes = 1024 * 1024;

juce::var errorBody(const juce::String& message)
{
    auto* body = new juce::DynamicObject();
    body->setProperty("error", message);
    return juce::var(body);
}

juce::String headerValue(const juce::StringArray& lines, const juce::String& name)
{
    for (int index = 1; index < lines.size(); ++index)
    {
        const auto line = lines[index];
        const auto colon = line.indexOfChar(':');
        if (colon > 0 && line.substring(0, colon).trim().equalsIgnoreCase(name))
            return line.substring(colon + 1).trim();
    }
    return {};
}
}

struct LocalAgentApi::State
{
    struct Request
    {
        juce::String status { "queued" };
        juce::String response;
        juce::String error;
        juce::var details;
        double startedAtMs = 0.0;
        double durationMs = 0.0;
    };

    std::mutex mutex;
    std::map<juce::String, Request> requests;
    bool active = true;
};

struct LocalAgentApi::HttpRequest
{
    juce::String method;
    juce::String path;
    juce::String authorization;
    juce::String body;
};

LocalAgentApi::LocalAgentApi(juce::File discoveryFileOverride)
    : juce::Thread("FrustIDE Local Agent API"), state(std::make_shared<State>()),
      discoveryOverride(std::move(discoveryFileOverride))
{
}

LocalAgentApi::~LocalAgentApi()
{
    stop();
}

juce::File LocalAgentApi::getDiscoveryFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LagDaemonResearchIDE")
        .getChildFile("agent-api.json");
}

juce::File LocalAgentApi::activeDiscoveryFile() const
{
    return discoveryOverride == juce::File() ? getDiscoveryFile() : discoveryOverride;
}

bool LocalAgentApi::start()
{
    if (isThreadRunning()) return true;
    if (!listener.createListener(0, "127.0.0.1")) return false;

    token = juce::Uuid().toString();
    auto* discovery = new juce::DynamicObject();
    discovery->setProperty("schema", "frustide-agent-api");
    discovery->setProperty("version", 2);
    discovery->setProperty("baseUrl", "http://127.0.0.1:" + juce::String(listener.getBoundPort()));
    discovery->setProperty("token", token);
    discovery->setProperty("transport", "http");

    const auto file = activeDiscoveryFile();
    if (!file.getParentDirectory().createDirectory().wasOk()
        || !file.replaceWithText(juce::JSON::toString(juce::var(discovery), true)))
    {
        listener.close();
        return false;
    }

    startThread();
    return true;
}

void LocalAgentApi::stop()
{
    {
        std::lock_guard lock(state->mutex);
        state->active = false;
    }
    signalThreadShouldExit();
    listener.close();
    stopThread(3000);
    const auto discoveryFile = activeDiscoveryFile();
    const auto discovery = juce::JSON::parse(discoveryFile.loadFileAsString());
    if (discovery.getProperty("token", {}).toString() == token)
        discoveryFile.deleteFile();
}

void LocalAgentApi::run()
{
    while (!threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> socket(listener.waitForNextConnection());
        if (socket != nullptr && !threadShouldExit())
            handleConnection(*socket);
    }
}

void LocalAgentApi::handleConnection(juce::StreamingSocket& socket)
{
    HttpRequest request;
    if (!readRequest(socket, request))
    {
        writeJson(socket, 400, "Bad Request", errorBody("Invalid HTTP request."));
        return;
    }

    if (request.authorization != "Bearer " + token)
    {
        writeJson(socket, 401, "Unauthorized", errorBody("A valid bearer token is required."));
        return;
    }

    if (request.method == "GET" && request.path == "/v1/status")
    {
        auto* body = new juce::DynamicObject();
        body->setProperty("status", "ready");
        body->setProperty("service", "FrustIDE Agent API");
        writeJson(socket, 200, "OK", juce::var(body));
        return;
    }

    if (request.method == "POST" && (request.path == "/v1/messages" || request.path == "/v1/session"))
    {
        const auto parsed = juce::JSON::parse(request.body);
        const bool messageRequest = request.path == "/v1/messages";
        const auto content = parsed.getProperty("content", {}).toString().trim();
        if (!parsed.isObject() || (messageRequest && content.isEmpty()))
        {
            writeJson(socket, 400, "Bad Request", errorBody(messageRequest
                ? "JSON content must be a non-empty string."
                : "Session configuration must be a JSON object."));
            return;
        }

        const auto requestId = juce::Uuid().toString();
        {
            std::lock_guard lock(state->mutex);
            state->requests[requestId] = {};
        }

        auto sharedState = state;
        auto messageHandler = onMessage;
        auto sessionHandler = onSession;
        juce::MessageManager::callAsync([sharedState, messageHandler, sessionHandler,
                                         requestId, content, parsed, messageRequest] {
            {
                std::lock_guard lock(sharedState->mutex);
                auto found = sharedState->requests.find(requestId);
                if (!sharedState->active || found == sharedState->requests.end()) return;
                found->second.status = "running";
                found->second.startedAtMs = juce::Time::getMillisecondCounterHiRes();
            }

            auto completion = [sharedState, requestId](bool ok, const juce::String& result,
                                                        const juce::var& details) {
                std::lock_guard lock(sharedState->mutex);
                auto found = sharedState->requests.find(requestId);
                if (found == sharedState->requests.end()) return;
                found->second.status = ok ? "completed" : "failed";
                found->second.durationMs = juce::Time::getMillisecondCounterHiRes() - found->second.startedAtMs;
                found->second.details = details;
                if (ok) found->second.response = result;
                else found->second.error = result;
            };
            if (messageRequest)
            {
                if (messageHandler) messageHandler(content, std::move(completion));
                else completion(false, "The IDE assistant is unavailable.", {});
            }
            else
            {
                if (sessionHandler) sessionHandler(parsed, std::move(completion));
                else completion(false, "The IDE session controller is unavailable.", {});
            }
        });

        auto* body = new juce::DynamicObject();
        body->setProperty("requestId", requestId);
        body->setProperty("status", "queued");
        writeJson(socket, 202, "Accepted", juce::var(body));
        return;
    }

    if (request.method == "POST" && request.path == "/v1/cancel")
    {
        auto cancelHandler = onCancel;
        if (!cancelHandler)
        {
            writeJson(socket, 503, "Service Unavailable",
                      errorBody("The IDE cancellation controller is unavailable."));
            return;
        }
        juce::MessageManager::callAsync([cancelHandler] { cancelHandler(); });
        auto* body = new juce::DynamicObject();
        body->setProperty("status", "stopping");
        writeJson(socket, 202, "Accepted", juce::var(body));
        return;
    }

    const juce::String requestPrefix = "/v1/requests/";
    if (request.method == "GET" && request.path.startsWith(requestPrefix))
    {
        const auto requestId = request.path.substring(requestPrefix.length()).trim();
        State::Request result;
        {
            std::lock_guard lock(state->mutex);
            const auto found = state->requests.find(requestId);
            if (found == state->requests.end())
            {
                writeJson(socket, 404, "Not Found", errorBody("Unknown request id."));
                return;
            }
            result = found->second;
        }

        auto* body = new juce::DynamicObject();
        body->setProperty("requestId", requestId);
        body->setProperty("status", result.status);
        body->setProperty("durationMs", result.durationMs);
        if (result.response.isNotEmpty()) body->setProperty("response", result.response);
        if (result.error.isNotEmpty()) body->setProperty("error", result.error);
        if (!result.details.isVoid()) body->setProperty("details", result.details);
        writeJson(socket, 200, "OK", juce::var(body));
        return;
    }

    writeJson(socket, 404, "Not Found", errorBody("Unknown endpoint."));
}

bool LocalAgentApi::readRequest(juce::StreamingSocket& socket, HttpRequest& request)
{
    juce::MemoryBlock bytes;
    char buffer[4096] {};
    int headerEnd = -1;
    int expectedSize = -1;

    for (int attempt = 0; attempt < 100 && static_cast<int>(bytes.getSize()) <= maxRequestBytes; ++attempt)
    {
        if (socket.waitUntilReady(true, 100) <= 0) continue;
        const auto count = socket.read(buffer, sizeof(buffer), false);
        if (count <= 0) break;
        bytes.append(buffer, static_cast<size_t>(count));

        const auto text = juce::String::fromUTF8(static_cast<const char*>(bytes.getData()),
                                                 static_cast<int>(bytes.getSize()));
        if (headerEnd < 0)
        {
            headerEnd = text.indexOf("\r\n\r\n");
            if (headerEnd >= 0)
            {
                const auto headers = juce::StringArray::fromLines(text.substring(0, headerEnd));
                expectedSize = headerEnd + 4 + headerValue(headers, "Content-Length").getIntValue();
            }
        }
        if (headerEnd >= 0 && static_cast<int>(bytes.getSize()) >= expectedSize) break;
    }

    if (headerEnd < 0 || expectedSize < 0 || expectedSize > maxRequestBytes
        || static_cast<int>(bytes.getSize()) < expectedSize)
        return false;

    const auto text = juce::String::fromUTF8(static_cast<const char*>(bytes.getData()), expectedSize);
    const auto headers = juce::StringArray::fromLines(text.substring(0, headerEnd));
    if (headers.isEmpty()) return false;
    const auto requestLine = juce::StringArray::fromTokens(headers[0], " ", "");
    if (requestLine.size() < 2) return false;
    request.method = requestLine[0].toUpperCase();
    request.path = requestLine[1].upToFirstOccurrenceOf("?", false, false);
    request.authorization = headerValue(headers, "Authorization");
    request.body = text.substring(headerEnd + 4);
    return true;
}

bool LocalAgentApi::writeJson(juce::StreamingSocket& socket, int statusCode,
                              const juce::String& statusText, const juce::var& body)
{
    const auto json = juce::JSON::toString(body, false);
    const auto response = "HTTP/1.1 " + juce::String(statusCode) + " " + statusText + "\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + juce::String(json.getNumBytesAsUTF8()) + "\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n\r\n" + json;
    const auto utf8 = response.toUTF8();
    const auto total = static_cast<int>(utf8.sizeInBytes() - 1);
    int written = 0;
    while (written < total)
    {
        const auto count = socket.write(utf8.getAddress() + written, total - written);
        if (count <= 0) return false;
        written += count;
    }
    return true;
}
