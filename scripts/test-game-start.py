"""OS-isolated launcher tests: no game launch, focus, game write or process kill."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PWSH = shutil.which("pwsh")


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(PWSH, "PowerShell 7.4 required")
class LauncherTests(unittest.TestCase):
    def run_script(self, root, text):
        script = root / "fixture.ps1"; script.write_text(text, encoding="utf8")
        return subprocess.run([PWSH, "-NoProfile", "-File", str(script)], capture_output=True,
                              text=True, encoding="utf8", errors="replace", timeout=15)

    def test_repository_and_ancestry_identity_guards(self):
        with tempfile.TemporaryDirectory() as name:
            text = r'''
$ErrorActionPreference='Stop'
. HELPER
$script:entries=@{
  10=[pscustomobject]@{ProcessId=10;ParentProcessId=9;ExecutablePath='C:\Java\bin\java.exe';CommandLine='-Dcountercraft.enabled=true -cp C:\CounterCraft\minecraft\client.jar net.fabricmc.devlaunchinjector.Main'}
  9=[pscustomobject]@{ProcessId=9;ParentProcessId=8}
  8=[pscustomobject]@{ProcessId=8;ParentProcessId=0}
}
function Get-CimInstance { param($ClassName,$Filter); $script:entries[[int]($Filter.Split('=')[1])] }
function Require($value) { if(-not $value){throw 'Identity guard failed'} }
Require (Test-RepositoryGuest 10 'C:\Java\bin\java.exe' 'C:\CounterCraft')
Require (-not (Test-RepositoryGuest 10 'C:\Other\java.exe' 'C:\CounterCraft'))
Require (-not (Test-RepositoryGuest 10 'C:\Java\bin\java.exe' 'C:\Counter'))
Require (Test-GuestDescendant 10 8)
Require (-not (Test-GuestDescendant 10 7))
$script:entries[9].ParentProcessId=10
Require (-not (Test-GuestDescendant 10 7))
$script:entries[10].CommandLine='-Dcountercraft.enabled=trueX -cp C:\CounterCraft\minecraft\client.jar net.fabricmc.devlaunchinjector.Main'
Require (-not (Test-RepositoryGuest 10 'C:\Java\bin\java.exe' 'C:\CounterCraft'))
'passed'
'''.replace("HELPER", quote(ROOT / "game/guest-process.ps1"))
            result = self.run_script(Path(name), text)
            self.assertEqual(result.returncode, 0, result.stderr)

    def fixture(self, root):
        shutil.copytree(ROOT / "game", root / "game")
        (root / "minecraft/run/saves/Lab").mkdir(parents=True)
        for name in ("java/bin/java.exe", "gradle.bat", "backup.zip"):
            path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"fixture")
        config = {"Cs2Root": str(root / "fake-game"), "Gradle": str(root / "gradle.bat"),
                  "JavaHome": str(root / "java"), "BackupSnapshot": str(root / "backup.zip"), "World": "Lab"}
        path = root / "config.json"; path.write_text(json.dumps(config), encoding="utf8")
        return path

    def test_preview_has_no_launch_or_session_files(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); config = self.fixture(root)
            result = self.run_script(root, "& " + quote(root / "game/start.ps1") + " -ConfigPath "
                                     + quote(config) + " -SessionDirectory " + quote(root / "session") + " | ConvertTo-Json")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(json.loads(result.stdout)["GameFilesWritten"])
            self.assertFalse((root / "session").exists())

    def test_unverified_existing_guest_is_preserved_and_refused(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); config = self.fixture(root)
            text = ("$ErrorActionPreference='Stop'\n"
                    "function Get-Process {param($Name,$Id,$ErrorAction)}\n"
                    "function Get-NetTCPConnection {param($LocalPort,$State,$ErrorAction); [pscustomobject]@{OwningProcess=999}}\n"
                    "function Get-CimInstance {param($ClassName,$Filter); [pscustomobject]@{ExecutablePath='C:\\Unrelated\\java.exe';CommandLine='PCL'}}\n"
                    "function Start-Process {throw 'Unexpected process start'}\n"
                    "function Stop-Process {throw 'Unexpected process stop'}\n"
                    "& " + quote(root / "game/start.ps1") + " -Launch -ConfigPath " + quote(config)
                    + " -SessionDirectory " + quote(root / "session"))
            result = self.run_script(root, text)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unverified client", result.stderr)
            receipt = json.loads((root / "session/guest.json").read_text(encoding="utf-8-sig"))
            self.assertFalse(receipt["Owned"]); self.assertEqual(receipt["ProcessId"], 0)
            self.assertFalse((root / "session/loader-state.json").exists())


if __name__ == "__main__": unittest.main()
