import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('capture_control',Path(__file__).resolve().parents[1]/'scripts/request-depth-capture.py')
control=importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)


class CaptureControlTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        (self.root/'install-plan.json').write_text('{}')
        self.target=self.root/'capture-control.json'

    def tearDown(self):
        self.temp.cleanup()

    def test_initialize_and_sequence_never_silently_reset(self):
        self.assertEqual(control.write_control(self.root,True)['request'],0)
        self.assertEqual(control.write_control(self.root)['request'],1)
        with self.assertRaises(ValueError): control.write_control(self.root,True)
        self.assertEqual(control.write_control(self.root)['request'],2)
        self.assertEqual(json.loads(self.target.read_text()),dict(manual=True,request=2))
        self.assertFalse(list(self.root.glob('*.tmp')))

    def test_bad_values_are_preserved_without_triggering(self):
        for request in (True,-1,1.5,1000000,'2'):
            before=json.dumps(dict(manual=True,request=request))
            self.target.write_text(before)
            with self.assertRaises(ValueError): control.write_control(self.root)
            self.assertEqual(self.target.read_text(),before)

    def test_unprepared_directory_refused(self):
        (self.root/'install-plan.json').unlink()
        with self.assertRaises(ValueError): control.write_control(self.root,True)
        self.assertFalse(self.target.exists())

    def test_automatic_mode_and_oversize_refused(self):
        self.target.write_text('{"manual":false,"request":0}')
        with self.assertRaises(ValueError): control.write_control(self.root)
        self.target.write_text(' '*4097)
        with self.assertRaises(ValueError): control.write_control(self.root)


if __name__=='__main__': unittest.main()
