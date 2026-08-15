#include "DesktopAuthSession.h"

#include <thread>

namespace
{
juce::String sessionFolderName() { return "LagDaemonResearchIDE"; }
juce::String authFileName() { return "desktop-auth.json"; }
juce::String authSiteBase() { return "https://lagdaemon.com"; }
juce::String tokenApiBase() { return "https://lagdaemon.com/djehuti"; }

constexpr std::uint32_t sha256InitialState[] =
{
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};

constexpr std::uint32_t sha256RoundConstants[] =
{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};
}

DesktopAuthSession::DesktopAuthSession(juce::String appSlugToUse)
    : juce::Thread("Desktop Auth Session"), appSlug(std::move(appSlugToUse))
{
    loadFromDisk();
}

DesktopAuthSession::~DesktopAuthSession()
{
    cancelLogin();
    stopThread(3000);
    saveToDisk();
}

bool DesktopAuthSession::loadFromDisk()
{
    auto file = juce::File(getSessionFilePath());
    if (! file.existsAsFile())
        return false;

    auto xml = juce::parseXML(file);
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (! tree.isValid())
        return false;

    loadSessionFromValueTree(tree);
    return hasValidSession();
}

void DesktopAuthSession::saveToDisk() const
{
    juce::ScopedLock lock(sessionLock);

    if (session.token.isEmpty())
        return;

    juce::ValueTree tree("DesktopAuthSession");
    tree.setProperty("token", session.token, nullptr);
    tree.setProperty("expiresAt", session.expiresAt, nullptr);
    tree.setProperty("email", session.user.email, nullptr);
    tree.setProperty("displayName", session.user.displayName, nullptr);
    tree.setProperty("role", session.user.role, nullptr);

    juce::StringArray entitlements;
    for (int index = 0; index < session.user.entitlements.size(); ++index)
        entitlements.add(session.user.entitlements[index]);

    juce::String joinedEntitlements = entitlements.joinIntoString("\n");
    tree.setProperty("entitlements", joinedEntitlements, nullptr);

    auto file = juce::File(getSessionFilePath());
    file.getParentDirectory().createDirectory();
    if (auto xml = tree.createXml())
        xml->writeTo(file);
}

void DesktopAuthSession::clearSession()
{
    {
        juce::ScopedLock lock(sessionLock);
        session = {};
        sessionDirty = false;
    }

    auto file = juce::File(getSessionFilePath());
    if (file.existsAsFile())
        file.deleteFile();

    if (onSessionCleared != nullptr)
        onSessionCleared();
}

bool DesktopAuthSession::hasValidSession() const noexcept
{
    juce::ScopedLock lock(sessionLock);
    return session.token.isNotEmpty() && session.expiresAt > juce::Time::getCurrentTime().toMilliseconds();
}

juce::String DesktopAuthSession::getStatusMessage() const
{
    juce::ScopedLock lock(sessionLock);
    return statusMessage;
}

juce::String DesktopAuthSession::getErrorMessage() const
{
    juce::ScopedLock lock(sessionLock);
    return errorMessage;
}

void DesktopAuthSession::beginLogin()
{
    if (isThreadRunning())
        return;

    {
        juce::ScopedLock lock(sessionLock);
        loginRequested = true;
        loginActive = true;
        pending.state = makeRandomToken(24);
        pending.codeVerifier = makeRandomToken(48);
        pending.codeChallenge = sha256Base64Url(pending.codeVerifier);
        pending.port = 0;
        errorMessage.clear();
        statusMessage = "Opening browser to sign in...";
    }

    setBusy(true);
    startThread();
}

void DesktopAuthSession::cancelLogin()
{
    {
        juce::ScopedLock lock(sessionLock);
        loginRequested = false;
        loginActive = false;
    }

    listener.close();
    signalThreadShouldExit();
}

void DesktopAuthSession::run()
{
    if (! listener.createListener(0, "127.0.0.1"))
    {
        setError("Unable to start the local login listener.");
        setBusy(false);
        return;
    }

    juce::String redirectUri = "http://127.0.0.1:" + juce::String(listener.getBoundPort()) + "/callback";
    juce::URL authUrl = buildAuthorizationUrl(redirectUri);

    if (! authUrl.launchInDefaultBrowser())
    {
        setError("Could not open the browser for sign-in.");
        setBusy(false);
        return;
    }

    setStatus("Waiting for confirmation...");

    while (! threadShouldExit())
    {
        if (auto* client = listener.waitForNextConnection())
        {
            juce::String request = readRequest(*client);
            auto firstLine = request.upToFirstOccurrenceOf("\r\n", false, false);
            auto requestParts = juce::StringArray::fromTokens(firstLine, " ", "");

            auto responseBody = createHtmlResponse("Sign-in failed", "This request could not be processed.");
            auto responseCode = 400;
            auto responseText = "Bad Request";
            juce::String code;
            juce::String returnedState;

            if (requestParts.size() >= 2 && requestParts[0] == "GET")
            {
                auto pathAndQuery = requestParts[1];
                juce::URL callbackUrl("http://127.0.0.1" + pathAndQuery);
                auto path = callbackUrl.getSubPath(false);

                if (path == "callback")
                {
                    returnedState = getParameterValue(callbackUrl, "state");
                    code = getParameterValue(callbackUrl, "code");

                    juce::String pendingState;
                    {
                        juce::ScopedLock lock(sessionLock);
                        pendingState = pending.state;
                    }

                    if (returnedState == pendingState && code.isNotEmpty())
                    {
                        responseBody = createHtmlResponse("Sign-in complete", "You can return to Creation Station now.");
                        responseCode = 200;
                        responseText = "OK";
                    }
                    else
                    {
                        responseBody = createHtmlResponse("Sign-in failed", "This sign-in request could not be verified.");
                    }
                }
                else
                {
                    responseBody = createHtmlResponse("Creation Station", "You can close this tab and return to the app.");
                    responseCode = 200;
                    responseText = "OK";
                }
            }

            writeResponse(*client, responseCode, responseText, responseBody);
            client->close();
            delete client;

            if (responseCode == 200 && code.isNotEmpty())
            {
                setStatus("Finishing sign-in...");
                std::thread([this, code]
                {
                    exchangeCodeForToken(code);
                }).detach();
                break;
            }
            else if (responseCode != 200)
            {
                setError("The sign-in callback was invalid.");
            }
        }
    }

    {
        juce::ScopedLock lock(sessionLock);
        loginActive = false;
    }

    setBusy(false);
}

juce::String DesktopAuthSession::getSessionFilePath()
{
    auto sessionDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                               .getChildFile(sessionFolderName());
    return sessionDirectory.getChildFile(authFileName()).getFullPathName();
}

juce::String DesktopAuthSession::makeRandomToken(int byteCount)
{
    juce::MemoryBlock block;
    block.ensureSize(static_cast<size_t>(byteCount));
    auto* bytes = static_cast<std::uint8_t*>(block.getData());
    auto& random = juce::Random::getSystemRandom();

    for (int index = 0; index < byteCount; ++index)
        bytes[index] = static_cast<std::uint8_t>(random.nextInt(256));

    return toBase64Url(block.getData(), block.getSize());
}

juce::String DesktopAuthSession::toBase64Url(const void* data, size_t size)
{
    auto encoded = juce::Base64::toBase64(data, size);
    encoded = encoded.replace("+", "-").replace("/", "_");
    while (encoded.endsWithChar('='))
        encoded = encoded.dropLastCharacters(1);
    return encoded;
}

juce::String DesktopAuthSession::sha256Base64Url(const juce::String& text)
{
    auto digest = sha256(text.toRawUTF8(), static_cast<size_t>(text.getNumBytesAsUTF8()));
    return toBase64Url(digest.data(), digest.size());
}

juce::String DesktopAuthSession::escapeJson(const juce::String& text)
{
    juce::String escaped;
    escaped.preallocateBytes(text.getNumBytesAsUTF8() + 16);

    for (auto character : text)
    {
        switch (character)
        {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (character < 0x20)
                    escaped << "\\u" << juce::String::toHexString((int) character).paddedLeft('0', 4);
                else
                    escaped << character;
                break;
        }
    }

    return escaped;
}

juce::String DesktopAuthSession::jsonQuote(const juce::String& text)
{
    return "\"" + escapeJson(text) + "\"";
}

bool DesktopAuthSession::decodeBase64Url(const juce::String& encodedText, juce::MemoryBlock& decodedData)
{
    auto base64Text = encodedText.replace("-", "+").replace("_", "/");

    while ((base64Text.length() % 4) != 0)
        base64Text << '=';

    return decodedData.fromBase64Encoding(base64Text);
}

juce::String DesktopAuthSession::readRequest(juce::StreamingSocket& clientSocket)
{
    juce::MemoryBlock requestData;
    char buffer[2048] {};

    for (int i = 0; i < 50; ++i)
    {
        if (clientSocket.waitUntilReady(true, 100) <= 0)
            continue;

        auto bytesRead = clientSocket.read(buffer, sizeof(buffer), false);
        if (bytesRead <= 0)
            break;

        requestData.append(buffer, static_cast<size_t>(bytesRead));

        auto text = juce::String::fromUTF8(static_cast<const char*>(requestData.getData()),
                                          static_cast<int>(requestData.getSize()));
        if (text.contains("\r\n\r\n"))
            return text;
    }

    return juce::String::fromUTF8(static_cast<const char*>(requestData.getData()),
                                  static_cast<int>(requestData.getSize()));
}

juce::String DesktopAuthSession::createHtmlResponse(const juce::String& title, const juce::String& body)
{
    return "<!doctype html><html><head><meta charset=\"utf-8\"><title>" + escapeJson(title) +
           "</title><style>body{font-family:system-ui;background:#11151c;color:#eef;padding:40px;}</style></head><body><h1>" +
           escapeJson(title) + "</h1><p>" + escapeJson(body) + "</p></body></html>";
}

bool DesktopAuthSession::writeResponse(juce::StreamingSocket& clientSocket,
                                       int statusCode,
                                       const juce::String& statusText,
                                       const juce::String& body)
{
    auto response = "HTTP/1.1 " + juce::String(statusCode) + " " + statusText + "\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Content-Length: " + juce::String(body.getNumBytesAsUTF8()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;
    return clientSocket.write(response.toRawUTF8(), static_cast<int>(response.getNumBytesAsUTF8())) > 0;
}

juce::String DesktopAuthSession::getParameterValue(const juce::URL& url, const juce::String& name)
{
    const auto& names = url.getParameterNames();
    const auto& values = url.getParameterValues();

    for (int index = 0; index < names.size(); ++index)
        if (names[index] == name)
            return values[index];

    return {};
}

juce::String DesktopAuthSession::truncateForStatus(const juce::String& text, int maxChars)
{
    if (text.length() <= maxChars)
        return text;

    return text.substring(0, maxChars) + "...";
}

juce::var DesktopAuthSession::decodeJwtPayload(const juce::String& token)
{
    auto firstDot = token.indexOfChar('.');
    auto secondDot = token.indexOfChar(firstDot >= 0 ? firstDot + 1 : 0, '.');

    if (firstDot < 0 || secondDot < 0 || secondDot <= firstDot + 1)
        return {};

    auto payloadText = token.substring(firstDot + 1, secondDot);
    juce::MemoryBlock payloadBytes;
    if (! decodeBase64Url(payloadText, payloadBytes))
        return {};

    auto payloadString = juce::String::fromUTF8(static_cast<const char*>(payloadBytes.getData()),
                                                static_cast<int>(payloadBytes.getSize()));
    return juce::JSON::parse(payloadString);
}

std::int64_t DesktopAuthSession::parseExpiryToMillis(const juce::String& expiryText)
{
    if (expiryText.containsChar('T'))
        return juce::Time::fromISO8601(expiryText).toMilliseconds();

    return expiryText.getLargeIntValue();
}

std::uint32_t DesktopAuthSession::rotateRight(std::uint32_t value, std::uint32_t amount)
{
    return (value >> amount) | (value << (32u - amount));
}

std::array<std::uint8_t, 32> DesktopAuthSession::sha256(const void* data, size_t size)
{
    auto bytes = static_cast<const std::uint8_t*>(data);
    std::array<std::uint32_t, 8> state
    {
        sha256InitialState[0], sha256InitialState[1], sha256InitialState[2], sha256InitialState[3],
        sha256InitialState[4], sha256InitialState[5], sha256InitialState[6], sha256InitialState[7]
    };

    std::array<std::uint8_t, 64> block {};
    size_t blockOffset = 0;
    size_t processed = 0;

    auto processBlock = [&]
    {
        std::array<std::uint32_t, 64> schedule {};

        for (int i = 0; i < 16; ++i)
        {
            auto offset = static_cast<size_t>(i * 4);
            schedule[i] = (static_cast<std::uint32_t>(block[offset + 0]) << 24)
                        | (static_cast<std::uint32_t>(block[offset + 1]) << 16)
                        | (static_cast<std::uint32_t>(block[offset + 2]) << 8)
                        | (static_cast<std::uint32_t>(block[offset + 3]));
        }

        for (int i = 16; i < 64; ++i)
        {
            auto s0 = rotateRight(schedule[i - 15], 7) ^ rotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
            auto s1 = rotateRight(schedule[i - 2], 17) ^ rotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];

        for (int i = 0; i < 64; ++i)
        {
            auto s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            auto ch = (e & f) ^ ((~e) & g);
            auto temp1 = h + s1 + ch + sha256RoundConstants[i] + schedule[i];
            auto s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            auto maj = (a & b) ^ (a & c) ^ (b & c);
            auto temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    };

    while (processed < size)
    {
        block[blockOffset++] = bytes[processed++];

        if (blockOffset == block.size())
        {
            processBlock();
            blockOffset = 0;
            block.fill(0);
        }
    }

    block[blockOffset++] = 0x80;

    if (blockOffset > 56)
    {
        while (blockOffset < 64)
            block[blockOffset++] = 0;

        processBlock();
        blockOffset = 0;
        block.fill(0);
    }

    while (blockOffset < 56)
        block[blockOffset++] = 0;

    auto bitCount = static_cast<std::uint64_t>(size) * 8u;
    for (int shift = 7; shift >= 0; --shift)
        block[blockOffset++] = static_cast<std::uint8_t>((bitCount >> (shift * 8)) & 0xffu);

    processBlock();

    std::array<std::uint8_t, 32> digest {};
    for (int index = 0; index < 8; ++index)
    {
        digest[index * 4 + 0] = static_cast<std::uint8_t>((state[index] >> 24) & 0xffu);
        digest[index * 4 + 1] = static_cast<std::uint8_t>((state[index] >> 16) & 0xffu);
        digest[index * 4 + 2] = static_cast<std::uint8_t>((state[index] >> 8) & 0xffu);
        digest[index * 4 + 3] = static_cast<std::uint8_t>(state[index] & 0xffu);
    }

    return digest;
}

bool DesktopAuthSession::exchangeCodeForToken(const juce::String& code)
{
    setStatus("Exchanging sign-in code...");

    auto bodyObject = juce::DynamicObject::Ptr(new juce::DynamicObject);
    bodyObject->setProperty("code", code);
    bodyObject->setProperty("codeVerifier", pending.codeVerifier);

    auto body = juce::JSON::toString(juce::var(bodyObject.get()));
    auto url = juce::URL(tokenApiBase() + "/api/auth/desktop/token").withPOSTData(body);
    int statusCode = 0;
    juce::StringPairArray headers;
    auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                            .withHttpRequestCmd("POST")
                                            .withConnectionTimeoutMs(15000)
                                            .withResponseHeaders(&headers)
                                            .withStatusCode(&statusCode)
                                            .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n"));

    if (stream == nullptr)
    {
        setError("Could not complete token exchange. HTTP " + juce::String(statusCode));
        return false;
    }

    auto responseText = stream->readEntireStreamAsString();
    if (statusCode < 200 || statusCode >= 300)
    {
        setError("Token exchange failed (HTTP " + juce::String(statusCode) + "): " + truncateForStatus(responseText, 180));
        return false;
    }

    auto parsed = juce::JSON::parse(responseText);
    if (parsed.isVoid())
    {
        setError("The login server returned an invalid response: " + truncateForStatus(responseText, 180));
        return false;
    }

    auto* response = parsed.getDynamicObject();
    juce::String token;
    juce::String expiresText;
    juce::var userValue;

    if (response != nullptr)
    {
        token = response->getProperty("token").toString();
        expiresText = response->getProperty("expiresAt").toString();
        userValue = response->getProperty("user");
    }
    else if (parsed.isString())
    {
        token = parsed.toString();
    }
    else
    {
        setError("The login server response was missing fields: " + truncateForStatus(responseText, 180));
        return false;
    }

    {
        juce::ScopedLock lock(sessionLock);
        session.token = token;

        session.expiresAt = parseExpiryToMillis(expiresText);
        if (session.expiresAt == 0)
            session.expiresAt = juce::Time::getCurrentTime().toMilliseconds() + juce::RelativeTime::days(30).inMilliseconds();

        auto jwtPayload = decodeJwtPayload(token);
        if (auto* jwtObject = jwtPayload.getDynamicObject())
        {
            session.user.role = jwtObject->getProperty("role").toString();
            session.user.entitlements.clear();

            if (auto* entitlementsArray = jwtObject->getProperty("entitlements").getArray())
            {
                for (int index = 0; index < entitlementsArray->size(); ++index)
                    session.user.entitlements.add((*entitlementsArray)[index].toString());
            }
        }

        if (auto* userObject = userValue.getDynamicObject())
        {
            session.user.id = userObject->getProperty("id").toString();
            session.user.email = userObject->getProperty("email").toString();
            session.user.displayName = userObject->getProperty("displayName").toString();
            if (session.user.displayName.isEmpty())
                session.user.displayName = userObject->getProperty("display_name").toString();
        }
        else if (userValue.isString())
        {
            session.user.email = userValue.toString();
        }
    }

    persistSession();
    juce::MessageManager::callAsync([this]
    {
        notifyAuthenticated();
    });
    return true;
}

bool DesktopAuthSession::isRedirectUriAllowed(const juce::String& redirectUri) const
{
    auto url = juce::URL(redirectUri);
    auto domain = url.getDomain().toLowerCase();
    return (domain == "127.0.0.1" || domain == "localhost") && url.getScheme() == "http";
}

juce::URL DesktopAuthSession::buildAuthorizationUrl(const juce::String& redirectUri) const
{
    return juce::URL(authSiteBase() + "/auth/desktop")
        .withParameter("redirect_uri", redirectUri)
        .withParameter("state", pending.state)
        .withParameter("app", appSlug)
        .withParameter("code_challenge", pending.codeChallenge)
        .withParameter("code_challenge_method", "S256");
}

void DesktopAuthSession::setStatus(const juce::String& text)
{
    {
        juce::ScopedLock lock(sessionLock);
        statusMessage = text;
    }

    if (onStatusChanged != nullptr)
        juce::MessageManager::callAsync([callback = onStatusChanged, text]
        {
            callback(text);
        });
}

void DesktopAuthSession::setError(const juce::String& text)
{
    {
        juce::ScopedLock lock(sessionLock);
        errorMessage = text;
        statusMessage = text;
    }

    if (onError != nullptr)
        juce::MessageManager::callAsync([callback = onError, text]
        {
            callback(text);
        });
}

void DesktopAuthSession::setBusy(bool shouldBeBusy)
{
    if (onBusyChanged != nullptr)
        juce::MessageManager::callAsync([callback = onBusyChanged, shouldBeBusy]
        {
            callback(shouldBeBusy);
        });
}

void DesktopAuthSession::notifyAuthenticated()
{
    if (onAuthenticated == nullptr)
        return;

    SessionData copiedSession;
    {
        juce::ScopedLock lock(sessionLock);
        copiedSession = session;
    }

    onAuthenticated(copiedSession);
}

void DesktopAuthSession::persistSession()
{
    saveToDisk();
}

void DesktopAuthSession::loadSessionFromValueTree(const juce::ValueTree& tree)
{
    juce::ScopedLock lock(sessionLock);
    session.token = tree.getProperty("token").toString();
    session.expiresAt = static_cast<std::int64_t>(tree.getProperty("expiresAt", 0));
    session.user.email = tree.getProperty("email").toString();
    session.user.displayName = tree.getProperty("displayName").toString();
    session.user.role = tree.getProperty("role").toString();
    session.user.entitlements.clear();

    auto entitlementsJoined = tree.getProperty("entitlements").toString();
    juce::StringArray entitlements;
    entitlements.addTokens(entitlementsJoined, "\n", "");
    session.user.entitlements = entitlements;

    if (session.token.isNotEmpty()
        && (session.user.role.isEmpty() || session.user.entitlements.isEmpty() || session.user.displayName.isEmpty()))
    {
        auto jwtPayload = decodeJwtPayload(session.token);
        if (auto* jwtObject = jwtPayload.getDynamicObject())
        {
            if (session.user.role.isEmpty())
                session.user.role = jwtObject->getProperty("role").toString();

            if (session.user.displayName.isEmpty())
            {
                session.user.displayName = jwtObject->getProperty("displayName").toString();
                if (session.user.displayName.isEmpty())
                    session.user.displayName = jwtObject->getProperty("display_name").toString();
            }

            if (session.user.email.isEmpty())
                session.user.email = jwtObject->getProperty("email").toString();

            if (session.user.entitlements.isEmpty())
            {
                if (auto* entitlementsArray = jwtObject->getProperty("entitlements").getArray())
                {
                    for (const auto& entitlement : *entitlementsArray)
                        session.user.entitlements.add(entitlement.toString());
                }
            }
        }
    }
}
