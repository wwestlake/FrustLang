# Frusty Plugin Migration

## Purpose

Move Frusty's behavioral framework out of hardcoded C++ and into Frust-authored IDE plugins while keeping C++ responsible for host safety, permissions, approval gates, and execution.

The current C++ implementation proved the workflow ideas: modes, task gates, planning, memory cards, RAG audit, approval, and tool mediation. The next iteration should keep those lessons but let Frust plugins contribute behavior and artifacts without rebuilding the IDE for every change.

## First Boundary

The first plugin boundary is behavior/context contribution, not tool execution.

Frust plugins should initially provide:

- behavior cards
- mode guidance
- artifact templates
- process policies
- capture conventions

C++ should continue to enforce:

- file and command permissions
- approval prompts
- access levels
- tool execution
- conversation integrity
- RAG audit
- plugin loading trust

## Prototype Plugin

Initial source:

`D:\FrustLang\projects\02_juce_language_host\plugins\frusty_architect.frust`

This plugin contributes the Architect behavior pack:

- abstract-zone posture
- artifact literacy
- open-ended requirements questions
- use cases as architecture constructs
- durable capture discipline
- business/domain-first architecture thinking

## Contract v0

A Frusty behavior plugin declares a normal Frust plugin `manifest` and exposes these entry points:

```frust
pub fn frusty_plugin_kind() -> String
pub fn frusty_cards_jsonl() -> String
pub fn frusty_artifact_templates_json() -> String
pub fn frusty_capabilities_json() -> String
```

### `frusty_plugin_kind`

Returns:

```text
frusty_extension
```

The host uses this to distinguish assistant behavior plugins from ordinary callable IDE plugins.

### `frusty_cards_jsonl`

Returns newline-delimited JSON cards. These cards should be ingested into the same late-loading context path as memory/process cards.

Each card should include at minimum:

```json
{
  "id": "frusty.architect.abstract-zone",
  "kind": "architect_policy",
  "title": "Architect mode stays in the abstract zone",
  "tokens": ["architect", "requirements"],
  "text": "..."
}
```

### `frusty_artifact_templates_json`

Returns a JSON object containing artifact templates:

```json
{
  "templates": [
    {
      "id": "use-case",
      "title": "Use Case",
      "sections": ["High Purpose", "Actor", "Goal"]
    }
  ]
}
```

These templates are not modes. They are artifact literacy available inside Architect mode and other future workflows.

### `frusty_capabilities_json`

Returns the concrete Frusty-facing capabilities this plugin contributes:

```json
{
  "capabilities": [
    {
      "id": "architect.capture.use_case",
      "title": "Capture use case",
      "kind": "artifact_template",
      "template": "use-case",
      "routeWhen": [
        "The user describes a workflow and asks to document, capture, formalize, or write up a use case."
      ],
      "mode": "architect",
      "requiresApproval": false
    }
  ]
}
```

Cards teach Frusty how to think about the plugin. Capabilities tell the
IDE what the plugin can actually do and under what routing conditions it
should be considered.

## Host Bridge Needed

The IDE needs a small C++ bridge that:

1. Scans loaded plugins for `frusty_plugin_kind`.
2. Calls `frusty_cards_jsonl`.
3. Validates each JSONL card.
4. Adds the cards to the dynamic LiteSemRAG/context source.
5. Calls `frusty_artifact_templates_json`.
6. Makes templates available to Architect/capture UI.
7. Calls `frusty_capabilities_json`.
8. Registers the returned capabilities with the IDE's tool/router layer.

The bridge should hot-reload plugin contributions when a plugin is reloaded.

## Plugin UI Contract

Plugins can expose user interface surfaces in tiers. The first tier is
implemented now; later tiers deliberately keep higher trust boundaries.

### Tier 1: Declarative Native UI

A plugin may export:

```frust
pub fn frusty_ui_manifest_json() -> String
pub fn frusty_ui_handle_event_json(event_json: String) -> String
```

`frusty_ui_manifest_json` returns JSON. The IDE renders it as normal JUCE
controls inside a dockable panel. Current control types:

- `label`
- `text`
- `textarea`
- `button`

Example:

```json
{
  "panels": [
    {
      "id": "architect",
      "title": "Architect",
      "controls": [
        { "type": "label", "text": "Architect behavior pack is loaded." },
        { "type": "textarea", "id": "note", "label": "Architecture note" },
        { "type": "button", "id": "echo_note", "label": "Send UI Event" }
      ]
    }
  ]
}
```

When a button is clicked, the IDE calls `frusty_ui_handle_event_json` with
an event object:

```json
{
  "panelId": "architect",
  "controlId": "echo_note",
  "type": "click",
  "values": {
    "note": "User-entered text"
  }
}
```

The plugin returns text for the panel status area. The IDE owns rendering,
layout, event dispatch, permission checks, and plugin lifetime safety.

### Tier 2: Service-Bound UI

The next tier should let declarative controls bind to host services such
as `ide.workspace.*`, `ide.context.*`, `ide.approval.*`, and
`ide.tools.*`. The plugin still describes intent and event handlers; the
host still decides whether the current mode/access level permits the
operation.

### Tier 3: Custom Canvas or Widget

For richer tools, a plugin may eventually request a custom canvas or
widget area with a constrained drawing/event API. This is useful for
visualizations, inspectors, small graph editors, workflow maps, and
domain-specific controls without granting raw native UI access.

### Tier 4: Native JUCE Editor

The highest-trust tier is a native editor surface, similar in spirit to a
VST plugin editor. This should require explicit trust/signing and a
stronger review path because native UI code can do much more than a
declarative manifest.

## Current UI Prototype

Initial source:

`D:\FrustLang\projects\02_juce_language_host\Source\PluginUiPanel.h`

`D:\FrustLang\projects\02_juce_language_host\Source\PluginUiPanel.cpp`

Sample plugin:

`D:\FrustLang\projects\02_juce_language_host\plugins\frusty_architect.frust`

The Architect plugin now declares a small native docked panel to prove
the plugin UI path.

## Host Services

The plugin host already supports `frust_register_service` and
`frust_lookup_service`. FrustIDE should use that mechanism as the normal
capability surface between the IDE and Frust-authored behavior plugins.

The first IDE service is:

```text
ide.log(message: String, severity: i64) -> i64
```

Severity values:

- `0`: info
- `1`: warning
- `2`: error

Plugins should call `frust_lookup_service("ide.log")` during `on_init`
and announce themselves if the service is present. The IDE logs plugin
discovery, load, init result, reload, unload, and any plugin-originated
messages. A plugin should not assume console output is visible in the
GUI.

Future service families should follow the same naming style:

- `ide.workspace.*` for project and file-tree capabilities
- `ide.context.*` for LiteSemRAG/context injection and audit
- `ide.approval.*` for explicit user approval flows
- `ide.secrets.*` for key and credential access
- `ide.tools.*` for registered tool invocation
- `ide.ui.*` for panels, plans, and review surfaces

## Frusty Plugin Routing Contract

A behavior plugin should provide enough information for Frusty and the
IDE to route work deliberately:

1. Identify itself and announce load through `ide.log`.
2. Supply LiteSemRAG cards explaining what the plugin does, when it
   should be considered, what inputs it expects, and what it must not do.
3. Supply callable capabilities or artifact templates through named
   entry points.
4. Let the IDE act as the authority layer. The plugin can describe when
   it is useful, but the IDE decides whether the call is allowed under
   the current mode, access level, workspace grants, and approval policy.

In short: Frust plugins own behavior knowledge and domain capabilities;
the C++ host owns trust, visibility, dispatch, and containment.

## Plugin Factory Workflow

Frusty should treat a user statement such as "I wish the app had..."
as a possible plugin opportunity.

The expected workflow is:

1. Offer: "Would you like me to construct a plugin for that?"
2. Interview: ask open requirements questions about purpose, workflow,
   UI placement, data access, write access, approvals, and acceptance
   tests.
3. Design: produce the plugin manifest, capability routing, LiteSemRAG
   cards, permission model, and local test plan.
4. Build locally: create source, docs, cards, and tests; load or smoke
   test the plugin in FrustIDE.
5. Release packet: call `plugin_prepare_release_packet` to generate:
   - `PLUGIN_RELEASE_PACKET.md`
   - `REGISTRY_METADATA.json`
   - `FORUM_ANNOUNCEMENT_DRAFT.md`
6. Final approval: show the release packet to the user and ask for an
   explicit final go.
7. Publish: run the authenticated registry publication path only after
   local verification and final approval.
8. Announce: post the reviewed forum announcement only after the registry
   publication succeeds.
9. Learn: add or update cards so future Frusty runs can suggest the
   published plugin instead of rebuilding the same capability.

## Migration Order

1. Architect behavior cards and artifact templates.
2. Plan-review policy and plan artifact schema.
3. Mode router prompt/rules.
4. Task process policies.
5. Tool descriptions and tool-use guidance.
6. Optional Frust-authored tool implementations, while keeping C++ permission enforcement.

## Rule

C++ owns trust. Frust plugins own behavior.
