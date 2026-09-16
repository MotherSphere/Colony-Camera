"""Fetch exact public dependency commits; never overwrite an existing checkout."""
import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
for name, spec in json.loads((root / "dependencies.json").read_text()).items():
    path = root / "deps" / name
    if not path.exists():
        subprocess.run(["git", "clone", "--filter=blob:none", "--no-checkout", spec["url"], str(path)], check=True)
        subprocess.run(["git", "-C", str(path), "checkout", "--detach", spec["revision"]], check=True)
    revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if revision != spec["revision"]:
        raise SystemExit(f"{name}: unexpected revision {revision}; expected {spec['revision']}")
    if subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True).strip():
        raise SystemExit(f"{name}: tracked modifications; refusing a non-reproducible dependency")
    print(name, revision)
