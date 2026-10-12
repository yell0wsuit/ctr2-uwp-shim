import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
class SetupTests(unittest.TestCase):
    def test_rejects_destination_inside_source_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp); source = base/'source'; source.mkdir(); build = base/'build'; build.mkdir()
            for name in ['Game.exe', 'msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll']:
                (source/name).write_bytes(b'original')
            for name in ['ctr2shim.dll', 'd3dcompiler_47.dll']:
                (build/name).write_bytes(b'shim')
            destination = source/'nested'
            result = subprocess.run(['bash', str(ROOT/'scripts/setup.sh'), str(source), str(destination), str(build)], capture_output=True)
            self.assertNotEqual(result.returncode, 0, 'overlapping stage must be rejected')
            self.assertFalse(destination.exists(), 'validation must run before any write into source')
    def test_rejects_destination_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); source=base/'source'; source.mkdir(); build=base/'build'; build.mkdir(); dest=base/'dest'; dest.mkdir()
            for name in ['Game.exe', 'msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll']:
                (source/name).write_bytes(b'original')
            for name in ['ctr2shim.dll', 'd3dcompiler_47.dll']:
                (build/name).write_bytes(b'shim')
            unrelated=base/'unrelated'; unrelated.write_bytes(b'preserve'); (dest/'Game.exe').symlink_to(unrelated)
            result=subprocess.run(['bash',str(ROOT/'scripts/setup.sh'),str(source),str(dest),str(build)],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(unrelated.read_bytes(),b'preserve')

    def test_replaces_dll_without_mutating_existing_mapped_inode(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); source=base/'source'; source.mkdir(); build=base/'build'; build.mkdir(); dest=base/'dest'; dest.mkdir()
            for name in ['Game.exe','msvcr120_app.dll','msvcp120_app.dll','vccorlib120_app.dll']:
                (source/name).write_bytes(b'original')
            for name in ['ctr2shim.dll','d3dcompiler_47.dll']:
                (build/name).write_bytes(b'new shim')
                (dest/name).write_bytes(b'old mapped shim')
            with (dest/'ctr2shim.dll').open('rb') as mapped:
                result=subprocess.run(['bash',str(ROOT/'scripts/setup.sh'),str(source),str(dest),str(build)],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr.decode())
                self.assertEqual(mapped.read(),b'old mapped shim')
            self.assertEqual((dest/'ctr2shim.dll').read_bytes(),b'new shim')
            self.assertEqual((source/'Game.exe').read_bytes(),(dest/'Game.exe').read_bytes())
