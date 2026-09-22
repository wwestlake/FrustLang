#!/usr/bin/env python3
"""Run repeatable FrustIDE Virtual Engineer evaluations through its local API."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


SCHEMA = "frustide-agent-eval-suite"
TERMINAL = {"completed", "failed"}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", value).strip("-") or "run"


def file_snapshot(root: Path) -> dict[str, str]:
    snapshot = {}
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        relative = path.relative_to(root).as_posix()
        snapshot[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return snapshot


def validate_suite(suite_path: Path) -> tuple[dict, list[str]]:
    errors: list[str] = []
    try:
        suite = load_json(suite_path)
    except (OSError, json.JSONDecodeError) as exc:
        return {}, [f"Could not read suite: {exc}"]
    if suite.get("schema") != SCHEMA:
        errors.append(f"schema must be {SCHEMA!r}")
    cases = suite.get("cases")
    if not isinstance(cases, list) or not cases:
        errors.append("cases must be a non-empty array")
        return suite, errors
    seen = set()
    for index, case in enumerate(cases):
        prefix = f"cases[{index}]"
        case_id = case.get("id")
        if not isinstance(case_id, str) or not case_id.strip():
            errors.append(f"{prefix}.id must be a non-empty string")
        elif case_id in seen:
            errors.append(f"duplicate case id: {case_id}")
        else:
            seen.add(case_id)
        if not isinstance(case.get("prompt"), str) or not case["prompt"].strip():
            errors.append(f"{prefix}.prompt must be a non-empty string")
        fixture = case.get("fixture")
        if not isinstance(fixture, str) or not (suite_path.parent / fixture).is_dir():
            errors.append(f"{prefix}.fixture is not an existing directory: {fixture!r}")
        if not isinstance(case.get("assertions", []), list):
            errors.append(f"{prefix}.assertions must be an array")
    return suite, errors


class AgentApi:
    def __init__(self, discovery_path: Path):
        discovery = load_json(discovery_path)
        self.base_url = discovery["baseUrl"].rstrip("/")
        self.token = discovery["token"]

    def call(self, method: str, path: str, body: dict | None = None) -> dict:
        payload = None if body is None else json.dumps(body).encode("utf-8")
        request = urllib.request.Request(
            self.base_url + path,
            data=payload,
            method=method,
            headers={"Authorization": f"Bearer {self.token}", "Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {exc.code} from {path}: {detail}") from exc

    def submit_and_wait(self, path: str, body: dict, timeout_seconds: int) -> dict:
        submitted = self.call("POST", path, body)
        request_id = submitted.get("requestId")
        if not request_id:
            raise RuntimeError(f"No requestId returned by {path}")
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            result = self.call("GET", f"/v1/requests/{request_id}")
            if result.get("status") in TERMINAL:
                return result
            time.sleep(0.5)
        try:
            self.call("POST", "/v1/cancel", {})
        finally:
            raise TimeoutError(
                f"Request {request_id} did not finish within {timeout_seconds} seconds; cancellation requested")


def nested(details: dict, dotted: str):
    value = details
    for part in dotted.split("."):
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    return value


def evaluate_assertion(assertion: dict, result: dict, workspace: Path) -> tuple[bool, str]:
    kind = assertion.get("type")
    response = result.get("response", "")
    details = result.get("details", {})
    task = details.get("task", {}) if isinstance(details, dict) else {}
    if kind in {"response_contains", "response_not_contains"}:
        expected = str(assertion.get("value", ""))
        found = expected.lower() in response.lower()
        passed = found if kind == "response_contains" else not found
        return passed, f"response {'contains' if found else 'does not contain'} {expected!r}"
    if kind in {"file_exists", "file_not_exists"}:
        relative = str(assertion.get("path", ""))
        exists = (workspace / relative).is_file()
        passed = exists if kind == "file_exists" else not exists
        return passed, f"{relative} {'exists' if exists else 'does not exist'}"
    if kind == "file_contains":
        relative = str(assertion.get("path", ""))
        expected = str(assertion.get("value", ""))
        path = workspace / relative
        content = path.read_text(encoding="utf-8") if path.is_file() else ""
        passed = expected in content
        return passed, f"{relative} {'contains' if passed else 'does not contain'} {expected!r}"
    if kind == "task_field":
        field = str(assertion.get("field", ""))
        actual = nested(task, field)
        expected = assertion.get("equals")
        return actual == expected, f"task.{field} is {actual!r}; expected {expected!r}"
    if kind in {"max_tool_calls", "max_provider_calls", "max_duration_ms", "max_total_tokens"}:
        maximum = int(assertion.get("value", 0))
        field_map = {
            "max_tool_calls": (task.get("toolCalls"), "tool calls"),
            "max_provider_calls": (task.get("providerCalls"), "provider calls"),
            "max_duration_ms": (result.get("durationMs"), "duration ms"),
            "max_total_tokens": (task.get("totalTokens"), "total tokens"),
        }
        actual, label = field_map[kind]
        passed = isinstance(actual, (int, float)) and actual <= maximum
        return passed, f"{label} is {actual!r}; maximum {maximum}"
    return False, f"unknown assertion type: {kind!r}"


def write_report(run: dict, output: Path) -> None:
    lines = [
        f"# {run['suiteName']}",
        "",
        f"- Run: `{run['runId']}`",
        f"- Model override: `{run.get('model') or 'suite default'}`",
        f"- Score: **{run['score']:.1f}%**",
        f"- Cases passed: **{run['passedCases']} / {run['totalCases']}**",
        f"- Tool calls: **{run['totals']['toolCalls']}**",
        f"- Provider calls: **{run['totals']['providerCalls']}**",
        f"- Tokens: **{run['totals']['totalTokens']}**",
        f"- Elapsed: **{run['totals']['durationMs'] / 1000.0:.1f}s**",
        "",
        "| Case | Result | Score | Tools | Provider calls | Tokens | Time |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for case in run["cases"]:
        task = case.get("details", {}).get("task", {})
        lines.append(
            f"| {case['title']} | {'PASS' if case['passed'] else 'FAIL'} | {case['score']:.0f}% | "
            f"{task.get('toolCalls', 0)} | {task.get('providerCalls', 0)} | {task.get('totalTokens', 0)} | "
            f"{case.get('durationMs', 0) / 1000.0:.1f}s |"
        )
    lines.extend(["", "## Assertions", ""])
    for case in run["cases"]:
        lines.append(f"### {case['title']}")
        lines.extend(f"- [{'x' if item['passed'] else ' '}] {item['message']}" for item in case["assertions"])
        if case.get("error"):
            lines.append(f"- Error: {case['error']}")
        lines.append("")
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")


def run_suite(args) -> int:
    suite_path = args.suite.resolve()
    suite, errors = validate_suite(suite_path)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 2
    api = AgentApi(args.discovery)
    status = api.call("GET", "/v1/status")
    if status.get("status") != "ready":
        raise RuntimeError("FrustIDE Agent API is not ready")

    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "-" + safe_name(args.model or "default")
    output = (args.output or suite_path.parent / "runs" / run_id).resolve()
    output.mkdir(parents=True, exist_ok=False)
    defaults = suite.get("defaults", {})
    case_results = []

    selected_cases = [case for case in suite["cases"] if not args.case or case["id"] == args.case]
    if not selected_cases:
        raise ValueError(f"Unknown case id: {args.case}")

    for case in selected_cases:
        case_output = output / safe_name(case["id"])
        workspace = case_output / "workspace"
        case_output.mkdir(parents=True)
        shutil.copytree(suite_path.parent / case["fixture"], workspace)
        before = file_snapshot(workspace)
        session = dict(defaults.get("session", {}))
        session.update(case.get("session", {}))
        session.update({"projectRoot": str(workspace), "newConversation": True})
        if args.model:
            session["model"] = args.model
        timeout = int(case.get("timeoutSeconds", defaults.get("timeoutSeconds", 900)))
        record = {"id": case["id"], "title": case.get("title", case["id"]), "assertions": []}
        try:
            configured = api.submit_and_wait("/v1/session", session, 30)
            if configured.get("status") != "completed":
                raise RuntimeError(configured.get("error", "Session configuration failed"))
            result = api.submit_and_wait("/v1/messages", {"content": case["prompt"]}, timeout)
            record.update(result)
            for assertion in case.get("assertions", []):
                passed, message = evaluate_assertion(assertion, result, workspace)
                record["assertions"].append({"passed": passed, "message": message})
            if case.get("expectNoChanges"):
                unchanged = before == file_snapshot(workspace)
                record["assertions"].append({"passed": unchanged, "message": "workspace remained unchanged"})
        except Exception as exc:  # preserve the rest of a costly suite when one case fails
            record["error"] = str(exc)
            record["assertions"].append({"passed": False, "message": str(exc)})
        passed_count = sum(1 for item in record["assertions"] if item["passed"])
        assertion_count = len(record["assertions"])
        record["score"] = 100.0 * passed_count / assertion_count if assertion_count else 0.0
        record["passed"] = assertion_count > 0 and passed_count == assertion_count and record.get("status") == "completed"
        case_results.append(record)
        (case_output / "result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
        time.sleep(float(defaults.get("betweenCasesSeconds", 2)))

    totals = {"toolCalls": 0, "providerCalls": 0, "totalTokens": 0, "durationMs": 0.0}
    assertion_total = assertion_passed = 0
    for case in case_results:
        task = case.get("details", {}).get("task", {})
        for key in ("toolCalls", "providerCalls", "totalTokens"):
            totals[key] += int(task.get(key, 0) or 0)
        totals["durationMs"] += float(case.get("durationMs", 0.0) or 0.0)
        assertion_total += len(case["assertions"])
        assertion_passed += sum(1 for item in case["assertions"] if item["passed"])
    run = {
        "schema": "frustide-agent-eval-run",
        "schemaVersion": 1,
        "runId": run_id,
        "suiteName": suite.get("name", suite_path.stem),
        "model": args.model,
        "score": 100.0 * assertion_passed / assertion_total if assertion_total else 0.0,
        "passedCases": sum(1 for case in case_results if case["passed"]),
        "totalCases": len(case_results),
        "totals": totals,
        "cases": case_results,
    }
    (output / "run.json").write_text(json.dumps(run, indent=2), encoding="utf-8")
    write_report(run, output)
    print(f"{run['passedCases']}/{run['totalCases']} cases passed; score {run['score']:.1f}%")
    print(output / "report.md")
    return 0 if run["passedCases"] == run["totalCases"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    validate = subcommands.add_parser("validate", help="validate a suite without contacting the IDE")
    validate.add_argument("suite", type=Path)
    run = subcommands.add_parser("run", help="run a suite through the active FrustIDE")
    run.add_argument("suite", type=Path)
    run.add_argument("--model", help="override the suite model with an exact model ID")
    run.add_argument("--case", help="run only the case with this exact id")
    run.add_argument("--output", type=Path, help="write this run to a specific new directory")
    run.add_argument(
        "--discovery",
        type=Path,
        default=Path(os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming")))
        / "LagDaemonResearchIDE" / "agent-api.json",
    )
    args = parser.parse_args()
    if args.command == "validate":
        _, errors = validate_suite(args.suite.resolve())
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 2
        print(f"Suite is valid: {args.suite}")
        return 0
    return run_suite(args)


if __name__ == "__main__":
    raise SystemExit(main())
