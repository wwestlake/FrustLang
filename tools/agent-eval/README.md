# FrustIDE Agent Evaluation Harness

This harness runs repeatable Virtual Engineer tasks through the authenticated loopback API of a running FrustIDE. Each case receives a fresh conversation and a fresh copy of its fixture, so earlier conversations and file changes cannot influence later scores.

It records:

- assertion correctness and passed cases;
- task status and verification gates;
- tool and provider calls;
- repeated failures;
- elapsed time;
- provider-reported input, output, and total tokens.

Useful assertion types include file checks (`file_exists`, `file_contains`,
`file_contains_any`), task-state checks (`task_field`, `max_tool_calls`,
`max_provider_calls`, `max_total_tokens`), response checks, `command_exit`,
which runs a command inside the resulting workspace and verifies its exit code,
and `built_executable_exit`, which runs the first `.exe` produced under
`build`.

## Validate

Validation is offline and does not spend API tokens:

```powershell
py tools\agent-eval\agent_eval.py validate tools\agent-eval\baseline.json
```

## Run

Start the newly built FrustIDE, sign in or configure the API profile, and run:

```powershell
py tools\agent-eval\agent_eval.py run tools\agent-eval\baseline.json --model gpt-4o-mini
```

Run one case while iterating on a specific behavior:

```powershell
py tools\agent-eval\agent_eval.py run tools\agent-eval\baseline.json --model gpt-4o-mini --case repair-frust
```

The runner discovers the active IDE through `%APPDATA%\LagDaemonResearchIDE\agent-api.json`. Results are written under `tools\agent-eval\runs\<run-id>\` as `run.json`, `report.md`, per-case JSON, and the resulting workspaces.

Use the exact same suite for every model comparison. A result is evidence about the complete model-plus-FrustIDE system at that commit, not about the model in isolation.
