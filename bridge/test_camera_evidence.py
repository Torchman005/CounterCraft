import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

from bridge.camera_evidence import analyze_capture, calibration, cross, dot, multiply


class CameraEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.path = self.directory/'capture.json'
        self.position = (-1234.5,-567.25,205.125)
        yaw,pitch = math.radians(43),math.radians(17)
        forward = (math.cos(yaw)*math.cos(pitch),math.sin(yaw)*math.cos(pitch),-math.sin(pitch))
        right = (math.sin(yaw),-math.cos(yaw),0)
        up = cross(right,forward)
        rows = [right,up,tuple(-n for n in forward)]
        self.view = tuple(tuple(row)+( -dot(row,self.position),) for row in rows)+((0,0,0,1),)
        self.projection = ((1.125,0,.0001,0),(0,1.8,-.0002,0),(0,0,-10000/9996,-40000/9996),(0,0,-1,0))
        relative = tuple(tuple(0 if c==3 and r<3 else self.view[r][c] for c in range(4)) for r in range(4))
        self.matrices = [self.view,self.projection,multiply(self.projection,relative),multiply(self.projection,self.view)]
        self.metadata = dict(schema=1,timing='before-current-draw',frame=240,captureId=1,candidateId=3,candidateDraw=256,
            viewports=[[0,0,1680,1050,0,.95]],constantBuffers=[dict(stage='VS',slot=0,bytes=272,
                file='vs-0.bin',firstConstant=0,numConstants=4096)])
        self.write_fixture()

    def tearDown(self):
        self.temp.cleanup()

    def write_fixture(self,layouts=('column-major','row-major','column-major','column-major')):
        values = [99,88,77,66]
        for m,layout in zip(self.matrices,layouts):
            values.extend(m[r][c] for a in range(4) for b in range(4)
                          for r,c in [(a,b) if layout=='row-major' else (b,a)])
        (self.directory/'vs-0.bin').write_bytes(struct.pack('<'+str(len(values))+'f',*values))
        self.path.write_text(json.dumps(self.metadata),encoding='utf8')

    def test_pose_and_lens_need_two_independent_products(self):
        report = analyze_capture(self.path)
        self.assertEqual(len(report['candidates']),1)
        candidate = report['candidates'][0]
        for a,b in zip(candidate['position'],self.position): self.assertAlmostEqual(a,b,places=3)
        self.assertAlmostEqual(candidate['sourceYaw'],43,places=4)
        self.assertAlmostEqual(candidate['sourcePitch'],17,places=4)
        self.assertAlmostEqual(candidate['lens']['near'],4,places=4)
        self.assertAlmostEqual(candidate['lens']['far'],10000,delta=3)
        self.assertFalse(candidate['sceneVerified']); self.assertFalse(report['cameraDepthVerified'])
        self.assertFalse(report['autoSelected'])

    def test_all_layouts_and_offsets_derive_from_binding_bytes(self):
        for layout in ('row-major','column-major'):
            self.write_fixture((layout,)*4)
            candidate = analyze_capture(self.path)['candidates'][0]
            self.assertEqual(candidate['view']['byteOffset'],16)
            self.assertEqual(candidate['view']['layout'],layout)

    def test_calibration_is_explicit_and_requires_unique_candidate(self):
        report=analyze_capture(self.path)
        layout=calibration(report)
        self.assertEqual(layout['view'],report['candidates'][0]['view'])
        self.assertEqual(layout['worldVP'],report['candidates'][0]['worldVP'][0])
        self.assertFalse(layout['autoSelected'])
        for candidates in ([],report['candidates']*2):
            with self.assertRaises(ValueError): calibration(dict(candidates=candidates))

    def test_similar_projection_and_view_without_world_vp_are_rejected(self):
        self.matrices[-1] = ((0,0,0,0),)*4
        self.write_fixture()
        self.assertEqual(analyze_capture(self.path)['candidates'],[])

    def test_without_relative_vp_are_rejected(self):
        self.matrices[2] = ((0,0,0,0),)*4
        self.write_fixture()
        self.assertEqual(analyze_capture(self.path)['candidates'],[])

    def test_unbound_partial_range_is_not_scanned(self):
        self.metadata['constantBuffers'][0]['firstConstant'] = 16
        self.metadata['constantBuffers'][0]['numConstants'] = 16
        self.write_fixture()
        self.assertEqual(analyze_capture(self.path)['candidates'],[])

    def test_wrong_viewport_aspect_refuses_camera_set(self):
        self.metadata['viewports'][0][2] = 1920
        self.write_fixture()
        self.assertEqual(analyze_capture(self.path)['candidates'],[])

    def test_buffer_length_and_external_path_are_refused(self):
        (self.directory/'vs-0.bin').write_bytes(b'broken')
        with self.assertRaisesRegex(ValueError,'byte length'): analyze_capture(self.path)
        self.metadata['constantBuffers'][0]['file'] = '../outside.bin'
        self.path.write_text(json.dumps(self.metadata))
        with self.assertRaisesRegex(ValueError,'local'): analyze_capture(self.path)

    def test_nonfinite_or_degenerate_viewport_refused(self):
        for value in (float('nan'),0):
            self.metadata['viewports'][0][3] = value
            self.write_fixture()
            with self.assertRaises(ValueError): analyze_capture(self.path)


if __name__ == '__main__': unittest.main()
