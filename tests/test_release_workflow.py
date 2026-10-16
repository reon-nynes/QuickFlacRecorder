"""Check the Actions event, permission, and artifact contracts in issue #15."""

from pathlib import Path
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[1]


class WorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # BaseLoader preserves the Actions key `on` as text rather than YAML 1.1 true.
        cls.workflow = yaml.load((ROOT / ".github/workflows/preview.yml").read_text(), Loader=yaml.BaseLoader)
        cls.jobs = cls.workflow["jobs"]

    def test_preview_events_and_all_v_tags_still_trigger(self):
        events = self.workflow["on"]
        self.assertEqual(events["push"]["tags"], ["v*"])
        self.assertIn("main", events["push"]["branches"])
        self.assertIn("codex/**", events["push"]["branches"])
        self.assertIn("pull_request", events)

    def test_publication_is_only_for_tag_pushes_after_all_checks(self):
        publication = self.jobs["publish"]
        self.assertEqual(publication["if"], "github.event_name == 'push' && github.ref_type == 'tag'")
        self.assertEqual(set(publication["needs"]), {"prepare", "windows", "macos", "linux", "linux-baseline"})
        for platform in ["windows", "macos", "linux"]:
            self.assertEqual(self.jobs[platform]["needs"], "prepare")
        self.assertEqual(self.jobs["linux-baseline"]["needs"], "linux")

    def test_only_publication_has_release_write_permission(self):
        self.assertEqual(self.workflow["permissions"], {"contents": "read"})
        for name, job in self.jobs.items():
            self.assertEqual(job.get("permissions", self.workflow["permissions"]),
                             {"contents": "write" if name == "publish" else "read"})

    def test_publication_downloads_exact_platform_artifacts_from_the_current_run(self):
        downloads = [step["with"] for step in self.jobs["publish"]["steps"]
                     if step.get("uses", "").startswith("actions/download-artifact@")]
        self.assertEqual(downloads, [
            {"name": "QuickFlacRecorder-windows-x64", "path": "dist/windows"},
            {"name": "QuickFlacRecorder-macos-universal", "path": "dist/macos"},
            {"name": "QuickFlacRecorder-linux-x64", "path": "dist/linux"}])

    def test_all_jobs_check_out_the_event_source_without_saved_credentials(self):
        for name, job in self.jobs.items():
            with self.subTest(job=name):
                checkout = next(step for step in job["steps"] if step.get("uses", "").startswith("actions/checkout@"))
                self.assertEqual(checkout["with"], {"ref": "${{ github.sha }}", "persist-credentials": "false"})

    def test_tag_packages_split_support_before_recording_both_manifests(self):
        for platform, archive in [("windows", "QuickFlacRecorder-windows-x64.zip"),
                                  ("macos", "QuickFlacRecorder-macos-universal.zip"),
                                  ("linux", "QuickFlacRecorder-linux-x64.tar.gz")]:
            with self.subTest(platform=platform):
                steps = self.jobs[platform]["steps"]
                step = next(step for step in steps if "package-app" in step.get("run", ""))
                self.assertEqual(step["if"], "github.event_name == 'push' && github.ref_type == 'tag'")
                commands = step["run"].splitlines()
                support = archive.removesuffix(".tar.gz").removesuffix(".zip") + "-support.zip"
                self.assertIn(f"package-app --package dist/{archive}", commands[0])
                self.assertIn(f"manifest --tag", commands[1])
                self.assertTrue(commands[1].endswith(f"--package dist/{archive}"))
                self.assertTrue(commands[2].endswith(f"--package dist/{support}"))
                upload = next(step["with"] for step in steps if step.get("uses", "").startswith("actions/upload-artifact@"))
                self.assertEqual(set(upload["path"].splitlines()),
                                 {f"dist/{name}{suffix}" for name in (archive, support) for suffix in ("", ".json")})


if __name__ == "__main__":
    unittest.main()
