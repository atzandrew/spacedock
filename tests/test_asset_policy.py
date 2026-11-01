import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_assets import check


class AssetPolicyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'assets').mkdir()
        (self.root / 'assets/MANIFEST.csv').write_text('path,origin,license\n')

    def test_empty_original_asset_set(self):
        self.assertEqual(check(self.root), [])

    def test_unlisted_asset(self):
        (self.root / 'assets/image.png').write_bytes(b'image')
        self.assertTrue(any('Unlisted' in e for e in check(self.root)))

    def test_hash_scan_includes_files_outside_assets(self):
        (self.root / 'capture.bin').write_bytes(b'private recording')
        deny = self.root / 'hashes.txt'
        deny.write_text(hashlib.sha256(b'private recording').hexdigest())
        self.assertTrue(any('Private reference' in e for e in check(self.root, denylist=deny)))

    def test_reference_directory(self):
        reference = self.root / 'reference'
        reference.mkdir()
        (reference / 'private.png').write_bytes(b'private')
        (self.root / 'copy.png').write_bytes(b'private')
        self.assertTrue(any('copy.png' in e for e in check(self.root, reference)))

    def test_ignored_references_are_private_working_material(self):
        reference = self.root / 'references'
        reference.mkdir()
        (reference / 'private.png').write_bytes(b'private')
        self.assertEqual(check(self.root, reference), [])
        (self.root / 'copy.png').write_bytes(b'private')
        self.assertTrue(any('copy.png' in e for e in check(self.root, reference)))

    def test_manifest_cannot_escape_assets(self):
        (self.root / 'assets/MANIFEST.csv').write_text('path,origin,license\nassets/../secret,original,CC0\n')
        self.assertTrue(any('Invalid asset path' in e for e in check(self.root)))


if __name__ == '__main__':
    unittest.main()
