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


if __name__ == "__main__":
    unittest.main()
