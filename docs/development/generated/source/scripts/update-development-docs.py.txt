#!/usr/bin/env python3
"""구현 목록과 원문을 생성하거나 현재 작업 트리와의 일치를 검사합니다."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = Path("docs/development/generated")
PRIVATE_DIRS = {".git", ".ssh", ".aws", ".kube", ".codex", ".config"}
PRIVATE_SUFFIXES = {".pem", ".key", ".p12", ".pfx", ".cer", ".mobileprovision"}
PRIVATE_NAMES = {"id_rsa", "id_ed25519", "roomcut.local.json", "settings.local.json"}


def excluded(path):
    if path == OUTPUT or OUTPUT in path.parents:
        return True
    return any(part in PRIVATE_DIRS or part == ".env" or part.startswith(".env.")
               for part in path.parts) or path.name in PRIVATE_NAMES or path.suffix in PRIVATE_SUFFIXES


def source_paths():
    # Include working-tree edits and untracked implementation, not only HEAD.
    data = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT)
    names = {Path(os.fsdecode(name)) for name in data.split(b"\0") if name}
    if (ROOT / "AGENTS.md").exists():
        names.add(Path("AGENTS.md"))
    paths = []
    for path in sorted(names):
        if excluded(path):
            continue
        absolute = ROOT / path
        # Never follow a link into credentials or another checkout.
        if any(parent.is_symlink() for parent in [absolute, *absolute.parents]):
            raise ValueError(f"심볼릭 링크는 자동 수집하지 않습니다: {path}")
        if not absolute.exists():
            continue  # A tracked deletion is absent from the current implementation.
        if not absolute.is_file():
            raise ValueError(f"일반 파일이 아닙니다: {path}")
        paths.append(path)
    return paths


def link(label, source, destination):
    relative = os.path.relpath(destination, source.parent)
    return f"[{label}]({quote(relative, safe='/.-_')})"


def render():
    artifacts = {}
    records = []
    folders = {}
    for path in source_paths():
        data = (ROOT / path).read_bytes()
        try:
            content = data.decode("utf-8")
            is_text = "\0" not in content
        except UnicodeDecodeError:
            is_text = False
        record = {"path": path.as_posix(), "bytes": len(data),
                  "sha256": hashlib.sha256(data).hexdigest(),
                  "kind": "text" if is_text else "binary"}
        if is_text:
            archive = OUTPUT / "source" / (path.as_posix() + ".txt")
            artifacts[archive] = data  # Preserve every byte, including the final newline.
            record.update(lines=len(data.splitlines()), archive=archive.as_posix())
        records.append(record)
        folders.setdefault(path.parent.as_posix(), []).append(record)

    index = OUTPUT / "README.md"
    text_count = sum(record["kind"] == "text" for record in records)
    lines = ["# 현재 구현 전체 목록", "",
             "`python3 scripts/update-development-docs.py`로 생성합니다. 직접 수정하지 마세요.", "",
             f"총 {len(records)}개 파일: 텍스트 원문 {text_count}개, 바이너리 {len(records) - text_count}개.",
             f"텍스트 총 {sum(record.get('lines', 0) for record in records)}행.", "",
             "추적 파일과 Git에서 무시하지 않는 미추적 파일의 **현재 작업 트리**를 수집합니다.",
             "로컬 `AGENTS.md`도 포함합니다. 원문은 `source/`에 바이트 그대로 보존합니다.",
             "바이너리는 원본 링크·바이트 수·SHA-256으로 기록하며 중복 복사하지 않습니다.",
             "빌드·캐시 등 Git 무시 파일, 비밀 파일, 생성 문서 자신은 수집 대상이 아닙니다.",
             "수동 개발 문서와 검증 자료는 포함하며, 심볼릭 링크는 오류로 보고합니다.", "",
             "`--check`는 파일 추가·수정·삭제와 생성 원문의 변조·누락을 검사합니다.",
             "이 목록은 파일의 존재와 원문 일치를 보장하는 도구이며 동작 검증 결과가 아닙니다.", "",
             "| 폴더 | 파일 수 |", "|---|---:|"]
    for folder, entries in sorted(folders.items()):
        page = OUTPUT / "inventory" / folder / "index.md"
        lines.append(f"| {link(folder, index, page)} | {len(entries)} |")
        body = [f"# `{folder}`", "", "| 현재 파일 | 전체 원문 | 행 | 바이트 | SHA-256 |",
                "|---|---|---:|---:|---|"]
        for record in entries:
            path = Path(record["path"])
            archive = (link("원문", page, Path(record["archive"]))
                       if "archive" in record else "바이너리 원본 참조")
            body.append(f"| {link(path.name, page, path)} | {archive} | "
                        f"{record.get('lines', '—')} | {record['bytes']} | `{record['sha256']}` |")
        artifacts[page] = ("\n".join(body) + "\n").encode()
    artifacts[index] = ("\n".join(lines) + "\n").encode()
    artifacts[OUTPUT / "manifest.json"] = (json.dumps(
        {"schema_version": 1, "files": records}, ensure_ascii=False, indent=2) + "\n").encode()
    ownership = OUTPUT / "owned-files.json"
    artifacts[ownership] = (json.dumps(sorted(path.as_posix() for path in [*artifacts, ownership]),
                                     ensure_ascii=False, indent=2) + "\n").encode()
    return artifacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="파일을 쓰지 않고 최신 상태인지 검사")
    args = parser.parse_args()
    if any((ROOT / parent).is_symlink() for parent in [OUTPUT, *OUTPUT.parents]):
        raise ValueError("생성 폴더의 심볼릭 링크는 허용하지 않습니다")
    artifacts = render()
    existing = set()
    if (ROOT / OUTPUT).exists():
        for directory, subdirs, files in os.walk(ROOT / OUTPUT):
            for name in [*subdirs, *files]:
                if (Path(directory) / name).is_symlink():
                    raise ValueError("생성 폴더의 심볼릭 링크는 허용하지 않습니다")
            existing.update((Path(directory) / name).relative_to(ROOT) for name in files)
    ownership = ROOT / OUTPUT / "owned-files.json"
    owned = {Path(path) for path in json.loads(ownership.read_text())} if ownership.exists() else set()
    if any(path.is_absolute() or ".." in path.parts or OUTPUT not in path.parents for path in owned):
        raise ValueError("생성 파일 소유 목록에 잘못된 경로가 있습니다")
    unknown = existing - owned - artifacts.keys()
    if unknown:
        raise ValueError("직접 작성한 파일은 보존합니다. 생성 폴더 밖으로 옮겨주세요: " +
                         ", ".join(str(path) for path in sorted(unknown)))
    stale = sorted((existing & owned) - artifacts.keys())
    changed = [path for path, data in artifacts.items()
               if not (ROOT / path).exists() or (ROOT / path).read_bytes() != data]
    if args.check:
        for path in changed:
            print(f"갱신 필요: {path}")
        for path in stale:
            print(f"현재 구현에서 제거됨: {path}")
        if changed or stale:
            return 1
        print(f"전체 구현 문서 일치: {len(artifacts)}개 생성 파일")
        return 0
    # Only files owned by this generator are removed, never sources or handwritten docs.
    for path in stale:
        (ROOT / path).unlink()
    for path in changed:
        (ROOT / path).parent.mkdir(parents=True, exist_ok=True)
        (ROOT / path).write_bytes(artifacts[path])
    print(f"구현 문서 갱신: {len(changed)}개, 이전 생성 파일 제거: {len(stale)}개")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
