#include "ai_provider/Json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

// JUCE 7's JSON reader keeps an escaped surrogate pair (how JSON writes an emoji) as two separate, invalid characters, and its
// writer turns a bell character into "\a", which is not JSON. A saved model reply with one emoji in it was sent back on the next
// step as invalid Unicode, and OpenAI rejected the whole request ("failed to parse JSON value"). So everything read from OpenAI
// is repaired, and every request is written by this writer instead.
namespace ai_provider::json
{
namespace
{
bool isHighSurrogate(juce::juce_wchar c) { return c >= 0xD800 && c <= 0xDBFF; }
bool isLowSurrogate(juce::juce_wchar c) { return c >= 0xDC00 && c <= 0xDFFF; }

void appendUtf8(std::string& out, juce::juce_wchar c);
void writeJsonString(std::string& out, const juce::String& text);
void writeJson(std::string& out, const juce::var& value);
}

juce::String repairSurrogates(const juce::String& text)
{
    bool broken = false;
    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (isHighSurrogate(c) || isLowSurrogate(c)) { broken = true; break; }
    }
    if (! broken)
        return text;

    juce::String out;
    out.preallocateBytes(text.getNumBytesAsUTF8());
    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (isHighSurrogate(c))
        {
            auto next = p;
            const auto low = next.isEmpty() ? 0 : next.getAndAdvance();
            if (isLowSurrogate(low))
            {
                out += (juce::juce_wchar) (0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00));
                p = next;
            }
            else
                out += (juce::juce_wchar) 0xFFFD;
        }
        else if (isLowSurrogate(c))
            out += (juce::juce_wchar) 0xFFFD;
        else
            out += c;
    }
    return out;
}

juce::var repaired(const juce::var& value)
{
    if (value.isString())
        return repairSurrogates(value.toString());
    if (auto* array = value.getArray())
    {
        juce::Array<juce::var> copy;
        for (const auto& element : *array)
            copy.add(repaired(element));
        return copy;
    }
    if (auto* object = value.getDynamicObject())
    {
        auto* copy = new juce::DynamicObject();
        for (const auto& property : object->getProperties())
            copy->setProperty(property.name, repaired(property.value));
        return juce::var(copy);
    }
    return value;
}

namespace
{
void appendUtf8(std::string& out, juce::juce_wchar c)
{
    const auto v = (uint32_t) c;
    if (v < 0x80) out.push_back((char) v);
    else if (v < 0x800) { out.push_back((char) (0xC0 | (v >> 6))); out.push_back((char) (0x80 | (v & 0x3F))); }
    else if (v < 0x10000) { out.push_back((char) (0xE0 | (v >> 12))); out.push_back((char) (0x80 | ((v >> 6) & 0x3F))); out.push_back((char) (0x80 | (v & 0x3F))); }
    else { out.push_back((char) (0xF0 | (v >> 18))); out.push_back((char) (0x80 | ((v >> 12) & 0x3F))); out.push_back((char) (0x80 | ((v >> 6) & 0x3F))); out.push_back((char) (0x80 | (v & 0x3F))); }
}

void writeJsonString(std::string& out, const juce::String& text)
{
    out.push_back('"');
    const auto clean = repairSurrogates(text);
    for (auto p = clean.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20)
                {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\u%04x", (unsigned) c);
                    out += escape;
                }
                else
                    appendUtf8(out, c);
        }
    }
    out.push_back('"');
}

void writeJson(std::string& out, const juce::var& value)
{
    if (value.isString())
        writeJsonString(out, value.toString());
    else if (value.isBool())
        out += (bool) value ? "true" : "false";
    else if (value.isInt() || value.isInt64())
        out += std::to_string((juce::int64) value);
    else if (value.isDouble())
    {
        const double d = value;
        if (std::isfinite(d))
        {
            char number[64];
            std::snprintf(number, sizeof(number), "%.17g", d);
            out += number;
        }
        else
            out += "null";
    }
    else if (auto* array = value.getArray())
    {
        out.push_back('[');
        for (int i = 0; i < array->size(); ++i)
        {
            if (i > 0) out.push_back(',');
            writeJson(out, (*array)[i]);
        }
        out.push_back(']');
    }
    else if (auto* object = value.getDynamicObject())
    {
        out.push_back('{');
        bool first = true;
        for (const auto& property : object->getProperties())
        {
            if (! first) out.push_back(',');
            first = false;
            writeJsonString(out, property.name.toString());
            out.push_back(':');
            writeJson(out, property.value);
        }
        out.push_back('}');
    }
    else
        out += "null";
}
}

std::string toJson(const juce::var& value)
{
    std::string out;
    writeJson(out, value);
    return out;
}
}
