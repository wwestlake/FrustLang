# Frusty Process Cards

Process cards teach Frusty how work is supposed to move inside a project.
They are not tool descriptions and they are not hardcoded assistant logic.
They are retrievable LiteSemRAG policy cards that explain workflow,
authority, gates, evidence, and escalation rules.

## Shape

Each process card is JSONL, one card per line:

```json
{
  "id": "frustlang.process.git.integration-flow",
  "kind": "project_process",
  "title": "Use development as the integration branch",
  "scope": "project",
  "project": "FrustLang",
  "source": "projects/frust-ide-agent/PROJECT_PROCESS_CARDS.jsonl",
  "tokens": ["git", "branch", "development", "master", "pull request"],
  "trigger": "The assistant is changing source, docs, build files, cards, or generated project assets.",
  "authority": "Project owner workflow for FrustLang.",
  "steps": [
    "Start from development.",
    "Create a feature branch from development.",
    "Commit focused work on the feature branch.",
    "Merge the feature branch back into development when ready.",
    "Push development.",
    "Open a pull request from development to master."
  ],
  "gates": [
    "Do not merge to master directly.",
    "Do not treat a random feature branch as the integration branch.",
    "Do not bypass development unless the user explicitly overrides this project process."
  ],
  "evidence": [
    "git status shows the intended branch and a clean tree.",
    "the pushed integration branch is development.",
    "the PR base is master and the PR head is development."
  ],
  "text": "Human-readable policy text for retrieval."
}
```

## Required Fields

- `id`: stable unique id.
- `kind`: usually `project_process` or `global_process`.
- `title`: short human title.
- `scope`: `project` or `global`.
- `source`: source file path.
- `tokens`: retrieval triggers.
- `text`: the complete natural-language rule Frusty should follow.

## Recommended Fields

- `project`: project name when scope is project.
- `trigger`: when this process applies.
- `authority`: why the process is binding.
- `steps`: ordered workflow.
- `gates`: things that must not happen without explicit override.
- `evidence`: what proves the process was followed.
- `escalation`: when Frusty should stop and ask.

## Process Rule

Git is not a file store. Git is a coordination process. A Git card should
always say which branch is the integration branch, where feature branches
come from, when commits happen, what is pushed, and what branch is used
as the pull request head.
