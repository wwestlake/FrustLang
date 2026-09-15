#include "RAGQuery.h"
#include "sqlite/sqlite3.h"

#include <set>

namespace rag {

namespace {

juce::StringArray queryTokens(const juce::String& query)
{
    juce::StringArray rawTokens;
    rawTokens.addTokens(query.toLowerCase(), " \t\r\n.,!?;:()[]{}<>+-=*/\\|&^%\"'", "");
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

} // namespace

juce::File getKnowledgeDatabaseFile()
{
    return juce::File(FRUST_REPO_ROOT_DIR)
        .getChildFile("tools")
        .getChildFile("rag")
        .getChildFile("frust_knowledge.db");
}

juce::String getContextForQuery(const juce::String& query)
{
    auto dbFile = getKnowledgeDatabaseFile();
    if (!dbFile.existsAsFile())
        return {};

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbFile.getFullPathName().toUTF8(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db != nullptr)
            sqlite3_close(db);
        return {};
    }

    auto tokens = queryTokens(query);
    if (tokens.isEmpty())
    {
        sqlite3_close(db);
        return {};
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

                context << "\n--- [" << type << "] " << name;
                if (source.isNotEmpty())
                    context << " (" << source << ")";
                context << " ---\n" << trimContentForPrompt(content) << "\n";
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
        return {};

    return context;
}

}
