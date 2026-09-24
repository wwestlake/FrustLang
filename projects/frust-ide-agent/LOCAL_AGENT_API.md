# FrustIDE Local Agent API

FrustIDE exposes an authenticated HTTP API on a random loopback port while the IDE is running. It uses the same active conversation, mode, access ceiling, task controller, and engineering tools as the AI Assistant panel. The evaluation harness also uses this API to establish clean, repeatable sessions.

The API never listens on a LAN interface. On startup, FrustIDE writes its connection information to:

```text
%APPDATA%\LagDaemonResearchIDE\agent-api.json
```

The discovery document contains `baseUrl` and a random per-run bearer `token`. It is deleted when the IDE closes. Clients must send:

```http
Authorization: Bearer <token>
```

## Status

```http
GET /v1/status
```

Returns `200` while the API is ready.

## Send a message

```http
POST /v1/messages
Content-Type: application/json

{"content":"Inspect the current project and explain its structure."}
```

Returns `202` with a `requestId`. The message appears in the current AI Assistant conversation. If the assistant is already working, the request finishes with a busy error rather than interrupting it.

## Configure an evaluation session

```http
POST /v1/session
Content-Type: application/json

{
  "projectRoot": "D:\\FrustLang\\tools\\agent-eval\\runs\\example\\workspace",
  "mode": "execute",
  "access": "full",
  "outputDetail": "brief",
  "model": "gpt-4o-mini",
  "newConversation": true
}
```

All fields are optional. Supported modes are `auto`, `plan`, `execute`, and `review`; access values are `observe`, `workspace`, and `full`; output detail values are `brief`, `standard`, and `detailed`. The project root and model must already exist. Task budgets can be set with `maxProviderCalls`, `maxToolCalls`, and `maxTotalTokens`; defaults are 64, 128, and 300,000. Like a message, configuration returns `202` with a `requestId` and is read through the request endpoint.

## Read the reply

```http
GET /v1/requests/{requestId}
```

The status is `queued`, `running`, `completed`, or `failed`. A completed request includes `response`; a failed request includes `error`. Results also include `durationMs` and structured `details` describing the active profile, model, mode, access, project, conversation, and host-observed task evidence. Task evidence includes tool/provider calls, token usage, verification gates, and repeated failures.

## Cancel the active run

```http
POST /v1/cancel
```

Returns `202` with `status: stopping`. Cancellation uses the same stop path as the AI Assistant's Stop button. The worker finishes the current provider request, then stops before another model or tool call.

## Plan review

When an Execute run records a plan, the host pauses in
`waiting-for-plan-approval`. The UI shows the Plan Review panel. The
local API exposes the same gate so orchestration clients can inspect,
approve, edit, or deny the plan.

```http
GET /v1/plan
```

Returns the current conversation id, whether a plan is waiting, the plan
markdown when present, and the task snapshot.

```http
POST /v1/plan/approve
Content-Type: application/json

{"markdown":"optional edited markdown"}
```

If `markdown` is omitted or empty, the currently pending markdown is
approved as-is. Approval switches the assistant into Execute and starts
the same continuation as the UI Approve button.

```http
POST /v1/plan/deny
Content-Type: application/json

{"reason":"What must change before approval."}
```

Denial records feedback and leaves the assistant waiting for the next
user instruction.

## PowerShell example

```powershell
$api = Get-Content "$env:APPDATA\LagDaemonResearchIDE\agent-api.json" -Raw | ConvertFrom-Json
$headers = @{ Authorization = "Bearer $($api.token)" }
$body = @{ content = "List the Frate pods related to networking." } | ConvertTo-Json
$request = Invoke-RestMethod "$($api.baseUrl)/v1/messages" -Method Post -Headers $headers -ContentType "application/json" -Body $body
Invoke-RestMethod "$($api.baseUrl)/v1/requests/$($request.requestId)" -Headers $headers
```

The local API is a conversation transport, not a permission bypass. Write operations are available only when the IDE's access control and task mode authorize an Execute run.

The benchmark runner and baseline suite are in `tools\agent-eval`. Run `agent_eval.py validate` without starting the IDE, or `agent_eval.py run` against the active discovery document.
