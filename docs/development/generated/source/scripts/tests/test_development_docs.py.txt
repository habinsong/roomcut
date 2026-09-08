"""임시 Git 저장소에서 전체 원문 갱신과 누락 검출을 검증합니다."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "update-development-docs.py"


class DevelopmentDocsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        (self.root / "scripts").mkdir()
        shutil.copyfile(SCRIPT, self.root / "scripts/update-development-docs.py")

    def run_docs(self, *args, expected=0):
        result = subprocess.run([sys.executable, "scripts/update-development-docs.py", *args],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_complete_bytes_and_untracked_files(self):
        (self.root / "source.swift").write_bytes("// 전체 원문\r\nlet x = 42".encode())
        (self.root / "image.bin").write_bytes(b"\0\xff\x01")
        self.run_docs()
        self.run_docs("--check")
        archive = self.root / "docs/development/generated/source/source.swift.txt"
        self.assertEqual(archive.read_bytes(), (self.root / "source.swift").read_bytes())
        manifest = json.loads((self.root / "docs/development/generated/manifest.json").read_text())
        self.assertEqual({record["path"] for record in manifest["files"]},
                         {"source.swift", "image.bin", "scripts/update-development-docs.py"})

    def test_detects_edits_additions_deletions_and_archive_damage(self):
        source = self.root / "source.swift"
        source.write_text("let x = 1\n")
        subprocess.run(["git", "add", "source.swift"], cwd=self.root, check=True)
        self.run_docs()
        source.write_text("let x = 2\n")
        self.run_docs("--check", expected=1)
        self.run_docs()
        archive = self.root / "docs/development/generated/source/source.swift.txt"
        archive.write_text("손상된 보관본")
        self.run_docs("--check", expected=1)
        self.run_docs()
        added = self.root / "new.cpp"
        added.write_text("int main() {}\n")
        self.run_docs("--check", expected=1)
        self.run_docs()
        source.unlink()
        self.run_docs("--check", expected=1)
        self.run_docs()
        self.assertFalse(archive.exists())
        self.run_docs("--check")

    def test_ignored_artifacts_and_links_are_not_collected(self):
        (self.root / ".gitignore").write_text("build/\n")
        (self.root / "build").mkdir()
        (self.root / "build/artifact.txt").write_text("빌드 결과")
        self.run_docs()
        manifest = json.loads((self.root / "docs/development/generated/manifest.json").read_text())
        self.assertNotIn("build/artifact.txt", {record["path"] for record in manifest["files"]})
        (self.root / "linked.txt").symlink_to(self.root / "build/artifact.txt")
        self.run_docs("--check", expected=1)

    def test_generated_directory_link_is_rejected(self):
        (self.root / "docs/development").mkdir(parents=True)
        (self.root / "outside").mkdir()
        marker = self.root / "outside/keep.txt"
        marker.write_text("보존")
        (self.root / "docs/development/generated").symlink_to(self.root / "outside", target_is_directory=True)
        self.run_docs(expected=1)
        self.assertEqual(marker.read_text(), "보존")

    def test_handwritten_file_in_generated_folder_is_preserved(self):
        self.run_docs()
        handwritten = self.root / "docs/development/generated/personal-note.md"
        handwritten.write_text("직접 작성한 내용")
        self.run_docs("--check", expected=1)
        self.run_docs(expected=1)
        self.assertEqual(handwritten.read_text(), "직접 작성한 내용")


if __name__ == "__main__":
    unittest.main()
