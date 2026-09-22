# FrustIDE Local Agent API

FrustIDE exposes an authenticated HTTP API on a random loopback port while the IDE is running. It uses the same active conversation, mode, access ceiling, task controller, and engineering tools as the AI Assistant panel.

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

## Read the reply

```http
GET /v1/requests/{requestId}
```

The status is `queued`, `running`, `completed`, or `failed`. A completed request includes `response`; a failed request includes `error`.

## PowerShell example

```powershell
$api = Get-Content "$env:APPDATA\LagDaemonResearchIDE\agent-api.json" -Raw | ConvertFrom-Json
$headers = @{ Authorization = "Bearer $($api.token)" }
$body = @{ content = "List the Frate pods related to networking." } | ConvertTo-Json
$request = Invoke-RestMethod "$($api.baseUrl)/v1/messages" -Method Post -Headers $headers -ContentType "application/json" -Body $body
Invoke-RestMethod "$($api.baseUrl)/v1/requests/$($request.requestId)" -Headers $headers
```

The local API is a conversation transport, not a permission bypass. Write operations are available only when the IDE's access control and task mode authorize an Execute run.
