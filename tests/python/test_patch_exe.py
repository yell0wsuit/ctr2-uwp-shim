import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def executable(flags=0x9140):
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', data, 0x84, 0x14C)
    struct.pack_into('<H', data, 0x94, 224)
    struct.pack_into('<H', data, 0x96, 0x102)
    struct.pack_into('<H', data, 0x98, 0x10B)
    struct.pack_into('<H', data, 0xDE, flags)
    return bytes(data)


@unittest.skipUnless(shutil.which('powershell') or shutil.which('pwsh'),
                     'needs Windows PowerShell or PowerShell Core')
class PowerShellPatchExeTests(unittest.TestCase):
    def run_patch(self, source):
        shell = shutil.which('powershell') or shutil.which('pwsh')
        return subprocess.run([shell, '-NoProfile', '-NonInteractive',
                               '-ExecutionPolicy', 'Bypass', '-File',
                               str(ROOT / 'scripts' / 'patch_exe.ps1'),
                               '-Exe', str(source)], capture_output=True, text=True)

    def test_changes_only_appcontainer_bit_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / 'Game.exe'
            original = executable()
            source.write_bytes(original)
            result = self.run_patch(source)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(source.read_bytes(), original)
            patched = source.with_name('Game.desktop.exe').read_bytes()
            expected = bytearray(original)
            expected[0xDF] ^= 0x10
            self.assertEqual(patched, expected)

    def test_refuses_to_overwrite_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / 'Game.exe'
            source.write_bytes(executable())
            output = source.with_name('Game.desktop.exe')
            output.write_bytes(b'preserve')
            result = self.run_patch(source)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b'preserve')
            self.assertIn('already exists', result.stderr)

    def test_rejects_invalid_unsupported_and_truncated_headers(self):
        cases = [b'not a PE', executable()[:0xDF]]
        for offset, value in [(0, 0), (0x80, 0), (0x84, 0x8664),
                              (0x94, 2), (0x96, 0x2102), (0x98, 0x20B),
                              (0x3C, 0xFFFF)]:
            data = bytearray(executable())
            struct.pack_into('<H', data, offset, value)
            cases.append(bytes(data))
        for data in cases:
            with self.subTest(data=data[:4]), tempfile.TemporaryDirectory() as folder:
                source = pathlib.Path(folder) / 'Game.exe'
                source.write_bytes(data)
                result = self.run_patch(source)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(source.read_bytes(), data)
                self.assertFalse(source.with_name('Game.desktop.exe').exists())

    def test_already_desktop_executable_needs_no_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            source = pathlib.Path(folder) / 'Game.exe'
            source.write_bytes(executable(0x8140))
            result = self.run_patch(source)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('already clear', result.stdout)
            self.assertFalse(source.with_name('Game.desktop.exe').exists())


    def test_runtime_mode_preserves_source_and_patches_all_four_images(self):
        with tempfile.TemporaryDirectory() as folder:
            base = pathlib.Path(folder)
            originals = {'Game.exe': executable()}
            for name in ['msvcr120_app.dll', 'msvcp120_app.dll', 'vccorlib120_app.dll']:
                data = bytearray(executable(0x1140))
                struct.pack_into('<H', data, 0x96, 0x2102)
                originals[name] = bytes(data)
            for name, data in originals.items():
                (base / name).write_bytes(data)
            for name in ['ctr2shim.dll', 'd3dcompiler_47.dll']:
                (base / name).write_bytes(b'shim')
            (base / 'Assets').mkdir()
            (base / 'Assets' / 'asset.txt').write_bytes(b'asset')
            (base / 'SaveData').mkdir()
            (base / 'SaveData' / 'save.txt').write_bytes(b'save')
            (base / 'Game.exe.c').write_bytes(b'exclude decompile')
            shell = shutil.which('powershell') or shutil.which('pwsh')
            command = [shell, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                       '-File', str(ROOT / 'scripts' / 'patch_exe.ps1'),
                       '-Exe', str(base / 'Game.exe'), '-IncludeRuntime']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name, data in originals.items():
                self.assertEqual((base / name).read_bytes(), data)
                expected = bytearray(data)
                expected[0xDF] &= 0xEF
                target = 'Game.desktop.exe' if name == 'Game.exe' else name
                self.assertEqual((base / 'desktop' / target).read_bytes(), expected)
            self.assertEqual((base / 'desktop' / 'Assets' / 'asset.txt').read_bytes(), b'asset')
            self.assertEqual((base / 'desktop' / 'SaveData' / 'save.txt').read_bytes(), b'save')
            self.assertFalse((base / 'desktop' / 'Game.exe.c').exists())
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('already exists', result.stderr)
