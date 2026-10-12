import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
# A minimal x86 PE header: setup.ps1 checks MZ, the PE signature, and I386.
X86_PE = b'MZ' + bytes(0x3a) + struct.pack('<I', 0x40) + b'PE' + bytes(2) + struct.pack('<H', 0x14c) + bytes(2)
def stage_inputs(base):
    source = base/'source'; build = base/'build'; dest = base/'dest'
    maps = source/'Assets'/'assets'/'maps'; maps.mkdir(parents=True); build.mkdir()
    for name in ['Game.exe', 'msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll']:
        (source/name).write_bytes(X86_PE)
    for name in ['ctr2shim.dll', 'd3dcompiler_47.dll']:
        (build/name).write_bytes(X86_PE + b'shim')
    # Raw .appx extraction keeps OPC percent-encoded names.
    (maps/'%21_MasterGameDesignDocument.xml').write_bytes(b'design')
    (source/'Assets'/'a%20b').mkdir(); (source/'Assets'/'a%20b'/'c.txt').write_bytes(b'c')
    return source, build, dest
@unittest.skipUnless(shutil.which('pwsh'), 'needs PowerShell 7')
class PowerShellSetupTests(unittest.TestCase):
    def run_setup(self, source, dest, build):
        return subprocess.run(['pwsh', '-NoProfile', '-NonInteractive', '-File', str(ROOT/'scripts'/'setup.ps1'),
                               '-SourceDirectory', str(source), '-Destination', str(dest), '-BuildDirectory', str(build)],
                              capture_output=True)
    def test_decodes_asset_names_and_restages(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, dest = stage_inputs(pathlib.Path(temp))
            result = self.run_setup(source, dest, build)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual((dest/'Assets'/'assets'/'maps'/'!_MasterGameDesignDocument.xml').read_bytes(), b'design')
            self.assertFalse((dest/'Assets'/'assets'/'maps'/'%21_MasterGameDesignDocument.xml').exists())
            self.assertEqual((dest/'Assets'/'a b'/'c.txt').read_bytes(), b'c')
            self.assertIn(b'SaveLocation=game', (dest/'ctr2shim.ini').read_bytes())
            (dest/'ctr2shim.ini').write_bytes(b'[storage]\r\nSaveLocation=documents\r\n')
            # Re-staging replaces the shim DLLs in place, decodes again, and keeps settings.
            (build/'ctr2shim.dll').write_bytes(X86_PE + b'new shim')
            result = self.run_setup(source, dest, build)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual((dest/'ctr2shim.dll').read_bytes(), X86_PE + b'new shim')
            self.assertFalse((dest/'Assets'/'assets'/'maps'/'%21_MasterGameDesignDocument.xml').exists())
            self.assertIn(b'SaveLocation=documents', (dest/'ctr2shim.ini').read_bytes())
@unittest.skipIf(sys.platform == 'win32', 'setup.sh stages the game for Wine on macOS')
class SetupTests(unittest.TestCase):
    def test_decodes_asset_names(self):
        with tempfile.TemporaryDirectory() as temp:
            source, build, dest = stage_inputs(pathlib.Path(temp))
            result = subprocess.run(['bash', str(ROOT/'scripts/setup.sh'), str(source), str(dest), str(build)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual((dest/'Assets'/'assets'/'maps'/'!_MasterGameDesignDocument.xml').read_bytes(), b'design')
            self.assertFalse((dest/'Assets'/'assets'/'maps'/'%21_MasterGameDesignDocument.xml').exists())
            self.assertEqual((dest/'Assets'/'a b'/'c.txt').read_bytes(), b'c')
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
