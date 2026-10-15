"""Release behavior at the command and GitHub API boundaries."""

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.parse import parse_qs, unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release", ROOT / "scripts/release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class GitHubService:
    """In-memory GitHub HTTP boundary; public assets remain observable after each run."""

    def __init__(self):
        self.release = None
        self.assets = {}
        self.upload_failure = None
        self.publications = []
        self.mutations = []
        self.tag_object = {"type": "commit", "sha": "a" * 40}
        self.corrupt_upload = False
        self.release_error = None

    def urlopen(self, request, **kwargs):
        path = unquote(urlsplit(request.full_url).path)
        method = request.get_method()
        if method != "GET":
            self.mutations.append((method, path))
        if "/git/ref/tags/" in path:
            result = {"object": self.tag_object}
        elif "/git/tags/" in path:
            result = {"object": {"type": "commit", "sha": "a" * 40}}
        elif "/releases/tags/" in path:
            if self.release_error:
                raise HTTPError(request.full_url, self.release_error, "API failure", {}, None)
            if self.release is None or self.release["draft"]:
                raise HTTPError(request.full_url, 404, "Not Found", {}, None)
            result = self.release
        elif path.endswith("/releases") and method == "GET":
            result = [self.release] if self.release else []
        elif path.endswith("/releases") and method == "POST":
            self.release = {"id": 7, "upload_url": "https://uploads.github.com/repos/owner/repo/releases/7/assets{?name,label}",
                            **json.loads(request.data)}
            result = self.release
        elif path.endswith("/releases/7") and method == "PATCH":
            self.release.update(json.loads(request.data))
            if not self.release["draft"]:
                self.publications.append(set(self.assets))
            result = self.release
        elif path.endswith("/releases/7/assets") and method == "GET":
            result = [{"id": i, "name": name, "state": "uploaded", "size": len(data)}
                      for i, (name, data) in enumerate(self.assets.items(), 1)]
            page = int(parse_qs(urlsplit(request.full_url).query)["page"][0])
            result = result[(page - 1) * 100:page * 100]
        elif path.endswith("/releases/7/assets") and method == "POST":
            name = parse_qs(urlsplit(request.full_url).query)["name"][0]
            if name == self.upload_failure:
                raise HTTPError(request.full_url, 503, "Unavailable", {}, None)
            if name in self.assets:
                raise AssertionError("Publisher tried to overwrite an existing asset")
            self.assets[name] = request.data.read()
            if self.corrupt_upload:
                self.assets[name] = b"X" * len(self.assets[name])
            result = {"name": name}
        elif "/releases/assets/" in path:
            index = int(path.rsplit("/", 1)[1]) - 1
            return io.BytesIO(list(self.assets.values())[index])
        else:
            raise AssertionError(f"Unexpected GitHub request: {method} {path}")
        return io.BytesIO(json.dumps(result).encode())


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.packages = Path(self.directory.name)
        self.github = GitHubService()
        self.tag = "v1.2.3-rc.1"
        for platform, name in [("windows", "QuickFlacRecorder-windows-x64.zip"),
                               ("macos", "QuickFlacRecorder-macos-universal.zip"),
                               ("linux", "QuickFlacRecorder-linux-x64.tar.gz")]:
            folder = self.packages / platform
            folder.mkdir()
            package = folder / name
            package.write_bytes(f"portable {platform}".encode())
            package.with_name(name + ".json").write_text(json.dumps({
                "tag": "v1.2.3-rc.1", "commit": "a" * 40, "run_id": "42",
                "version": "1.2.3-rc.1", "package": name,
                "sha256": hashlib.sha256(package.read_bytes()).hexdigest()}), encoding="utf-8")

    def publish(self):
        arguments = ["release.py", "publish", "--repository", "owner/repo", "--tag", self.tag,
                     "--commit", "a" * 40, "--run-id", "42", "--packages", str(self.packages)]
        with patch.object(sys, "argv", arguments), patch.dict("os.environ", {"GH_TOKEN": "test-token"}), \
                patch("urllib.request.urlopen", self.github.urlopen), redirect_stdout(io.StringIO()), \
                redirect_stderr(io.StringIO()):
            release.main()

    def test_new_release_is_public_only_with_every_verified_package(self):
        self.publish()
        self.assertFalse(self.github.release["draft"])
        self.assertTrue(self.github.release["prerelease"])
        self.assertEqual(self.github.publications, [{
            "QuickFlacRecorder-windows-x64.zip", "QuickFlacRecorder-macos-universal.zip",
            "QuickFlacRecorder-linux-x64.tar.gz"}])

    def test_interrupted_upload_stays_draft_and_rerun_fills_missing_downloads(self):
        self.github.upload_failure = "QuickFlacRecorder-macos-universal.zip"
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertTrue(self.github.release["draft"])
        self.assertEqual(self.github.publications, [])
        windows = self.github.assets["QuickFlacRecorder-windows-x64.zip"]
        self.github.upload_failure = None
        self.publish()
        self.assertFalse(self.github.release["draft"])
        self.assertEqual(self.github.assets["QuickFlacRecorder-windows-x64.zip"], windows)
        self.assertEqual(self.github.mutations.count(("POST", "/repos/owner/repo/releases")), 1)

    def test_matching_published_downloads_are_preserved_without_mutations(self):
        self.publish()
        before = dict(self.github.assets)
        self.github.mutations.clear()
        self.publish()
        self.assertEqual(self.github.assets, before)
        self.assertEqual(self.github.mutations, [])

    def test_missing_public_download_is_added_and_other_assets_are_preserved(self):
        self.publish()
        del self.github.assets["QuickFlacRecorder-macos-universal.zip"]
        self.github.assets["owner-notes.txt"] = b"preserve this extra asset"
        before = dict(self.github.assets)
        self.publish()
        self.assertEqual(len(self.github.assets), 4)
        for name, data in before.items():
            self.assertEqual(self.github.assets[name], data)

    def test_changed_public_download_fails_before_uploading_missing_assets(self):
        self.publish()
        name = "QuickFlacRecorder-windows-x64.zip"
        self.github.assets[name] = b"X" * len(self.github.assets[name])
        del self.github.assets["QuickFlacRecorder-macos-universal.zip"]
        before = dict(self.github.assets)
        self.github.mutations.clear()
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.assets, before)
        self.assertEqual(self.github.mutations, [])

    def test_missing_platform_cannot_create_a_release(self):
        (self.packages / "linux" / "QuickFlacRecorder-linux-x64.tar.gz").unlink()
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_wrong_run_tag_source_version_or_hash_cannot_publish(self):
        path = self.packages / "windows" / "QuickFlacRecorder-windows-x64.zip.json"
        original = json.loads(path.read_text())
        for key, value in [("run_id", "41"), ("tag", "v1.2.3"), ("commit", "b" * 40),
                           ("version", "1.2.3"), ("sha256", "0" * 64)]:
            with self.subTest(key=key):
                path.write_text(json.dumps({**original, key: value}))
                with self.assertRaises(SystemExit):
                    self.publish()
                self.assertEqual(self.github.mutations, [])

    def test_annotated_tag_resolves_to_the_built_commit(self):
        self.github.tag_object = {"type": "tag", "sha": "c" * 40}
        self.publish()
        self.assertFalse(self.github.release["draft"])

    def test_moved_tag_cannot_publish(self):
        self.github.tag_object = {"type": "commit", "sha": "b" * 40}
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_duplicate_platform_package_cannot_publish(self):
        (self.packages / "QuickFlacRecorder-windows-x64.zip").write_bytes(b"another build")
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_existing_prerelease_status_cannot_be_changed_silently(self):
        self.publish()
        self.github.release["prerelease"] = False
        self.github.mutations.clear()
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_stable_tag_publishes_a_regular_release(self):
        self.tag = "v1.2.3"
        for path in self.packages.rglob("*.json"):
            manifest = json.loads(path.read_text())
            manifest.update(tag=self.tag, version="1.2.3")
            path.write_text(json.dumps(manifest))
        self.publish()
        self.assertFalse(self.github.release["prerelease"])
        self.assertFalse(self.github.release["draft"])

    def test_api_permission_failure_does_not_create_another_release(self):
        self.github.release_error = 403
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_corrupt_upload_cannot_make_a_draft_public(self):
        self.github.corrupt_upload = True
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertTrue(self.github.release["draft"])
        self.assertEqual(self.github.publications, [])

    def test_existing_assets_are_verified_beyond_the_first_page(self):
        self.publish()
        self.github.assets = {**{f"notes-{i}": b"owner notes" for i in range(101)}, **self.github.assets}
        self.github.mutations.clear()
        self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_draft_notes_hashes_describe_the_archives_actually_published(self):
        self.github.upload_failure = "QuickFlacRecorder-windows-x64.zip"
        with self.assertRaises(SystemExit):
            self.publish()
        self.assertEqual(self.github.assets, {})
        for package in self.packages.rglob("*.zip"):
            package.write_bytes(b"rebuilt portable application")
            path = package.with_name(package.name + ".json")
            manifest = json.loads(path.read_text())
            manifest["sha256"] = hashlib.sha256(package.read_bytes()).hexdigest()
            path.write_text(json.dumps(manifest))
        self.github.upload_failure = None
        self.publish()
        for name, data in self.github.assets.items():
            self.assertIn(f"{hashlib.sha256(data).hexdigest()}  {name}", self.github.release["body"])


class VersionTests(unittest.TestCase):
    def version(self, tag):
        return subprocess.run(
            [sys.executable, str(ROOT / "scripts/release.py"), "version", "--tag", tag],
            capture_output=True, text=True)

    def test_stable_tag_maps_to_application_version(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/release.py"), "version", "--tag", "v1.2.3"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), {
            "version": "1.2.3", "numeric_version": "1.2.3", "prerelease": False})

    def test_prerelease_retains_its_identity(self):
        result = self.version("v1.2.3-rc.1+build.7")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), {
            "version": "1.2.3-rc.1+build.7", "numeric_version": "1.2.3", "prerelease": True})

    def test_invalid_or_unrepresentable_tags_fail(self):
        for tag in ["vtest", "v1.2", "1.2.3", "v01.2.3", "v1.2.3-01", "v1.2.3-",
                    "v65536.2.3", "v1.2.3\n", "v1.2.3/evil", "v1.2.3+", "v1.2.3;echo"]:
            with self.subTest(tag=tag):
                result = self.version(tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")

    def test_github_outputs_preserve_version_and_release_type(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "outputs"
            result = subprocess.run([sys.executable, str(ROOT / "scripts/release.py"), "version",
                                     "--tag", "v1.2.3-rc.1", "--github-output", str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_text(), "version=1.2.3-rc.1\nnumeric_version=1.2.3\nprerelease=true\n")


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/release.py", self.root / "scripts/release.py")
        subprocess.run(["git", "init", "--quiet", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "config", "core.autocrlf", "false"], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "scripts/release.py"], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Test", "-c", "user.email=test@example.com",
                        "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "Source snapshot"], check=True)
        self.commit = subprocess.check_output(["git", "-C", str(self.root), "rev-parse", "HEAD"], text=True).strip()
        self.package = self.root / "QuickFlacRecorder-windows-x64.zip"
        self.package.write_bytes(b"abc")
        (self.root / "build").mkdir()
        (self.root / "build/CMakeCache.txt").write_text("QFR_VERSION:STRING=1.2.3-rc.1\n")

    def manifest(self, commit=None, tag="v1.2.3-rc.1"):
        return subprocess.run([sys.executable, str(self.root / "scripts/release.py"), "manifest",
                               "--tag", tag, "--commit", commit or self.commit, "--run-id", "42",
                               "--build", str(self.root / "build"), "--package", str(self.package)],
                              capture_output=True, text=True)

    def test_manifest_identifies_configured_version_source_run_and_actual_package_bytes(self):
        result = self.manifest()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(self.package.with_suffix(".zip.json").read_text())
        self.assertEqual(manifest, {"tag": "v1.2.3-rc.1", "version": "1.2.3-rc.1", "commit": self.commit,
                                    "run_id": "42", "package": "QuickFlacRecorder-windows-x64.zip",
                                    "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"})

    def test_wrong_source_or_configured_version_cannot_generate_a_manifest(self):
        self.assertNotEqual(self.manifest(commit="b" * 40).returncode, 0)
        self.assertNotEqual(self.manifest(tag="v1.2.3").returncode, 0)
        self.assertFalse(self.package.with_suffix(".zip.json").exists())


if __name__ == "__main__":
    unittest.main()
