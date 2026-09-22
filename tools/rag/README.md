# Frust LiteSemRAG Ingestion Tools

This directory contains the Python tools for generating the Information Space Design (ISD) Semantic RAG database for the Frust IDE Agent.

## Overview
Rather than flooding an LLM context window with plain text, we parse the Frust language documentation and source code into a SQLite graph (`frust_knowledge.db`). This allows the IDE Agent to precisely pull only the necessary nodes (Structs, Functions, Chapters) based on the user's query.

## Scripts
### `litesemrag_builder.py`
Crawls the `wiki/reference` directory and `projects/` source code.
- **Nodes**: Extracted as `AGENT_BRIEF`, `SPEC`, `GRAMMAR`, `CHAPTER`, `POD`, `STRUCT`, and `FUNCTION`.
- **Edges**: `DEFINED_IN`, `IMPLEMENTS`.
- By default, pod ingestion only includes `frate.json` manifests already tracked by Git. Set `FRUST_RAG_INCLUDE_UNTRACKED=1` when experimenting locally with untracked pod folders.

### `build_engineer_tool_cards.py`
Generates the assistant's LiteSemRAG tool cards from
`projects/frust-ide-agent/ENGINEER_TOOLS.json`. The database builder also ingests
`ENGINEER_PROCESS_CARDS.jsonl`. FrustIDE reads the tool catalog
to construct the live provider tool definitions, keeping tool knowledge and
executable capability synchronized.

**Usage:**
```powershell
py tools\rag\build_engineer_tool_cards.py
py tools\rag\litesemrag_builder.py
```
This will output `frust_knowledge.db` in this directory.

To update only tool and process cards without rebuilding language and pod nodes:

```powershell
py tools\rag\litesemrag_builder.py --cards-only
```

The IDE links SQLite directly and reads `tools/rag/frust_knowledge.db` from the
repo root compiled into the Debug/Release build. `AiChatPanel` keeps the
conversation history clean and attaches only the per-request LiteSemRAG context
to the outgoing provider call.

## Schema
- `nodes (id, type, name, content, source_file)`
- `edges (source_id, target_id, relation)`
