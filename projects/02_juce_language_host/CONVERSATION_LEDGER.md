# DjehutiSuite Conversation Ledger

## Purpose

Store each AI conversation as a readable JSON file while making accidental or
unauthorised edits detectable. This is a local hash-chained ledger: each message
block commits to the previous block and to its own content. It uses no
proof-of-work and has no network or consensus dependency.

The current reference implementation is:

- `Source/ConversationStore.h`
- `Source/ConversationStore.cpp`
- `Source/ConversationStoreTests.cpp`
- `Source/AiChatPanel.cpp`

## Storage model

There is one JSON file per conversation. The filename is the conversation UUID:

```text
<conversation-folder>/<uuid>.json
```

Default folders on Windows are:

```text
%USERPROFILE%\Documents\LagDaemon Research IDE\Conversations
%USERPROFILE%\Documents\LagDaemon Research IDE\Conversation Archive
```

Both paths are user-configurable. The IDE persists them in its application
properties as `aiConversationFolder` and `aiConversationArchiveFolder`.

Writes use JUCE `File::replaceWithText`, which writes through a temporary file
and replaces the destination. Archiving moves the unchanged JSON file from the
active folder into the archive folder.

## JSON schema

```json
{
  "schema": "djehuti-conversation-chain",
  "schemaVersion": 1,
  "id": "conversation-uuid",
  "title": "Derived from the first user message",
  "createdAt": "ISO-8601 timestamp",
  "updatedAt": "ISO-8601 timestamp of the final block",
  "blocks": [
    {
      "index": 0,
      "timestamp": "ISO-8601 timestamp",
      "role": "user",
      "content": "Message text",
      "previousHash": "64 zeroes for the genesis block",
      "hash": "64-character lowercase SHA-256 hex digest"
    },
    {
      "index": 1,
      "timestamp": "ISO-8601 timestamp",
      "role": "assistant",
      "content": "Response text",
      "previousHash": "hash from block 0",
      "hash": "SHA-256 digest for this block"
    }
  ],
  "integrity": {
    "algorithm": "SHA-256",
    "blockCount": 2,
    "headHash": "hash from the final block"
  }
}
```

Only `user` and `assistant` turns are stored. System prompts, provider secrets,
temporary status text, and LiteSemRAG retrieval context are not conversation
blocks. This keeps the ledger an honest record of the visible dialogue.

## Canonical hash algorithm

Hashing must not depend on JSON whitespace or property order. Build a byte
stream from these fields in this exact order:

1. `djehuti-conversation-chain`
2. schema version as decimal text (`1`)
3. conversation UUID
4. conversation `createdAt`
5. block index as decimal text
6. block timestamp
7. role
8. content
9. previous hash

Encode every field as UTF-8 and append it as:

```text
<UTF-8 byte count>:<UTF-8 bytes>
```

There are no separators between encoded fields beyond each length and colon.
The block hash is the lowercase hexadecimal SHA-256 digest of the completed byte
stream. The first block uses 64 ASCII zeroes as `previousHash`; every later block
uses the preceding block's `hash`.

Length-prefixing makes the input unambiguous even when message content includes
newlines, colons, JSON, or arbitrary Unicode.

## Verification rules

Loaders must reject a conversation when any rule fails:

- Schema name and version must be supported.
- Integrity algorithm must be `SHA-256`.
- Declared block count must equal the actual array length.
- Block indexes must begin at zero and be contiguous.
- Roles must be `user` or `assistant` and timestamps must be present.
- Genesis `previousHash` must be 64 zeroes.
- Every later `previousHash` must equal the preceding block hash.
- Every block hash must match a fresh canonical hash calculation.
- Integrity `headHash` must equal the final block hash.
- Title must match the title derived from the first user message.
- `updatedAt` must equal the final block timestamp, or `createdAt` when empty.

The UI labels unreadable or failed-integrity files as `[ALTERED]` and does not
load their messages into model history. A new clean conversation remains
available so one damaged file cannot disable the agent.

## Title derivation

The title comes from the first user message. Replace carriage returns, newlines,
and tabs with spaces, collapse repeated spaces, trim the result, and limit it to
60 characters. When truncated, retain the first 57 trimmed characters and add
`...`. Empty conversations use `New conversation`.

## Suite integration contract

All DjehutiSuite applications should use the same schema and canonical hash
algorithm so conversations can be indexed, moved, archived, and verified by
shared infrastructure. Each application may choose its own configured folders,
but should preserve UUID filenames and move the original bytes when archiving.

Recommended application flow:

1. Create a UUID and creation timestamp in memory.
2. Append and atomically save the user block before starting an LLM request.
3. Send a separate history snapshot enriched with system and RAG context.
4. Append and atomically save the visible assistant response.
5. Verify the complete chain before displaying a stored conversation or moving
   it into the archive.

The test target `ConversationStoreTests` proves save, reload, block-content
tamper detection, archive movement, and verification after archive.

## Security boundary

This format is tamper-evident, not literally immutable. It reliably detects an
edit, insertion, deletion, or reordering when hashes and integrity metadata are
not also regenerated. A program with permission to rewrite the entire file can
recalculate a new valid plain SHA-256 chain.

If DjehutiSuite later needs proof against a malicious local writer, keep this
schema and add an authenticated external anchor: for example, sign each head
hash with a key protected by Windows DPAPI, or periodically publish head hashes
to a separate append-only store. That can be added as a new integrity field and
schema version without changing the message block model.
