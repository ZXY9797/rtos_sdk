from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prune_cmake_helpers import reduce
from sdk_artifacts import git_identity
from source_audit import candidates
import source_audit

ROOT = Path(__file__).resolve().parents[3]


class SourceSubsetTest(unittest.TestCase):
    def test_comments_do_not_retain_unrelated_cmake_functions(self):
        source = ('function(used)\n helper()\nendfunction()\n'
                  'function(helper)\nendfunction()\n'
                  '# unused() is only a documentation example\n'
                  'function(unused)\nendfunction()\n')
        output, names = reduce(source, '# unused()\nused()\n')
        self.assertEqual(names, ['helper', 'used'])
        self.assertNotIn('function(unused)', output)

    def test_source_tree_obeys_the_explicit_retention_policy(self):
        policy = json.loads((ROOT / 'tools/source-policy.json').read_text(encoding='utf-8'))
        self.assertEqual(candidates(policy), [])

    def test_vendor_pruning_rules_are_rejected(self):
        policy = json.loads((ROOT / 'tools/source-policy.json').read_text(encoding='utf-8'))
        for directory in ('embedded/modules/cmsis/CMSIS', 'embedded/osal',
                          'component/ble/goodix/libraries'):
            with self.subTest(directory=directory):
                invalid = dict(policy, retention_groups=[{'root': directory, 'keep': []}])
                with self.assertRaisesRegex(ValueError, 'protected vendor sources'):
                    candidates(invalid)

    def test_prune_cannot_bypass_vendor_protection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            vendor = root / 'vendor/source.c'
            vendor.parent.mkdir()
            vendor.write_text('/* preserved upstream source */\n', encoding='utf-8')
            policy = root / 'policy.json'
            policy.write_text(json.dumps({'protected_roots': ['vendor']}), encoding='utf-8')
            backup = root / 'out/removal.zip'
            with patch.object(source_audit, 'ROOT', root), patch.object(source_audit, 'POLICY', policy):
                with self.assertRaisesRegex(ValueError, 'protected vendor source'):
                    source_audit.prune([{'path': 'vendor/source.c'}], backup)
            self.assertTrue(vendor.exists())
            self.assertFalse(backup.exists())

    @unittest.skipUnless(shutil.which('git'), 'Git required')
    def test_source_identity_tracks_modification_deletion_and_application_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / 'app'
            app.mkdir()
            (app / 'source.cc').write_text('int value = 1;\n', encoding='utf-8')
            (root / 'vendor.bin').write_bytes(b'\x00original vendor source')
            def git(*args):
                subprocess.run(['git', '-C', str(root), *args], check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            git('init')
            git('add', '.')
            git('-c', 'user.name=SDK Test', '-c', 'user.email=sdk-test@example.invalid',
                'commit', '--no-verify', '-m', 'test fixture')
            original = git_identity(root)
            app_original = git_identity(app)
            self.assertFalse(original['dirty'])
            (root / 'vendor.bin').unlink()
            deleted = git_identity(root)
            self.assertTrue(deleted['dirty'])
            self.assertNotEqual(original['changes_sha256'], deleted['changes_sha256'])
            self.assertEqual(app_original, git_identity(app))
            (app / 'source.cc').write_text('int value = 2;\n', encoding='utf-8')
            first = git_identity(root)
            (app / 'source.cc').write_text('int value = 3;\n', encoding='utf-8')
            second = git_identity(root)
            self.assertNotEqual(first['changes_sha256'], second['changes_sha256'])
            self.assertNotEqual(app_original, git_identity(app))


if __name__ == '__main__':
    unittest.main()
