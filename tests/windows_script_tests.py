import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = Path(os.environ["SystemRoot"]) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
PIN = "a" * 40


class WindowsScripts(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build" / "_scratch"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="studio-script-test-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        tools = self.root / "tools"
        tools.mkdir()
        (self.root / "cmake").mkdir()
        for name in ("setup.ps1", "build.ps1"):
            shutil.copy2(ROOT / name, self.root / name)
        (self.root / "cmake" / "WindowsToolchain.ps1").write_text(
            "function Initialize-StudioWindowsToolchain { }\n", encoding="utf-8"
        )
        (tools / "cmake.cmd").write_text(
            '@echo off\n'
            'echo cmake %* >>"%TFLST_TEST_TRACE%"\n'
            'if "%1"=="--preset" if "%TFLST_TEST_FAIL%"=="configure" exit /b 41\n'
            'if "%1"=="--build" if "%TFLST_TEST_FAIL%"=="build" exit /b 43\n'
            'exit /b 0\n', encoding="ascii"
        )
        (tools / "ninja.cmd").write_text("@exit /b 0\n", encoding="ascii")
        (tools / "winget.cmd").write_text(
            '@echo INSTALL>>"%TFLST_TEST_TRACE%"\n@exit /b 99\n', encoding="ascii"
        )
        (tools / "git.cmd").write_text(
            '@echo off\n'
            'echo git %*>>"%TFLST_TEST_TRACE%"\n'
            f'if "%1"=="ls-files" echo 160000 {PIN} 0 submodule\n'
            f'if "%3"=="rev-parse" if "%TFLST_TEST_OUTDATED%"=="" echo {PIN}\n'
            'if "%3"=="rev-parse" if not "%TFLST_TEST_OUTDATED%"=="" echo bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n'
            'if "%3"=="status" if not "%TFLST_TEST_DIRTY%"=="" echo M source.cpp\n'
            'exit /b 0\n', encoding="ascii"
        )
        truffle = self.root / "external" / "truffle"
        for path in (truffle, self.root / "external" / "SDL", self.root / "external" / "occt"):
            path.mkdir(parents=True)
            (path / ".git").touch()
        for name in ("freetype", "harfbuzz", "sheenbidi", "utf8proc", "vulkan-headers", "volk", "glslang"):
            path = truffle / "vendor" / name
            path.mkdir(parents=True)
            (path / ".git").touch()
        self.trace = self.root / "trace.txt"
        self.env = dict(os.environ, PATH=str(tools), TFLST_TEST_TRACE=str(self.trace))

    def run_script(self, name, *args):
        return subprocess.run(
            [str(POWERSHELL), "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(self.root / name), *args],
            cwd=self.root / "tools", env=self.env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )

    def calls(self):
        return self.trace.read_text() if self.trace.exists() else ""

    def test_build_help_needs_no_tools(self):
        result = self.run_script("build.ps1", "--help")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(self.calls(), "")

    def test_build_preserves_arguments(self):
        result = self.run_script("build.ps1", "release", "-WarningsAsErrors", "on", "--parallel", "2")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("--preset release -DTFLST_WARNINGS_AS_ERRORS=ON", self.calls())
        self.assertIn("--build --preset release --parallel 2", self.calls(), result.stdout)

    def test_build_keeps_cached_warning_policy(self):
        result = self.run_script("build.ps1", "debug")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("WARNINGS_AS_ERRORS", self.calls())

    def test_build_forwards_arguments_without_warning_option(self):
        result = self.run_script("build.ps1", "debug", "--parallel", "4")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("--build --preset debug --parallel 4", self.calls())
        self.assertNotIn("WARNINGS_AS_ERRORS", self.calls())

    def test_configure_failure_prevents_build(self):
        self.env["TFLST_TEST_FAIL"] = "configure"
        self.assertEqual(self.run_script("build.ps1").returncode, 41)
        self.assertNotIn("--build", self.calls())

    def test_build_failure_propagates(self):
        self.env["TFLST_TEST_FAIL"] = "build"
        self.assertEqual(self.run_script("build.ps1").returncode, 43)

    def test_setup_check_is_read_only(self):
        result = self.run_script("setup.ps1", "-Check")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("submodule update", self.calls())
        self.assertNotIn("INSTALL", self.calls())

    def test_setup_check_rejects_outdated_pins(self):
        self.env["TFLST_TEST_OUTDATED"] = "1"
        self.assertNotEqual(self.run_script("setup.ps1", "-Check").returncode, 0)
        self.assertNotIn("submodule update", self.calls())

    def test_setup_preserves_dirty_submodule(self):
        self.env.update(TFLST_TEST_OUTDATED="1", TFLST_TEST_DIRTY="1")
        result = self.run_script("setup.ps1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Refusing to move dirty", " ".join(result.stdout.split()))
        self.assertNotIn("submodule update", self.calls())

    def test_setup_only_updates_named_pins(self):
        self.env["TFLST_TEST_OUTDATED"] = "1"
        result = self.run_script("setup.ps1")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("submodule update --init --", self.calls())
        self.assertNotIn("--recursive", self.calls())


if __name__ == "__main__":
    unittest.main()
