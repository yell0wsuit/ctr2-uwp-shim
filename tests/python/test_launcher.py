import os
import sys
import json
import hashlib
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipIf(sys.platform == "win32", "Wine launcher uses Bash")
class LauncherTests(unittest.TestCase):
    def run_launcher(self, renderer, ambient="", corrupt=False):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp)
            game = base / "game"
            game.mkdir()
            for name in (
                "Game.exe",
                "ctr2shim.dll",
                "wined3d.dll",
                "d3d11.dll",
                "dxgi.dll",
            ):
                (game / name).touch()
            (game / "wine-graphics.json").write_text(
                json.dumps(
                    {
                        "sha256": {
                            name: hashlib.sha256((game / name).read_bytes()).hexdigest()
                            for name in ("wined3d.dll", "d3d11.dll", "dxgi.dll")
                        }
                    }
                )
            )
            if corrupt:
                (game / "d3d11.dll").write_bytes(b"mismatched")
            wine = base / "bin" / "wine"
            wine.parent.mkdir()
            wine.write_text(
                "#!/bin/bash\n"
                'if [[ "$1" == Game.exe ]]; then\n'
                '  cat "$WINEPREFIX/commands"\n'
                '  echo "OVERRIDES=$WINEDLLOVERRIDES"\n'
                'else\n  printf "%s\\n" "$@" >> "$WINEPREFIX/commands"\nfi\n'
            )
            wine.chmod(0o755)
            (base / "prefix").mkdir()
            (base / "prefix" / ".ctr2-retina").write_text("0")
            env = dict(
                os.environ,
                WINE=str(wine),
                WINEPREFIX=str(base / "prefix"),
                CTR2_RETINA="0",
                WINEDLLOVERRIDES=ambient,
            )
            return subprocess.run(
                [
                    "bash",
                    str(ROOT / "scripts/run.sh"),
                    str(game),
                    "--renderer",
                    renderer,
                ],
                env=env,
                capture_output=True,
                text=True,
            )

    def test_vulkan_uses_stock_wine_and_app_specific_renderer(self):
        result = self.run_launcher("vulkan")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("wined3d,d3d11,dxgi=b", result.stdout)
        self.assertIn("AppDefaults\\Game.exe\\Direct3D", result.stdout)
        self.assertIn("vulkan", result.stdout)
        self.assertNotIn("wined3d=n", result.stdout)

    def test_opengl_ignores_stale_private_wined3d(self):
        result = self.run_launcher("opengl", ambient="wined3d=b;d3d11,dxgi=b")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("d3d11,dxgi,wined3d=n", result.stdout)
        self.assertIn("gl", result.stdout)

    def test_invalid_renderer_fails_before_wine_runs(self):
        for renderer in ("typo", "metal", "legacy"):
            with self.subTest(renderer=renderer):
                result = self.run_launcher(renderer)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Unknown renderer", result.stderr)
                self.assertNotIn("OVERRIDES=", result.stdout)

    def test_mismatched_stack_fails_before_game_launch(self):
        result = self.run_launcher("opengl", corrupt=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)
        self.assertNotIn("OVERRIDES=", result.stdout)
