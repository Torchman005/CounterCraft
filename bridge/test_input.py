import unittest
from bridge.frame_stream import FrameReader
from bridge.test_frame_stream import Fragmented, packet, SESSION

class ClientLayerTests(unittest.TestCase):
    def test_full_client_color_has_explicit_mixed_depth_semantics(self):
        m = FrameReader(Fragmented(packet()), SESSION, 7).read().metadata
        m.update(includesHandHud=True, layer="client-color-world-depth", guiOpen=True)
        f = FrameReader(Fragmented(packet(metadata=m)), SESSION, 7).read()
        self.assertTrue(f.metadata["guiOpen"])
        for key, bad in (("layer", "world"), ("guiOpen", 1)):
            invalid = dict(m); invalid[key] = bad
            with self.assertRaises(ValueError): FrameReader(Fragmented(packet(metadata=invalid)),SESSION,7).read()
