#include <ai_provider/Json.h>

#include <iostream>
#include <limits>

int main()
{
    int failures = 0;
    auto expect = [&failures](bool ok, const char* what) {
        if (! ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
    };
    using namespace ai_provider;

    // What JUCE 7 does to an escaped emoji, and the repair.
    const auto rocket = juce::String::charToString((juce::juce_wchar) 0x1F680);
    // (JUCE 7's JSON::parse reads objects and arrays only, so the string sits in an object, as it does in a real reply.)
    const auto viaJuce = juce::JSON::parse(juce::String(R"({"s":"go \ud83d\ude80 now"})")).getProperty("s", {}).toString();
    const auto fixed = json::repairSurrogates(viaJuce);
    expect(fixed == "go " + rocket + " now", "an emoji split into two halves by JUCE's reader is joined again");
    expect(json::repairSurrogates("plain text") == "plain text", "ordinary text is left alone");

    // The writer: valid JSON whatever the text holds.
    auto* object = new juce::DynamicObject();
    object->setProperty("emoji", viaJuce);
    object->setProperty("bell", juce::String::charToString((juce::juce_wchar) 7));
    object->setProperty("quote", "say \"hi\"\\ok\n");
    object->setProperty("n", 3);
    object->setProperty("x", 0.5);
    object->setProperty("inf", std::numeric_limits<double>::infinity());
    object->setProperty("t", true);
    juce::Array<juce::var> list { 1, "two" };
    object->setProperty("list", list);
    const auto text = json::toJson(juce::var(object));

    expect(text.find("\\a") == std::string::npos, "a bell is not written as the invalid escape \\a");
    expect(text.find("\\u0007") != std::string::npos, "it is written as \\u0007");
    expect(text.find("\"inf\":null") != std::string::npos, "infinity is written as null");
    expect(text.find("\"quote\":\"say \\\"hi\\\"\\\\ok\\n\"") != std::string::npos, "quotes, backslashes and line breaks are escaped");
    expect(text.find("\"list\":[1,\"two\"]") != std::string::npos, "arrays are written");
    const std::string rocketUtf8 = "\xF0\x9F\x9A\x80";
    expect(text.find(rocketUtf8) != std::string::npos, "the emoji goes out as real UTF-8");
    expect(text.find("\xED\xA0") == std::string::npos, "no surrogate half is ever written");

    // Written, read back (by JUCE), and written again: the same.
    const auto back = json::repaired(juce::JSON::parse(juce::String::fromUTF8(text.data(), (int) text.size())));
    expect(back.getProperty("emoji", {}).toString() == "go " + rocket + " now", "the round trip keeps the emoji");
    expect(json::toJson(back) == text, "writing what was read gives the same JSON");

    if (failures == 0)
        std::cout << "JsonTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
