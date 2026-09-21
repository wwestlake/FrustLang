import os
import sqlite3
import re
import json
import subprocess
from pathlib import Path

DB_PATH = Path(__file__).parent / "frust_knowledge.db"
REPO_ROOT = Path(__file__).parent.parent.parent
WIKI_DIR = REPO_ROOT / "wiki" / "reference"
PROJECTS_DIR = REPO_ROOT / "projects"
AGENT_CONTEXT = REPO_ROOT / "projects" / "frust-ide-agent" / "FRUST_AI_CONTEXT.md"
SPEC_FILE = REPO_ROOT / "projects" / "01_language_paradigms" / "02_functional" / "FRUST_LANG_SPEC.md"
ENGINEER_TOOL_CARDS = REPO_ROOT / "projects" / "frust-ide-agent" / "ENGINEER_TOOL_CARDS.jsonl"
GRAMMAR_FILES = [
    REPO_ROOT / "projects" / "01_language_paradigms" / "02_functional" / "grammar" / "frust.y",
    REPO_ROOT / "projects" / "01_language_paradigms" / "02_functional" / "grammar" / "frust.l",
]
INCLUDE_UNTRACKED_PODS = os.environ.get("FRUST_RAG_INCLUDE_UNTRACKED") == "1"

def init_db(cursor):
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS nodes (
            id TEXT PRIMARY KEY,
            type TEXT NOT NULL,
            name TEXT NOT NULL,
            content TEXT,
            source_file TEXT
        )
    ''')
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS edges (
            source_id TEXT,
            target_id TEXT,
            relation TEXT,
            PRIMARY KEY (source_id, target_id, relation),
            FOREIGN KEY(source_id) REFERENCES nodes(id),
            FOREIGN KEY(target_id) REFERENCES nodes(id)
        )
    ''')

def clear_db(cursor):
    cursor.execute("DELETE FROM edges")
    cursor.execute("DELETE FROM nodes")

def upsert_node(cursor, node_id, ntype, name, content, source):
    cursor.execute('''
        INSERT INTO nodes (id, type, name, content, source_file)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            type=excluded.type,
            name=excluded.name,
            content=excluded.content,
            source_file=excluded.source_file
    ''', (node_id, ntype, name, content, source))

def upsert_edge(cursor, source_id, target_id, relation):
    cursor.execute('''
        INSERT OR IGNORE INTO edges (source_id, target_id, relation)
        VALUES (?, ?, ?)
    ''', (source_id, target_id, relation))

def parse_wiki(cursor):
    if not WIKI_DIR.exists():
        print(f"Warning: Wiki dir not found at {WIKI_DIR}")
        return

    for md_file in WIKI_DIR.glob("*.md"):
        content = md_file.read_text(encoding="utf-8")
        node_id = f"chapter:{md_file.stem}"
        upsert_node(cursor, node_id, "CHAPTER", md_file.stem, content, str(md_file.relative_to(REPO_ROOT)))
        print(f"Ingested Chapter: {md_file.stem}")

def parse_authoritative_docs(cursor):
    docs = [
        ("brief:frust-ai-context", "AGENT_BRIEF", "FRUST_AI_CONTEXT", AGENT_CONTEXT),
        ("spec:frust-lang", "SPEC", "FRUST_LANG_SPEC", SPEC_FILE),
    ]

    for node_id, ntype, name, path in docs:
        if path.exists():
            upsert_node(cursor, node_id, ntype, name, path.read_text(encoding="utf-8"), str(path.relative_to(REPO_ROOT)))
            print(f"Ingested {ntype}: {name}")
        else:
            print(f"Warning: {name} not found at {path}")

    for path in GRAMMAR_FILES:
        if path.exists():
            name = path.name
            node_id = f"grammar:{name}"
            upsert_node(cursor, node_id, "GRAMMAR", name, path.read_text(encoding="utf-8"), str(path.relative_to(REPO_ROOT)))
            upsert_edge(cursor, node_id, "spec:frust-lang", "IMPLEMENTS")
            print(f"Ingested Grammar: {name}")
        else:
            print(f"Warning: grammar file not found at {path}")

def parse_engineer_tool_cards(cursor):
    if not ENGINEER_TOOL_CARDS.exists():
        print(f"Warning: Engineer tool cards not found at {ENGINEER_TOOL_CARDS}")
        return

    for line in ENGINEER_TOOL_CARDS.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        card = json.loads(line)
        upsert_node(
            cursor,
            card["id"],
            "TOOL",
            card["title"],
            card["text"],
            card["source"],
        )
        print(f"Ingested Tool: {card['title']}")

def parse_pods(cursor):
    if not PROJECTS_DIR.exists():
        return

    frate_files = sorted(PROJECTS_DIR.rglob("frate.json"))
    if not INCLUDE_UNTRACKED_PODS:
        try:
            tracked = subprocess.check_output(
                ["git", "ls-files", "--", "projects/**/frate.json"],
                cwd=REPO_ROOT,
                text=True,
            ).splitlines()
            tracked_paths = {REPO_ROOT / path for path in tracked}
            frate_files = [path for path in frate_files if path in tracked_paths]
        except Exception as e:
            print(f"Warning: could not filter pods to tracked files only: {e}")

    for frate_file in frate_files:
        pod_dir = frate_file.parent
        try:
            manifest = json.loads(frate_file.read_text(encoding="utf-8"))
            pod_name = manifest.get("name", pod_dir.name)
            pod_id = f"pod:{pod_name}"
            upsert_node(cursor, pod_id, "POD", pod_name, json.dumps(manifest, indent=2), str(frate_file.relative_to(REPO_ROOT)))
            print(f"Ingested Pod: {pod_name}")
            
            # Parse source files
            src_dir = pod_dir / "src"
            if src_dir.exists():
                for fr_file in src_dir.rglob("*.fr"):
                    parse_frust_file(cursor, fr_file, pod_id)
        except Exception as e:
            print(f"Failed to parse {frate_file}: {e}")

def parse_frust_file(cursor, fr_file, pod_id):
    content = fr_file.read_text(encoding="utf-8")
    rel_path = str(fr_file.relative_to(REPO_ROOT))
    
    # Very basic regex parsing for structs and functions
    struct_pattern = re.compile(r"struct\s+([A-Za-z0-9_]+)\s*\{([^}]*)\}")
    fn_pattern = re.compile(r"pub\s+fn\s+([A-Za-z0-9_]+)\s*\(([^)]*)\)\s*(?:->\s*([A-Za-z0-9_:]+))?")

    for match in struct_pattern.finditer(content):
        name = match.group(1)
        body = match.group(0)
        node_id = f"struct:{name}"
        upsert_node(cursor, node_id, "STRUCT", name, body, rel_path)
        upsert_edge(cursor, node_id, pod_id, "DEFINED_IN")

    for match in fn_pattern.finditer(content):
        name = match.group(1)
        body = match.group(0) # Just the signature for now
        node_id = f"fn:{name}"
        upsert_node(cursor, node_id, "FUNCTION", name, body, rel_path)
        upsert_edge(cursor, node_id, pod_id, "DEFINED_IN")

def main():
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    print("Initializing LiteSemRAG Database...")
    init_db(cursor)
    clear_db(cursor)
    
    parse_authoritative_docs(cursor)
    parse_engineer_tool_cards(cursor)
    parse_wiki(cursor)
    parse_pods(cursor)
    
    conn.commit()
    conn.close()
    print(f"Database built successfully at {DB_PATH}")

if __name__ == "__main__":
    main()
