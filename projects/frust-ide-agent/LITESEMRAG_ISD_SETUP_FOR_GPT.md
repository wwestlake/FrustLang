# LiteSemRAG + ISD Setup for Frusty

This document is for another GPT or engineering agent taking over the
FrustIDE Virtual Engineer context system.

The goal is not to dump every document into the model. The goal is to
maintain an Information Space Design (ISD) made of small, durable,
queryable cards. LiteSemRAG retrieves only the cards and source nodes
needed for the current request, then the host attaches that context to
the provider call.

## Important Paths

- RAG builder: `D:\FrustLang\tools\rag\litesemrag_builder.py`
- Tool-card generator: `D:\FrustLang\tools\rag\build_engineer_tool_cards.py`
- SQLite knowledge DB: `D:\FrustLang\tools\rag\frust_knowledge.db`
- Always-on agent brief: `D:\FrustLang\projects\frust-ide-agent\FRUST_AI_CONTEXT.md`
- Process source: `D:\FrustLang\projects\frust-ide-agent\ENGINEER_PROCESS.json`
- Process cards: `D:\FrustLang\projects\frust-ide-agent\ENGINEER_PROCESS_CARDS.jsonl`
- Tool source: `D:\FrustLang\projects\frust-ide-agent\ENGINEER_TOOLS.json`
- Tool cards: `D:\FrustLang\projects\frust-ide-agent\ENGINEER_TOOL_CARDS.jsonl`
- Global memory cards: `D:\FrustLang\projects\frust-ide-agent\MEMORY_GLOBAL_CARDS.jsonl`
- Project memory cards: `<open-project>\.frusty\MEMORY_PROJECT_CARDS.jsonl`
- Mode router: `D:\FrustLang\projects\02_juce_language_host\Source\AgentModeRouter.cpp`
- RAG runtime query code: `D:\FrustLang\projects\02_juce_language_host\Source\RAGQuery.cpp`
- Memory tool implementation: `D:\FrustLang\projects\02_juce_language_host\Source\EngineerTools.cpp`

## Rebuild Commands

From `D:\FrustLang`:

```powershell
py tools\rag\build_engineer_tool_cards.py
py tools\rag\litesemrag_builder.py
```

For card-only updates:

```powershell
py tools\rag\litesemrag_builder.py --cards-only
```

Use `--cards-only` after changing process, tool, or global memory cards.
Run the full builder after changing language docs, grammar, wiki reference
files, or pod source/manifests.

## Current ISD Model

LiteSemRAG stores source knowledge in SQLite `nodes` and `edges`.

Current node types include:

- `AGENT_BRIEF`: the always-on Frusty context brief
- `SPEC`: the Frust language spec
- `GRAMMAR`: `frust.y` and `frust.l`, the formal syntax source
- `CHAPTER`: wiki/reference pages
- `POD`: pod manifests
- `STRUCT`: parsed Frust structs from pod source
- `FUNCTION`: parsed public Frust functions from pod source
- `TOOL`: generated tool usage cards
- `PROCESS`: generated process/policy cards
- `MEMORY_GLOBAL`: durable cross-project memory cards
- `MEMORY_PROJECT`: durable per-project memory cards loaded from the open project

The runtime retrieval path tokenizes the user prompt, searches SQLite
nodes by name/content, limits ordinary DB hits to a small set, then adds
matching memory cards afterward. This keeps conversation history small
and makes the current prompt drive the retrieved context.

## Card Types

### Process Cards

Process cards describe how the engineer should behave. They are generated
from:

```text
D:\FrustLang\projects\frust-ide-agent\ENGINEER_PROCESS.json
```

into:

```text
D:\FrustLang\projects\frust-ide-agent\ENGINEER_PROCESS_CARDS.jsonl
```

Use process cards for rules like:

- how to plan
- how to verify
- how to handle missing capabilities
- when to ask the user
- when to stop repeating failed edits
- how to treat runtime identity

### Tool Cards

Tool cards are generated from:

```text
D:\FrustLang\projects\frust-ide-agent\ENGINEER_TOOLS.json
```

into:

```text
D:\FrustLang\projects\frust-ide-agent\ENGINEER_TOOL_CARDS.jsonl
```

The same source also drives live provider tool definitions. Keep tool
knowledge and executable tool definitions synchronized. Do not teach the
agent about a tool in prose only if the host cannot actually expose that
tool.

### Global Memory Cards

Global memory cards live at:

```text
D:\FrustLang\projects\frust-ide-agent\MEMORY_GLOBAL_CARDS.jsonl
```

Use global memory for durable cross-session behavior:

- user preferences
- personality rules
- machine facts that apply across projects
- mode-aware capability rules
- Architect-mode policies

Global memory changes require Full Access in the IDE.

### Project Memory Cards

Project memory cards live under the open project:

```text
<project-root>\.frusty\MEMORY_PROJECT_CARDS.jsonl
```

Use project memory for:

- project conventions
- local build/test procedures
- known project-specific traps
- decisions that should apply only inside that workspace

Project memory changes require Workspace or Full Access.

## Memory Card Shape

Memory cards are JSONL: one JSON object per line.

Required practical fields:

```json
{
  "id": "memory.global.example-rule",
  "scope": "global",
  "kind": "user_rule",
  "title": "Short human title",
  "tokens": ["search", "routing", "words"],
  "priority": 90,
  "status": "active",
  "source": "user-approved-memory",
  "updated_at": "2026-09-23T18:30:00Z",
  "text": "The actual instruction or remembered fact."
}
```

Rules:

- `id` must be stable. Prefer `memory.global.*` or `memory.project.*`.
- `scope` is `global` or `project`.
- `kind` should describe the role: `user_rule`, `personality`,
  `capability_fact`, `architect_policy`, `project_convention`, etc.
- `tokens` are retrieval hooks. Include words the user is likely to say.
- `priority` is the rating. Use `0-100`.
- `status` is `active` or `inactive`.
- `source` should explain origin. User-approved memory uses
  `user-approved-memory`.
- `text` should be directly usable by the LLM. Do not bury the rule in
  vague prose.

## Tracking and Rating Cards

The current implemented rating system is `priority`.

Runtime behavior:

- inactive cards are ignored unless a tool explicitly lists inactive cards
- `personality` cards are always eligible
- other memory cards match by query tokens against id, title, kind, text,
  and tokens
- matching memory cards are sorted by priority descending
- each memory source emits up to six matching cards
- project memory is appended before global memory

Recommended priority bands:

- `100`: hard user rule or critical safety/process rule
- `95-99`: strong durable behavior rule
- `85-94`: important preference or stable machine/project fact
- `70-84`: useful convention or reminder
- `50-69`: low-confidence or situational note
- below `50`: weak memory; consider improving or deleting instead

Track cards by stable `id`, `updated_at`, `source`, `status`, and
priority. When a memory becomes wrong, prefer setting `status` to
`inactive` or deleting it with `memory_delete_card` if the user clearly
asked to forget it.

Future improvement: add explicit evidence fields such as
`confidence`, `last_seen`, `hit_count`, `success_count`, and
`failure_count`. Those are not required by the current runtime.

## Memory Tools

The engineer has tools for durable memory:

- `memory_list_cards`
- `memory_upsert_card`
- `memory_delete_card`

Usage policy:

- Use `memory_list_cards` before changing memory when the relevant card
  may already exist.
- Use `memory_upsert_card` only when the user explicitly asks the agent
  to remember something or clearly approves a durable rule.
- Prefer project scope for project-specific rules.
- Use global scope only for cross-session user preferences, personality,
  machine-level facts, or broad engineering policies.
- Use `memory_delete_card` when the user says a remembered rule is wrong,
  obsolete, or should be forgotten.

## Auto Mode Selection

`Auto` is not a work mode. It is a routing stage.

Flow:

1. User submits a prompt while the mode selector is `Auto`.
2. The host sends a small tool-free classification request.
3. The classifier chooses exactly one mode.
4. The host switches into that mode.
5. Only then does the real request run with LiteSemRAG context, task
   packet, access rules, and tools.

Current modes:

- `conversation`: casual chat, praise, personality, requirements
  discussion, brainstorming, or exploratory back-and-forth where project
  inspection is not needed yet
- `architect`: abstract product, business, domain, workflow,
  requirements, use-case, constraint, risk, trust, or system-shape
  discussion that should stay above implementation
- `answer`: direct factual explanation or question needing no project
  inspection
- `review`: inspect, research, read, diagnose, compare, or report without
  changing files
- `plan`: produce or revise a concrete implementation plan without
  changing files
- `execute`: create, edit, delete, build, run, test, launch, or otherwise
  act on the project

Explicitly selected modes bypass Auto. Output detail is separate from
mode; Brief/Standard/Detailed changes presentation only.

## Mode Routing Rules

The router must return one JSON object only:

```json
{
  "mode": "conversation",
  "continuation": false,
  "confidence": 0.82,
  "reason": "short reason"
}
```

Routing principles:

- If the user asks to create, edit, fix, build, run, test, launch, or
  write files, choose `execute`.
- If the user asks to inspect, diagnose, compare, or report without
  changing files, choose `review`.
- If the user asks for a plan but does not authorize edits, choose
  `plan`.
- If the user wants business/domain/use-case/architecture discussion,
  choose `architect`.
- If the user is just talking socially or exploring requirements without
  project inspection, choose `conversation`.
- If the user asks a factual question about behavior or capability,
  choose `answer`.
- If the request includes inspection or planning as preparation for
  implementation in the same prompt, the final requested outcome wins:
  choose `execute`.

## Mode-Aware Capability Memory

Frusty must not say "I cannot do that" when the truth is "I cannot do
that in this mode."

Example:

- In Conversation mode, Frusty can discuss Python.
- In Execute mode, Frusty can use `run_command` to invoke `py` if access
  and approvals allow it.

This rule currently lives in global memory:

```text
memory.global.frusty-mode-aware-capabilities
```

Do not lose this card. It prevents the assistant from sounding powerless
when a capability is merely mode-gated.

## Card Load Order

Conceptual order:

1. Always-on base context: `FRUST_AI_CONTEXT.md`
2. Retrieved source/language/pod nodes from SQLite
3. Process cards
4. Tool cards
5. Project memory cards
6. Global memory cards
7. Future plugin-contributed cards

The important design principle is that durable memory cards come late.
They are intended to override ordinary broad guidance when applicable,
without replacing hard host constraints such as access control, read-only
roots, approval policy, or mode restrictions.

## Plugin-Contributed Cards

Frusty plugins should eventually contribute:

- behavior cards
- capability cards
- artifact templates
- routing conditions

The prototype contract is documented here:

```text
D:\FrustLang\projects\frust-ide-agent\FRUSTY_PLUGIN_MIGRATION.md
```

The intended split is:

- Frust plugins own behavior knowledge and domain capabilities.
- The C++ host owns trust, visibility, dispatch, access, approvals, and
  containment.

A plugin may say when it should be used. The IDE decides whether the
call is allowed.

## How to Add a New Durable Rule

1. Decide scope:
   - project-specific: project memory
   - cross-project/user preference: global memory
2. Check existing cards with `memory_list_cards`.
3. Write a precise card with strong retrieval tokens.
4. Give it a priority based on importance.
5. Upsert it through `memory_upsert_card`, or edit the JSONL file when
   working directly in source.
6. Run:

```powershell
py tools\rag\litesemrag_builder.py --cards-only
```

7. Test with a prompt whose words should retrieve the card.

## Common Mistakes

- Do not rely on chat history for durable behavior.
- Do not create global memory for a project-local rule.
- Do not put implementation facts into personality cards.
- Do not use vague tokens like only `rule` or `thing`; use words the user
  will actually say.
- Do not teach a tool in a card unless the host exposes the tool.
- Do not let Auto itself become a mode. It is only a classifier pass.
- Do not let Architect mode drift into Execute mode unless the user asks
  to implement.
- Do not use Markdown code blocks as evidence that files were edited.

## Current Gaps to Preserve for Future Work

- Card retrieval is token/LIKE based, not embeddings.
- Memory rating is priority-based only.
- There is no full card analytics table yet.
- Plugin-contributed cards are designed but not fully ingested by the
  host bridge yet.
- Project memory is read dynamically from the open project; global memory
  is also ingested into SQLite by the builder.

Do not "fix" those gaps by pretending they already exist. Preserve the
actual state, then improve deliberately.
