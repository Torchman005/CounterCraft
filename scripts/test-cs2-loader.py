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

def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / '.local' / ('loader-fixture-' + uuid.uuid4().hex)
        self.game_root = self.root / 'game-install'
        self.game = self.game_root / 'game' / 'bin' / 'win64'
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

    def command(self, mode, *extra, error=None, busy=False):
        # Fixture file operations must not depend on the user's running games.
        # Keep the production process guard enabled and test it with a busy mock.
        arguments = {'Mode': mode, 'Cs2Root': self.game_root, 'Candidate': self.candidate,
            'Loader': LOADER, 'BackupSnapshot': self.backup, 'StateFile': self.state}
        self.assertTrue(all(option == '-SteamLaunch' for option in extra))
        argument_text = ';'.join(ps_quote(k) + '=' + ps_quote(v) for k, v in arguments.items())
        if '-SteamLaunch' in extra:
            argument_text += ';SteamLaunch=$true'
        script = self.root / 'manage-fixture.ps1'
        script.write_text("$ErrorActionPreference='Stop'\nfunction Get-Process {\n"
            "    param($Name,$Id,$ErrorAction)\n"
            "    if($Name -eq 'cs2' -and " + ('$true' if busy else '$false') + ") {\n"
            "        [pscustomobject]@{Id=123;ProcessName='cs2'}\n    }\n}\n"
            "$fixtureArguments=@{" + argument_text + "}\n& "
            + ps_quote(ROOT / 'scripts/manage-cs2-loader.ps1') + " @fixtureArguments\n", encoding='utf8')
        result = subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File', str(script)], capture_output=True, text=True,
            encoding='utf8', errors='replace', timeout=15)
        if error:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(error, result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_preview_has_no_game_writes(self):
        self.command('Preview')
        self.assertFalse(self.target.exists()); self.assertFalse(self.state.exists())

    def test_running_cs2_refuses_install_without_game_writes(self):
        self.command('Install', '-SteamLaunch', busy=True, error='Close the existing CS2 process')
        self.assertFalse(self.target.exists())
        self.assertFalse((self.game / 'ReShade.ini').exists())
        self.assertFalse(self.state.exists())

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

    def preview_launch(self, probe=False, map_name='de_dust2', capture=False, camera=False, gameplay=False):
        # Only invoke the launcher's default preview; never pass -Launch.
        def quote(value):
            return "'" + str(value).replace("'", "''") + "'"
        script = self.root / 'preview.ps1'
        script.write_text("$ErrorActionPreference='Stop'\nfunction Get-CimInstance {param($Filter)}\n& " +
            quote(ROOT / 'scripts/launch-cs2-lab.ps1') + ' -StateFile ' + quote(self.state) +
            ' -Map ' + quote(map_name) + (' -HostProbe' if probe else '') +
            (' -DepthCapture' if capture else '') +
            (' -CameraRelay' if camera else '') +
            (' -Gameplay' if gameplay else '') +
            ' | ConvertTo-Json -Depth 5\n', encoding='utf8')
        return subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File', str(script)],
            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=15)

    def test_gameplay_is_explicit_and_refuses_camera_relay(self):
        self.command('Install','-SteamLaunch')
        result=self.preview_launch(gameplay=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('-countercraft-gameplay',json.loads(result.stdout)['Arguments'])
        self.assertNotIn('-countercraft-gameplay',json.loads(self.preview_launch().stdout)['Arguments'])
        mixed=self.preview_launch(gameplay=True,camera=True)
        self.assertNotEqual(mixed.returncode,0)
        self.assertIn('do not combine',mixed.stderr)

    def test_camera_relay_requires_private_calibration_and_is_explicit(self):
        self.command('Install','-SteamLaunch')
        missing=self.preview_launch(camera=True)
        self.assertNotEqual(missing.returncode,0)
        self.assertIn('private calibration',missing.stderr)
        (self.candidate/'camera-layout.json').write_text('{}')
        result=self.preview_launch(camera=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('-countercraft-camera-relay',json.loads(result.stdout)['Arguments'])
        plain=self.preview_launch()
        self.assertNotIn('-countercraft-camera-relay',json.loads(plain.stdout)['Arguments'])

    def test_host_probe_is_opt_in_and_preview_only(self):
        self.command('Install')
        for enabled in (False, True):
            result = self.preview_launch(probe=enabled)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan['Mode'], 'Preview')
            self.assertEqual('-countercraft-host-probe' in plan['Arguments'], enabled)
            self.assertEqual(plan['LaunchRoute'], 'Steam')
            self.assertEqual(plan['SteamArguments'], ['-applaunch', '730', *plan['Arguments']])
            self.assertIn('-insecure', plan['Arguments'])
            self.assertIn('-countercraft-lab', plan['Arguments'])
            self.assertEqual(plan['Candidate'], str(self.candidate))
        self.assertFalse(list(self.candidate.glob('cs2-console-*')))

    def test_depth_capture_is_opt_in_and_implies_probe(self):
        self.command('Install')
        for probe, capture in ((False, False), (True, False), (False, True), (True, True)):
            result = self.preview_launch(probe=probe, capture=capture)
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual('-countercraft-depth-capture' in plan['Arguments'], capture)
            self.assertEqual(plan['Arguments'].count('-countercraft-host-probe'), int(probe or capture))
            self.assertIn('-insecure', plan['Arguments'])
            self.assertEqual(plan['Mode'], 'Preview')
        self.assertFalse((self.candidate / 'captures').exists())

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
        self.assertEqual(plan['LaunchRoute'], 'Steam')
        self.assertEqual(plan['SteamAppId'], 730)
        self.assertEqual({Path(t['Path']).name for t in plan['Targets']}, {'dxgi.dll', 'ReShade.ini'})
        self.assertTrue(plan['SteamBootstrap']['PreparedOnly'])
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

    def test_steam_bootstrap_install_and_restore(self):
        self.command('Install', '-SteamLaunch')
        bootstrap = self.game / 'ReShade.ini'
        self.assertEqual(bootstrap.read_bytes(), ('[INSTALL]\nBasePath=' + str(self.candidate) + '\n').encode())
        state = json.loads(self.state.read_text())
        self.assertEqual(state['BootstrapSha256'], hashlib.sha256(bootstrap.read_bytes()).hexdigest())
        self.assertEqual(state['BootstrapTarget'], str(bootstrap))
        self.assertFalse(state['BootstrapOriginalExisted'])
        preview = self.preview_launch(probe=True)
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertTrue(json.loads(preview.stdout)['BootstrapReady'])
        self.command('Restore')
        self.assertFalse(bootstrap.exists())
        self.assertFalse(self.target.exists())

    def test_modified_bootstrap_preserves_both_files(self):
        self.command('Install', '-SteamLaunch')
        bootstrap = self.game / 'ReShade.ini'
        bootstrap.write_bytes(b'External config')
        self.command('Restore', error='Bootstrap changed')
        self.assertEqual(bootstrap.read_bytes(), b'External config')
        self.assertEqual(self.target.read_bytes(), LOADER.read_bytes())
        self.assertEqual(json.loads(self.state.read_text())['Mode'], 'Installed')

    def test_modified_loader_preserves_bootstrap_too(self):
        self.command('Install', '-SteamLaunch')
        before = (self.game / 'ReShade.ini').read_bytes()
        self.target.write_bytes(b'External loader')
        self.command('Restore', error='Target changed')
        self.assertEqual((self.game / 'ReShade.ini').read_bytes(), before)
        self.assertEqual(self.target.read_bytes(), b'External loader')

    def test_manipulated_bootstrap_target_is_refused(self):
        self.command('Install', '-SteamLaunch')
        other = self.root / 'other.ini'; other.write_bytes(b'Preserve this')
        state = json.loads(self.state.read_text()); state['BootstrapTarget'] = str(other)
        self.state.write_text(json.dumps(state))
        self.command('Restore', error='exact bootstrap')
        self.assertEqual(other.read_bytes(), b'Preserve this')
        self.assertTrue(self.target.exists())
        self.assertTrue((self.game / 'ReShade.ini').exists())

    def test_existing_config_is_never_overwritten(self):
        bootstrap = self.game / 'ReShade.ini'; bootstrap.write_bytes(b'Existing config')
        self.command('Install', '-SteamLaunch', error='Existing game-folder ReShade.ini')
        self.assertEqual(bootstrap.read_bytes(), b'Existing config')
        self.assertFalse(self.target.exists())
        self.assertFalse(self.state.exists())

    def test_missing_owned_files_can_finish_restore(self):
        self.command('Install', '-SteamLaunch')
        # A manual removal or an interrupted restore is completed without removing other files.
        self.target.unlink()
        self.command('Restore')
        self.assertFalse((self.game / 'ReShade.ini').exists())
        self.assertEqual(json.loads(self.state.read_text())['Mode'], 'Restored')

    def mock_steam_launch(self, suffix='', parent=17, exited=False, vanilla=False):
        if not vanilla:
            self.command('Install', '-SteamLaunch')
        def quote(value):
            return "'" + str(value).replace("'", "''") + "'"
        expected = ('-insecure -console +sv_lan 1 +map de_dust2' if vanilla else
            '-insecure -countercraft-lab -countercraft-preview -console +sv_lan 1 +map de_dust2 -countercraft-host-probe')
        command_line = '"' + str(self.game / 'cs2.exe') + '" -steam ' + expected + suffix
        # Shadow all OS process operations. The fixture executables are never executed.
        script = self.root / 'mock-steam-launch.ps1'
        script.write_text(r"""$ErrorActionPreference='Stop'
$script:launched=$false
function Get-CimInstance {
    param($Filter)
    if($Filter -like '*steam.exe*') {
        [pscustomobject]@{ProcessId=17;ExecutablePath='C:\Steam fixture\steam.exe'}
    } elseif($script:launched) {
        [pscustomobject]@{ProcessId=18;ParentProcessId=PARENT;ExecutablePath=GAME_EXE;CommandLine=COMMAND_LINE}
    }
}
function Get-Process {
    param($Name,$Id)
    if($Name){return}
    $owned=[pscustomobject]@{Id=$Id}
    $owned | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {param($ms) return EXITED}
    $owned
}
function Start-Process {
    param($FilePath,$ArgumentList,$WindowStyle)
    if($FilePath -ne 'C:\Steam fixture\steam.exe' -or $ArgumentList[0] -ne '-applaunch' -or $ArgumentList[1] -ne '730') {
        throw 'Attempted a direct or incorrect launch in fixture'
    }
    $script:launched=$true
}
INVOKE_LAUNCHER
""".replace('PARENT', str(parent)).replace('GAME_EXE', quote(self.game / 'cs2.exe'))
            .replace('COMMAND_LINE', quote(command_line)).replace('EXITED', '$true' if exited else '$false')
            .replace('INVOKE_LAUNCHER', ('& ' + quote(ROOT / 'game/launch-offline.ps1') + ' -Cs2Root ' + quote(self.game_root) + ' -Launch' if vanilla else
                '& ' + quote(ROOT / 'scripts/launch-cs2-lab.ps1') + ' -StateFile ' + quote(self.state) + ' -HostProbe -Launch') + ' | ConvertTo-Json -Depth 5'), encoding='utf8')
        return subprocess.run([PWSH, '-NoLogo', '-NoProfile', '-File', str(script)],
            capture_output=True, text=True, encoding='utf8', errors='replace', timeout=15)

    def test_steam_child_verification_without_launching_game(self):
        result = self.mock_steam_launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads(result.stdout)
        self.assertEqual(receipt['ProcessId'], 18)
        self.assertEqual(receipt['LaunchRoute'], 'Steam')
        self.assertTrue(receipt['ProcessIdentityVerified'])

    def test_steam_child_extra_online_arguments_are_refused(self):
        result = self.mock_steam_launch(suffix=' +connect external-server')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('arguments differ', result.stderr)
        receipts = list(self.candidate.glob('steam-launch-*.json'))
        self.assertEqual(len(receipts), 1)
        self.assertFalse(json.loads(receipts[0].read_text())['ProcessIdentityVerified'])

    def test_steam_child_china_region_suffix_retains_offline_flags(self):
        result = self.mock_steam_launch(suffix=' -perfectworld')
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads(result.stdout)
        self.assertTrue(receipt['ProcessIdentityVerified'])
        self.assertIn('-insecure', receipt['Arguments'])
        self.assertIn('-countercraft-lab', receipt['Arguments'])

    def test_region_suffix_does_not_admit_connect(self):
        result = self.mock_steam_launch(suffix=' -perfectworld +connect external-server')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('arguments differ', result.stderr)

    def test_steam_child_wrong_parent_is_refused(self):
        result = self.mock_steam_launch(parent=19)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('as its child', result.stderr)

    def test_steam_child_exit_is_not_launch_success(self):
        result = self.mock_steam_launch(exited=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('exited during startup', result.stderr)

    def test_vanilla_offline_launch_also_uses_steam(self):
        result = self.mock_steam_launch(vanilla=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads(result.stdout)
        self.assertEqual(receipt['LaunchRoute'], 'Steam')
        self.assertFalse(receipt['ModInstalled'])
        self.assertEqual(receipt['ProcessId'], 18)

    def test_vanilla_offline_extra_arguments_are_refused(self):
        result = self.mock_steam_launch(vanilla=True, suffix=' -secure')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('arguments differ', result.stderr)

    def test_vanilla_offline_china_region_suffix(self):
        result = self.mock_steam_launch(vanilla=True, suffix=' -perfectworld')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['ProcessIdentityVerified'])

    # Fixtures remain under .local as failure evidence; no recursive cleanup/game changes.


class PlaySupervisorTests(unittest.TestCase):
    setUp = LoaderTests.setUp
    write_backup = LoaderTests.write_backup
    command = LoaderTests.command

    def play(self, mode, error=None):
        script = self.root / 'play-fixture.ps1'
        script.write_text("$ErrorActionPreference='Stop'\n"
            "function Get-Process { param($Name,$Id,$ErrorAction) }\n"
            "function Get-CimInstance { param($ClassName,$Filter) }\n"
            "function python { $global:LASTEXITCODE=0; '{\"ready\":true}' }\n"
            "& " + ps_quote(ROOT / 'game/play.ps1') + " -Mode " + mode
            + " -Cs2Root " + ps_quote(self.game_root) + " -SessionDirectory " + ps_quote(self.root)
            + " -BackupSnapshot " + ps_quote(self.backup) + " -Loader " + ps_quote(LOADER)
            + " | ConvertTo-Json -Depth 8\n", encoding='utf8')
        result = subprocess.run([PWSH,'-NoLogo','-NoProfile','-File',str(script)],capture_output=True,
            text=True,encoding='utf8',errors='replace',timeout=20)
        if error:
            self.assertNotEqual(result.returncode,0);self.assertIn(error,result.stderr)
        else:
            self.assertEqual(result.returncode,0,result.stderr)
        return result

    def test_preview_does_not_install_or_query_guest(self):
        result=self.play('Preview')
        self.assertFalse(json.loads(result.stdout)['GameFilesWritten'])
        self.assertFalse(self.target.exists());self.assertFalse((self.root/'session.json').exists())

    def test_failed_steam_start_restores_installed_files(self):
        if not (ROOT/'.local/native-build/CounterCraftProbe.addon64').exists():
            self.skipTest('Build native candidate first')
        self.play('Play',error='Start the installed Steam client first')
        self.assertFalse(self.target.exists());self.assertFalse((self.game/'ReShade.ini').exists())
        receipt=json.loads((self.root/'session.json').read_text())
        self.assertEqual(receipt['Mode'],'Restored');self.assertTrue(receipt['Failure'])

    def test_recover_is_guarded_and_repeatable(self):
        self.state=self.root/'loader-state.json'
        self.command('Install','-SteamLaunch')
        self.play('Recover');self.assertFalse(self.target.exists())
        self.assertEqual(json.loads(self.state.read_text())['Mode'],'Restored')
        self.assertEqual(json.loads(self.play('Recover').stdout)['Mode'],'AlreadyRestored')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--loader', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    LOADER = args.loader.resolve(); PWSH = shutil.which('pwsh')
    if not PWSH: parser.error('PowerShell 7.4+ required')
    unittest.main(argv=[__file__, *remaining])
