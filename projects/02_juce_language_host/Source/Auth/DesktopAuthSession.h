#pragma once

#include <JuceHeader.h>

class DesktopAuthSession final : private juce::Thread
{
public:
    struct UserProfile
    {
        juce::String id;
        juce::String email;
        juce::String displayName;
        juce::String role;
        juce::StringArray entitlements;
    };

    struct SessionData
    {
        juce::String token;
        UserProfile user;
        std::int64_t expiresAt = 0;
    };

    explicit DesktopAuthSession(juce::String appSlug);
    ~DesktopAuthSession() override;

    bool loadFromDisk();
    void saveToDisk() const;
    void clearSession();

    bool hasValidSession() const noexcept;
    const SessionData& getSession() const noexcept { return session; }
    juce::String getStatusMessage() const;
    juce::String getErrorMessage() const;

    void beginLogin();
    void cancelLogin();

    std::function<void(const SessionData&)> onAuthenticated;
    std::function<void(const juce::String&)> onStatusChanged;
    std::function<void(const juce::String&)> onError;
    std::function<void(bool)> onBusyChanged;
    std::function<void()> onSessionCleared;

private:
    void run() override;

    struct PendingLogin
    {
        juce::String state;
        juce::String codeVerifier;
        juce::String codeChallenge;
        int port = 0;
    };

    static juce::String getSessionFilePath();
    static juce::String makeRandomToken(int byteCount);
    static juce::String toBase64Url(const void* data, size_t size);
    static juce::String sha256Base64Url(const juce::String& text);
    static juce::String escapeJson(const juce::String& text);
    static juce::String jsonQuote(const juce::String& text);
    static bool decodeBase64Url(const juce::String& encodedText, juce::MemoryBlock& decodedData);
    static juce::String readRequest(juce::StreamingSocket& clientSocket);
    static juce::String createHtmlResponse(const juce::String& title, const juce::String& body);
    static bool writeResponse(juce::StreamingSocket& clientSocket,
                              int statusCode,
                              const juce::String& statusText,
                              const juce::String& body);
    static juce::String getParameterValue(const juce::URL& url, const juce::String& name);
    static juce::String truncateForStatus(const juce::String& text, int maxChars);
    static juce::var decodeJwtPayload(const juce::String& token);
    static std::int64_t parseExpiryToMillis(const juce::String& expiryText);
    static std::uint32_t rotateRight(std::uint32_t value, std::uint32_t amount);
    static std::array<std::uint8_t, 32> sha256(const void* data, size_t size);
    bool exchangeCodeForToken(const juce::String& code);
    bool isRedirectUriAllowed(const juce::String& redirectUri) const;
    juce::URL buildAuthorizationUrl(const juce::String& redirectUri) const;
    void setStatus(const juce::String& text);
    void setError(const juce::String& text);
    void setBusy(bool shouldBeBusy);
    void notifyAuthenticated();
    void persistSession();
    void loadSessionFromValueTree(const juce::ValueTree& tree);

    juce::CriticalSection sessionLock;
    SessionData session;
    juce::String statusMessage;
    juce::String errorMessage;
    juce::String appSlug;
    PendingLogin pending;
    juce::StreamingSocket listener;
    bool loginRequested = false;
    bool loginActive = false;
    bool sessionDirty = false;
};
