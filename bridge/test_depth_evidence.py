from array import array
from dataclasses import replace
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest

from bridge.camera_evidence import multiply
from bridge.depth_evidence import Evidence, compare


class DepthEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.a = self.fixture('a', (1, 2, 3), 0)
        self.b = self.fixture('b', (2, 2, 4), 5)

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, name, origin, angle):
        directory = self.root/name
        directory.mkdir()
        width, height, near, far = 160, 100, 1, 1000
        c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        rotation = ((c, 0, -s), (0, 1, 0), (s, 0, c))
        view = tuple(row+(-sum(a*b for a, b in zip(row, origin)),) for row in rotation)+((0, 0, 0, 1),)
        p = ((1.25, 0, 0, 0), (0, 2, 0, 0), (0, 0, -far/(far-near), -far*near/(far-near)), (0, 0, -1, 0))
        relative = tuple(row+(0,) for row in rotation)+((0, 0, 0, 1),)
        matrices = (view, p, multiply(p, view), multiply(p, relative))
        (directory/'vs-0.bin').write_bytes(struct.pack('<64f', *(n for m in matrices for row in m for n in row)))
        # Analytic intersection with the fixed world plane z=-20. This does not
        # use the production unproject/transform or projection inverse functions.
        raw = []
        for y in range(height):
            for x in range(width):
                horizontal = ((x+.5)/width*2-1)/1.25
                distance = (20+origin[2])/(s*horizontal+c)
                depth = .95*(far/(far-near)-far*near/((far-near)*distance))
                raw.extend((depth, depth))
        (directory/'depth-minmax.f32').write_bytes(struct.pack('<'+str(len(raw))+'f', *raw))
        manifest = dict(schema=1, captureId=name, timing='before-current-draw',
                        viewports=[[0, 0, width, height, 0, .95]],
                        constantBuffers=[dict(stage='VS', slot=0, bytes=256, file='vs-0.bin')],
                        depth=dict(size=[width, height], file='depth-minmax.f32',
                                   encoding='little-endian float32 min,max; top-left row-major'))
        path = directory/'capture.json'
        path.write_text(json.dumps(manifest))
        return Evidence.read(path)

    def test_static_plane_agrees_after_translation_and_rotation(self):
        report = compare(self.a, self.b, raw_bounds=(0, .95), stride=4, relative_tolerance=.002)
        self.assertGreater(report['counts']['compared'], 600)
        self.assertGreater(report['withinFraction'], .99)
        self.assertLess(report['absoluteRelativeError']['p50'], .001)
        self.assertFalse(report['cameraDepthVerified'])
        self.assertFalse(report['autoSelected'])

    def test_wrong_view_origin_is_measurably_rejected(self):
        view = tuple(tuple(v+(2 if r==2 and c==3 else 0) for c, v in enumerate(row))
                     for r, row in enumerate(self.b.view))
        report = compare(self.a, replace(self.b, view=view), raw_bounds=(0, .95), stride=4)
        self.assertLess(report['withinFraction'], .01)
        self.assertGreater(report['absoluteRelativeError']['p50'], .05)

    def test_missing_geometry_and_msaa_edges_do_not_imply_success(self):
        for pixels in (array('f', [1, 1])*(self.b.width*self.b.height),
                       array('f', [.1, .9])*(self.b.width*self.b.height)):
            result = compare(self.a, replace(self.b, pixels=pixels), raw_bounds=(0, .95), stride=8)
            self.assertIsNone(result['withinFraction'])
            self.assertIsNone(result['absoluteRelativeError']['p50'])
            self.assertEqual(result['counts']['compared'], 0)

    def test_self_capture_matches_pixel_centres_and_range(self):
        report = compare(self.a, self.a, raw_bounds=(.5, .95), stride=4)
        self.assertEqual(report['withinFraction'], 1)
        self.assertLess(report['absoluteRelativeError']['p90'], 1e-6)

    def test_invalid_parameters_and_truncated_or_invalid_data(self):
        for options in (dict(stride=0), dict(stride=True), dict(raw_bounds=(0, 1)),
                        dict(max_spread=float('nan')), dict(relative_tolerance=0)):
            with self.assertRaises(ValueError):
                compare(self.a, self.b, **(dict(raw_bounds=(0, .95)) | options))
        path = self.root/'a'/'depth-minmax.f32'
        data = path.read_bytes()
        for changed in (data[:-1], struct.pack('<2f', float('nan'), 1)+data[8:],
                        struct.pack('<2f', .9, .1)+data[8:]):
            path.write_bytes(changed)
            with self.assertRaises(ValueError):
                Evidence.read(self.root/'a'/'capture.json')


if __name__ == '__main__':
    unittest.main()
