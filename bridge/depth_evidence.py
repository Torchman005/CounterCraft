"""Compare private pre-draw depth captures by reprojection, without selecting a live pass.

This tests geometric consistency of caller-selected evidence. Occlusion, moving
geometry, MSAA edges and mixed depth ranges can all cause disagreement.
"""
from __future__ import annotations

import argparse
from array import array
from dataclasses import dataclass
import json
import math
from pathlib import Path
import sys

from .camera_evidence import analyze_capture, bound_matrices


def transform(matrix, point):
    return tuple(sum(a*b for a, b in zip(row, point)) for row in matrix)


@dataclass
class Evidence:
    metadata: dict
    candidate: dict
    view: tuple
    projection: tuple
    width: int
    height: int
    pixels: array
    camera_capture_id: int | str | None = None

    @classmethod
    def read(cls, path: Path, camera_path: Path | None = None):
        path = Path(path).resolve()
        camera_path = Path(camera_path).resolve() if camera_path else path
        analysis = analyze_capture(camera_path)
        if len(analysis['candidates']) != 1:
            raise ValueError('Exactly one mathematical camera candidate required')
        if path.stat().st_size > 65536:
            raise ValueError('Depth manifest exceeds 64KiB')
        metadata = json.loads(path.read_text(encoding='utf8'))
        candidate = analysis['candidates'][0]
        camera_metadata = json.loads(camera_path.read_text(encoding='utf8'))
        if camera_path != path:
            validate_boundary_pair(path, metadata, camera_path, camera_metadata, candidate)
        depth = metadata.get('depth', {})
        size = depth.get('size')
        if (not isinstance(size, list) or len(size) != 2
                or any(type(n) is not int or n < 1 for n in size) or math.prod(size) > 4_194_304):
            raise ValueError('Invalid bounded depth dimensions')
        width, height = size
        if candidate['viewport'][:4] != [0, 0, width, height]:
            raise ValueError('Full-depth-sized viewport required for this experiment')
        if (depth.get('file') != 'depth-minmax.f32'
                or depth.get('encoding') != 'little-endian float32 min,max; top-left row-major'):
            raise ValueError('Unsupported depth encoding/file')
        file = path.parent / depth['file']
        if file.stat().st_size != width*height*8:
            raise ValueError('Incomplete depth data')
        pixels = array('f')
        pixels.frombytes(file.read_bytes())
        if sys.byteorder != 'little':
            pixels.byteswap()
        if any(not math.isfinite(n) or not 0 <= n <= 1 for n in pixels):
            raise ValueError('Nonfinite/out-of-range depth data')
        if any(pixels[i] > pixels[i+1] for i in range(0, len(pixels), 2)):
            raise ValueError('Reversed min/max samples')
        matrices = bound_matrices(camera_path.parent, camera_metadata['constantBuffers'])
        def matrix(name):
            return next(m.values for m in matrices if m.location() == candidate[name])
        return cls(metadata, candidate, matrix('view'), matrix('projection'), width, height, pixels,
                   camera_metadata.get('captureId'))

    def depth(self, x, y, bounds, max_spread):
        index = (y*self.width+x)*2
        lo, hi = self.pixels[index:index+2]
        if bounds[0] <= lo <= hi < bounds[1] and hi-lo <= max_spread:
            return lo
        return None

    def eye_z(self, raw):
        lo, hi = self.candidate['viewport'][4:]
        ndc = (raw-lo)/(hi-lo)
        p = self.projection
        divisor = ndc*p[3][2]-p[2][2]
        return (p[2][3]-ndc*p[3][3])/divisor if divisor else math.inf

    def world(self, x, y, raw):
        z = self.eye_z(raw)
        p, v = self.projection, self.view
        clip_w = p[3][2]*z+p[3][3]
        eye = (((x+.5)/self.width*2-1)*clip_w/p[0][0]-p[0][2]*z/p[0][0],
               (1-(y+.5)/self.height*2)*clip_w/p[1][1]-p[1][2]*z/p[1][1], z)
        # Rigid inverse: R^T * (eye - translation).
        return tuple(sum(v[i][j]*(eye[i]-v[i][3]) for i in range(3)) for j in range(3))+(1,)


def validate_boundary_pair(depth_path, depth, camera_path, camera, candidate):
    # Session scope comes from the private writer's directory, never a reused
    # candidateId/frame alone. Both paths must resolve to sibling capture folders.
    if depth_path.parent.parent != camera_path.parent.parent:
        raise ValueError('Camera and depth must belong to the same capture session')
    for metadata in (depth, camera):
        if metadata.get('schema') != 1 or metadata.get('timing') != 'before-current-draw':
            raise ValueError('Unsupported boundary timing/schema')
        for key in ('frame', 'candidateId', 'request', 'candidateDraw'):
            if type(metadata.get(key)) is not int or metadata[key] <= 0:
                raise ValueError('Manual boundary identity must be explicit')
    for key in ('frame', 'candidateId', 'request', 'outputSize'):
        if depth.get(key) != camera.get(key):
            raise ValueError('Camera/depth frame, resource, request or output mismatch')
    if (camera['candidateDraw'] >= depth['candidateDraw']
            or camera.get('trigger') != 'viewport-transitions'
            or camera.get('viewportTransitions') != 0
            or depth.get('trigger') != 'viewport-transitions'
            or depth.get('viewportTransitions') != 1
            or depth.get('bindingTiming') != 'current-draw-after-viewport-change-or-draw64'):
        raise ValueError('Require earlier camera context and the first viewport boundary')
    previous, current = depth.get('previousViewport'), depth.get('viewports')
    if (previous != candidate['viewport'] or not isinstance(current, list) or len(current) != 1
            or len(current[0]) != 6 or current[0][:4] != previous[:4]
            or any(type(n) not in (int, float) or not math.isfinite(n) for n in current[0])
            or not 0 <= current[0][4] < current[0][5] <= 1
            or current[0][4:] == previous[4:]):
        raise ValueError('Boundary must change depth range while preserving viewport rectangle')
    for key in ('size', 'samples', 'quality', 'sourceFormat'):
        if key not in depth.get('depth', {}) or depth['depth'][key] != camera.get('depth', {}).get(key):
            raise ValueError('Camera/depth texture description mismatch')


def compare(source: Evidence, target: Evidence, *, raw_bounds, stride=8,
            max_spread=.001, relative_tolerance=.01):
    if (type(stride) is not int or not 1 <= stride <= 1024
            or len(raw_bounds) != 2 or any(not math.isfinite(n) for n in raw_bounds)
            or not 0 <= raw_bounds[0] < raw_bounds[1] <= 1
            or not math.isfinite(max_spread) or not 0 <= max_spread <= 1
            or not math.isfinite(relative_tolerance) or not 0 < relative_tolerance < 1):
        raise ValueError('Invalid sampling bounds/tolerance')
    for evidence in (source, target):
        lo, hi = evidence.candidate['viewport'][4:]
        if raw_bounds[0] < lo or raw_bounds[1] > hi:
            raise ValueError('Raw interval must lie within both projection viewports')
    counts = dict(sampled=0, sourceAccepted=0, outsideTarget=0, targetRejected=0,
                  compared=0, withinTolerance=0, targetCloser=0, targetFarther=0)
    errors = []
    for y in range(stride//2, source.height, stride):
        for x in range(stride//2, source.width, stride):
            counts['sampled'] += 1
            raw = source.depth(x, y, raw_bounds, max_spread)
            if raw is None:
                continue
            counts['sourceAccepted'] += 1
            eye = transform(target.view, source.world(x, y, raw))
            clip = transform(target.projection, eye)
            if (not all(math.isfinite(n) for n in clip) or clip[3] <= 0
                    or not 0 <= clip[2]/clip[3] <= 1):
                counts['outsideTarget'] += 1
                continue
            tx = math.floor((clip[0]/clip[3]+1)*target.width/2)
            ty = math.floor((1-clip[1]/clip[3])*target.height/2)
            if not 0 <= tx < target.width or not 0 <= ty < target.height:
                counts['outsideTarget'] += 1
                continue
            actual = target.depth(tx, ty, raw_bounds, max_spread)
            if actual is None:
                counts['targetRejected'] += 1
                continue
            observed, predicted = abs(target.eye_z(actual)), abs(eye[2])
            error = (predicted-observed)/observed
            if not math.isfinite(error):
                counts['targetRejected'] += 1
                continue
            counts['compared'] += 1
            errors.append(abs(error))
            counts['withinTolerance' if abs(error) <= relative_tolerance
                   else 'targetCloser' if error > 0 else 'targetFarther'] += 1
    errors.sort()
    return dict(sourceCapture=source.metadata.get('captureId'), targetCapture=target.metadata.get('captureId'),
                sourceCameraCapture=source.camera_capture_id, targetCameraCapture=target.camera_capture_id,
                rawInterval=list(raw_bounds), stride=stride, maxMsaaSpread=max_spread,
                relativeTolerance=relative_tolerance, counts=counts,
                absoluteRelativeError={name: errors[round((len(errors)-1)*q)] if errors else None
                                       for name, q in [('p10', .1), ('p50', .5), ('p90', .9)]},
                withinFraction=counts['withinTolerance']/len(errors) if errors else None,
                cameraDepthVerified=False, autoSelected=False,
                limitation='Caller-selected partial depths; nearer targets may occlude source points. '
                           'Raw interval filtering does not identify or remove a weapon pass.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--source-camera', type=Path, help='Earlier same-frame camera capture for source boundary depth')
    parser.add_argument('--target-camera', type=Path, help='Earlier same-frame camera capture for target boundary depth')
    parser.add_argument('--raw-interval', type=float, nargs=2, required=True, metavar=('MIN', 'MAX'))
    parser.add_argument('--stride', type=int, default=8)
    parser.add_argument('--max-msaa-spread', type=float, default=.001)
    parser.add_argument('--relative-tolerance', type=float, default=.01)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = compare(Evidence.read(args.source, args.source_camera), Evidence.read(args.target, args.target_camera), raw_bounds=args.raw_interval,
                     stride=args.stride, max_spread=args.max_msaa_spread, relative_tolerance=args.relative_tolerance)
    text = json.dumps(report, indent=2, allow_nan=False)+'\n'
    if args.output:
        args.output.write_text(text, encoding='utf8')
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
