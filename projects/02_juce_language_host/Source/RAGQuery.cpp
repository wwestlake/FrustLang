#include "RAGQuery.h"
#include "sqlite/sqlite3.h"

#include <algorithm>
#include <set>

namespace rag {

namespace {

juce::StringArray queryTokens(const juce::String& query)
{
    const auto lowerQuery = query.toLowerCase();
    juce::StringArray rawTokens;
    if (lowerQuery.contains("string")) rawTokens.add("string");
    if (lowerQuery.contains("vector") || lowerQuery.contains("collection"))
    {
        rawTokens.add("vector");
        rawTokens.add("collections");
    }
    if (lowerQuery.contains("print") || lowerQuery.contains("console"))
        rawTokens.add("println_str");
    if (lowerQuery.contains("input") || lowerQuery.contains("read line")
        || lowerQuery.contains("repl"))
        rawTokens.add("read_line");
    rawTokens.addTokens(lowerQuery, " \t\r\n.,!?;:()[]{}<>+-=*/\\|&^%\"'", "");
    rawTokens.trim();
    rawTokens.removeEmptyStrings();

    static const std::set<std::string> stopWords {
        "about", "after", "also", "and", "are", "build", "can", "code", "does",
        "for", "from", "have", "how", "into", "like", "make", "need", "the",
        "this", "that", "use", "what", "when", "where", "with", "write"
    };

    juce::StringArray tokens;
    std::set<std::string> seen;
    for (auto& token : rawTokens)
    {
        token = token.retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789_");
        if (token.length() <= 2)
            continue;

        auto stdToken = token.toStdString();
        if (stopWords.count(stdToken) != 0 || seen.count(stdToken) != 0)
            continue;

        seen.insert(stdToken);
        tokens.add(token);
        if (tokens.size() >= 12)
            break;
    }

    return tokens;
}

juce::String trimContentForPrompt(juce::String content)
{
    content = content.trim();
    if (content.length() <= 1400)
        return content;

    return content.substring(0, 1400).trim() + "\n...";
}

juce::String columnText(sqlite3_stmt* stmt, int column)
{
    auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
    return text != nullptr ? juce::String(text) : juce::String();
}

bool cardMatchesQuery(const juce::var& card, const juce::StringArray& tokens)
{
    const auto status = card.getProperty("status", "active").toString();
    if (status.isNotEmpty() && !status.equalsIgnoreCase("active"))
        return false;
    if (card.getProperty("kind", {}).toString().equalsIgnoreCase("personality"))
        return true;

    juce::String haystack;
    haystack << card.getProperty("id", {}).toString() << " "
             << card.getProperty("title", {}).toString() << " "
             << card.getProperty("kind", {}).toString() << " "
             << card.getProperty("text", {}).toString();

    if (auto* cardTokens = card.getProperty("tokens", {}).getArray())
        for (const auto& token : *cardTokens)
            haystack << " " << token.toString();

    const auto lower = haystack.toLowerCase();
    for (const auto& token : tokens)
        if (lower.contains(token.toLowerCase()))
            return true;
    return false;
}

void appendMemoryCards(RetrievalResult& result, const juce::File& cardsFile,
                       const juce::String& nodeType, const juce::String& label)
{
    if (!cardsFile.existsAsFile() || result.tokens.isEmpty())
        return;

    struct MemoryMatch
    {
        int priority = 0;
        int line = 0;
        juce::var card;
    };
    std::vector<MemoryMatch> matches;
    juce::StringArray lines;
    lines.addLines(cardsFile.loadFileAsString());
    for (int index = 0; index < lines.size(); ++index)
    {
        const auto line = lines[index].trim();
        if (line.isEmpty())
            continue;
        const auto parsed = juce::JSON::parse(line);
        if (!parsed.isObject() || !cardMatchesQuery(parsed, result.tokens))
            continue;
        matches.push_back({ static_cast<int>(parsed.getProperty("priority", 50)), index, parsed });
    }

    std::stable_sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return left.priority > right.priority;
    });

    int emitted = 0;
    for (const auto& match : matches)
    {
        const auto id = match.card.getProperty("id", {}).toString();
        const auto title = match.card.getProperty("title", id).toString();
        auto text = trimContentForPrompt(match.card.getProperty("text", {}).toString());
        if (id.isEmpty() || text.isEmpty())
            continue;

        RetrievedNode node;
        node.id = id;
        node.type = nodeType;
        node.name = title;
        node.sourceFile = cardsFile.getFullPathName();
        node.content = text;
        result.nodes.push_back(std::move(node));

        if (result.context.isEmpty())
            result.context = "Relevant Frust Language Context (LiteSemRAG):\n";
        result.context << "\n--- [" << nodeType << "] " << title << " (" << label << ") ---\n"
                       << text << "\n";
        if (++emitted >= 6)
            break;
    }
}

} // namespace

juce::File getKnowledgeDatabaseFile()
{
    return juce::File(FRUST_REPO_ROOT_DIR)
        .getChildFile("tools")
        .getChildFile("rag")
        .getChildFile("frust_knowledge.db");
}

juce::File getGlobalMemoryCardsFile()
{
    return juce::File(FRUST_REPO_ROOT_DIR)
        .getChildFile("projects")
        .getChildFile("frust-ide-agent")
        .getChildFile("MEMORY_GLOBAL_CARDS.jsonl");
}

juce::File getProjectMemoryCardsFile(const juce::File& projectRoot)
{
    return projectRoot.getChildFile(".frusty").getChildFile("MEMORY_PROJECT_CARDS.jsonl");
}

RetrievalResult getRetrievalForQuery(const juce::String& query, const juce::File& projectRoot)
{
    RetrievalResult result;
    result.query = query;

    auto dbFile = getKnowledgeDatabaseFile();
    if (!dbFile.existsAsFile())
    {
        result.tokens = queryTokens(query);
        appendMemoryCards(result, getProjectMemoryCardsFile(projectRoot), "MEMORY_PROJECT", "project memory");
        appendMemoryCards(result, getGlobalMemoryCardsFile(), "MEMORY_GLOBAL", "global memory");
        return result;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbFile.getFullPathName().toUTF8(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db != nullptr)
            sqlite3_close(db);
        appendMemoryCards(result, getProjectMemoryCardsFile(projectRoot), "MEMORY_PROJECT", "project memory");
        appendMemoryCards(result, getGlobalMemoryCardsFile(), "MEMORY_GLOBAL", "global memory");
        return result;
    }

    auto tokens = queryTokens(query);
    result.tokens = tokens;
    if (tokens.isEmpty())
    {
        sqlite3_close(db);
        return result;
    }

    juce::String context = "Relevant Frust Language Context (LiteSemRAG):\n";
    int resultsFound = 0;
    std::set<std::string> seenNodeIds;

    for (auto& token : tokens)
    {
        const char* sql =
            "SELECT id, type, name, content, source_file FROM nodes "
            "WHERE lower(name) = ? OR lower(name) LIKE ? OR lower(content) LIKE ? "
            "ORDER BY CASE "
            "WHEN lower(name) = ? THEN 0 "
            "WHEN lower(name) LIKE ? THEN 1 "
            "ELSE 2 END, length(content) ASC "
            "LIMIT 4;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            juce::String pattern = "%" + token + "%";
            sqlite3_bind_text(stmt, 1, token.toUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pattern.toUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, pattern.toUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, token.toUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, pattern.toUTF8(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                auto* idText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (idText == nullptr || seenNodeIds.count(idText) != 0)
                    continue;

                seenNodeIds.insert(idText);

                auto type = columnText(stmt, 1);
                auto name = columnText(stmt, 2);
                auto content = columnText(stmt, 3);
                auto source = columnText(stmt, 4);
                const auto trimmedContent = trimContentForPrompt(content);

                RetrievedNode node;
                node.id = juce::String(idText);
                node.type = type;
                node.name = name;
                node.sourceFile = source;
                node.content = trimmedContent;
                result.nodes.push_back(std::move(node));

                context << "\n--- [" << type << "] " << name;
                if (source.isNotEmpty())
                    context << " (" << source << ")";
                context << " ---\n" << trimmedContent << "\n";
                resultsFound++;
                if (resultsFound >= 6)
                    break;
            }
            sqlite3_finalize(stmt);
        }
        if (resultsFound >= 6)
            break;
    }

    sqlite3_close(db);

    if (resultsFound == 0)
    {
        appendMemoryCards(result, getProjectMemoryCardsFile(projectRoot), "MEMORY_PROJECT", "project memory");
        appendMemoryCards(result, getGlobalMemoryCardsFile(), "MEMORY_GLOBAL", "global memory");
        return result;
    }

    result.context = context;
    appendMemoryCards(result, getProjectMemoryCardsFile(projectRoot), "MEMORY_PROJECT", "project memory");
    appendMemoryCards(result, getGlobalMemoryCardsFile(), "MEMORY_GLOBAL", "global memory");
    return result;
}

RetrievalResult getRetrievalForQuery(const juce::String& query)
{
    return getRetrievalForQuery(query, {});
}

juce::String getContextForQuery(const juce::String& query)
{
    return getRetrievalForQuery(query).context;
}

juce::String getContextForQuery(const juce::String& query, const juce::File& projectRoot)
{
    return getRetrievalForQuery(query, projectRoot).context;
}

}
