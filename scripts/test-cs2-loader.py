"""Exercise install/restore safety against isolated fixtures; never launch a game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOADER = None
PWSH = None


class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / '.local' / ('loader-fixture-' + uuid.uuid4().hex)
        self.game = self.root / 'game' / 'bin' / 'win64'
        self.game.mkdir(parents=True)
        (self.game / 'cs2.exe').write_bytes(b'Nonexecuted fixture only')
        self.target = self.game / 'dxgi.dll'
        self.candidate = self.root / 'candidate'; self.candidate.mkdir()
        addon = self.candidate / 'fixture.addon64'; addon.write_bytes(b'Own fixture')
        (self.candidate / 'install-plan.json').write_text(json.dumps({
            'Executable': str(self.game / 'cs2.exe'), 'CandidateAddon': str(addon),
            'CandidateSha256': hashlib.sha256(addon.read_bytes()).hexdigest()}))
        (self.candidate / 'ReShade.ini').write_text('[GENERAL]\n')
        self.backup = self.root / 'before.zip'
        self.write_backup(self.game)
        self.state = self.root / 'state.json'

    def write_backup(self, source):
        with zipfile.ZipFile(self.backup, 'w') as archive:
            archive.writestr('_um_manifest.json', json.dumps({'source': str(source), 'files': {}}))

    def command(self, mode, *extra, error=None):
        result = subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File',
            str(ROOT / 'scripts/manage-cs2-loader.ps1'), '-Mode', mode,
            '-Cs2Root', str(self.root), '-Candidate', str(self.candidate),
            '-Loader', str(LOADER), '-BackupSnapshot', str(self.backup),
            '-StateFile', str(self.state), *extra], capture_output=True, text=True,
            encoding='utf8', errors='replace', timeout=15)
        if error:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(error, result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_preview_has_no_game_writes(self):
        self.command('Preview')
        self.assertFalse(self.target.exists()); self.assertFalse(self.state.exists())

    def test_install_conflict_and_restore_owned_file(self):
        self.command('Install')
        self.assertEqual(self.target.read_bytes(), LOADER.read_bytes())
        self.command('Install', error='Existing dxgi.dll')
        self.assertEqual(self.target.read_bytes(), LOADER.read_bytes())
        self.command('Restore')
        self.assertFalse(self.target.exists())
        self.assertEqual(json.loads(self.state.read_text())['Mode'], 'Restored')

    def test_changed_loader_is_not_removed(self):
        self.command('Install'); self.target.write_bytes(b'External change')
        self.command('Restore', error='Target changed')
        self.assertEqual(self.target.read_bytes(), b'External change')

    def test_wrong_backup_source_is_refused(self):
        self.write_backup(self.root / 'other')
        self.command('Install', error='not the pre-loader snapshot')
        self.assertFalse(self.target.exists())

    def test_changed_state_cannot_remove_another_file(self):
        self.command('Install')
        other = self.root / 'keep-me.dll'; other.write_bytes(b'Own test marker')
        state = json.loads(self.state.read_text()); state['Target'] = str(other)
        self.state.write_text(json.dumps(state))
        self.command('Restore', error='does not authorize this exact target')
        self.assertEqual(other.read_bytes(), b'Own test marker')
        self.assertEqual(self.target.read_bytes(), LOADER.read_bytes())

    def preview_launch(self, probe=False, map_name='de_dust2'):
        # Only invoke the launcher's default preview; never pass -Launch.
        def quote(value):
            return "'" + str(value).replace("'", "''") + "'"
        script = self.root / 'preview.ps1'
        script.write_text("$ErrorActionPreference='Stop'\n& " +
            quote(ROOT / 'scripts/launch-cs2-lab.ps1') + ' -StateFile ' + quote(self.state) +
            ' -Map ' + quote(map_name) + (' -HostProbe' if probe else '') +
            ' | ConvertTo-Json -Depth 5\n', encoding='utf8')
        return subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File', str(script)],
            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=15)

    def test_host_probe_is_opt_in_and_preview_only(self):
        self.command('Install')
        for enabled in (False, True):
            result = self.preview_launch(probe=enabled)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan['Mode'], 'Preview')
            self.assertEqual('-countercraft-host-probe' in plan['Arguments'], enabled)
            self.assertIn('-insecure', plan['Arguments'])
            self.assertIn('-countercraft-lab', plan['Arguments'])
            self.assertEqual(plan['Environment']['RESHADE_BASE_PATH_OVERRIDE'], str(self.candidate))
        self.assertFalse(list(self.candidate.glob('cs2-console-*')))

    def test_launch_preview_refuses_restored_state(self):
        self.command('Install'); self.command('Restore')
        result = self.preview_launch(probe=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Loader state must be Installed', result.stderr)

    def test_launch_preview_refuses_map_arguments(self):
        self.command('Install')
        result = self.preview_launch(map_name='de_dust2;+connect')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Map', result.stderr)
        self.assertFalse(list(self.candidate.glob('cs2-console-*')))

    def test_prepare_missing_game_writes_only_candidate(self):
        missing_game = self.root / 'absent-game'
        native = self.root / 'native'; native.mkdir()
        (native / 'CounterCraftProbe.addon64').write_bytes(b'Own nonexecuted preparation fixture')
        destination = self.root / 'prepared'
        args = [PWSH, '-NoLogo', '-NoProfile', '-File',
            str(ROOT / 'scripts/prepare-cs2-lab.ps1'), '-Cs2Root', str(missing_game),
            '-NativeBuild', str(native), '-Destination', str(destination)]
        refused = subprocess.run(args, capture_output=True, text=True,
            encoding='utf8', errors='replace', timeout=15)
        self.assertNotEqual(refused.returncode, 0)
        self.assertFalse(destination.exists())
        prepared = subprocess.run([*args, '-AllowMissingGame'], capture_output=True,
            text=True, encoding='utf8', errors='replace', timeout=15)
        self.assertEqual(prepared.returncode, 0, prepared.stderr)
        plan = json.loads(prepared.stdout)
        self.assertFalse(plan['GameFilesWritten'])
        self.assertFalse(plan['GameExecutablePresent'])
        self.assertEqual(plan['OptionalHostProbeArgument'], '-countercraft-host-probe')
        self.assertTrue((destination / 'CounterCraftProbe.addon64').is_file())
        self.assertFalse(missing_game.exists())

    def test_prepare_refuses_candidate_inside_missing_game(self):
        missing_game = self.root / 'absent-game'
        result = subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File',
            str(ROOT / 'scripts/prepare-cs2-lab.ps1'), '-Cs2Root', str(missing_game),
            '-Destination', str(missing_game / 'candidate'), '-AllowMissingGame'],
            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=15)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('outside the game install', result.stderr)
        self.assertFalse(missing_game.exists())

    # Fixtures remain under .local as failure evidence; no recursive cleanup/game changes.


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--loader', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    LOADER = args.loader.resolve(); PWSH = shutil.which('pwsh')
    if not PWSH: parser.error('PowerShell 7.4+ required')
    unittest.main(argv=[__file__, *remaining])
