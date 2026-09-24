import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "projects" / "frust-ide-agent" / "ENGINEER_TOOLS.json"
OUTPUT = ROOT / "projects" / "frust-ide-agent" / "ENGINEER_TOOL_CARDS.jsonl"


def build_cards():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    cards = []
    for tool in catalog["tools"]:
        text = "\n".join(
            [
                f"Tool: {tool['name']}",
                f"Purpose: {tool['description']}",
                f"When to use: {tool['usage']}",
                f"Minimum access: {tool['access'].upper()}.",
                "Arguments schema: "
                + json.dumps(tool["parameters"], sort_keys=True, separators=(",", ":")),
            ]
        )
        card_id = "frust-ide.tool." + tool["name"].replace("_", ".") + "#usage"
        cards.append(
            {
                "id": card_id,
                "kind": "tool",
                "title": f"Use {tool['name']}",
                "text": text,
                "tokens": sorted(set(tool.get("tokens", []) + [tool["name"], "tool"])),
                "access": tool["access"],
                "contentHash": "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest(),
                "source": str(CATALOG.relative_to(ROOT)).replace("\\", "/"),
            }
        )
    return cards


if __name__ == "__main__":
    cards = build_cards()
    OUTPUT.write_text(
        "".join(json.dumps(card, sort_keys=True) + "\n" for card in cards),
        encoding="utf-8",
    )
    print(json.dumps({"cards": len(cards), "output": str(OUTPUT)}, indent=2))
