"""Offline mathematical investigation of opt-in public D3D11 binding captures.

No process/memory access and no retail buffer offsets. A consistent matrix set
is still a candidate: scene/pose/timing and the final depth pass need a game oracle.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import struct


@dataclass(frozen=True)
class Matrix:
    values: tuple[tuple[float, ...], ...]
    stage: str
    slot: int
    offset: int
    layout: str

    def location(self):
        return dict(stage=self.stage, slot=self.slot, byteOffset=self.offset, layout=self.layout)


def multiply(a, b):
    return tuple(tuple(sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)) for r in range(4))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def matches(a, b):
    return all(abs(a[r][c]-b[r][c]) <= 1e-4+1e-6*abs(b[r][c]) for r in range(4) for c in range(4))


def lens(m, aspect):
    if any(abs(m[r][c]) > 1e-7 for r, c in ((0,1),(0,3),(1,0),(1,3),(2,0),(2,1),(3,0),(3,1),(3,3))):
        return None
    if m[0][0] <= 0 or m[1][1] <= 0 or abs(abs(m[3][2])-1) > 1e-6 or m[2][3] == 0:
        return None
    scale = m[1][1]/m[0][0]
    fov = math.degrees(2*math.atan(1/m[1][1]))
    if not 1 < fov < 179 or abs(scale/aspect-1) > .005:
        return None
    a,b,c,d = m[2][2],m[2][3],m[3][2],m[3][3]
    sign = 1 if c > 0 else -1
    def eye(z):
        divisor = z*c-a
        return sign*(b-z*d)/divisor if divisor != 0 else math.inf
    zero,one = eye(0),eye(1)
    near,far = min(zero,one),max(zero,one)
    if not math.isfinite(near) or near < 1e-6 or far <= near:
        return None
    return dict(near=near, far=far if math.isfinite(far) else None, infiniteFar=not math.isfinite(far),
                verticalFov=fov, aspect=scale, reversedZ=zero>one, rightHanded=sign<0)


def affine(m):
    if any(abs(m[3][i]) > 1e-7 for i in range(3)) or abs(m[3][3]-1) > 1e-7:
        return False
    rows = [row[:3] for row in m[:3]]
    if any(abs(dot(rows[r], rows[c])-(1 if r == c else 0)) > 1e-5 for r in range(3) for c in range(3)):
        return False
    return abs(dot(rows[0],cross(rows[1],rows[2]))-1) < 1e-5


def bound_matrices(directory, buffers):
    matrices = []
    if not isinstance(buffers, list) or len(buffers) > 28:
        raise ValueError('invalid bounded constant-buffer manifest')
    for buffer in buffers:
        if buffer.get('stage') != 'VS' or 'skipped' in buffer:
            continue
        name = buffer['file']
        if not isinstance(name, str) or Path(name).name != name or '\\' in name or '/' in name:
            raise ValueError('constant-buffer file must be local to the capture')
        length,slot = buffer['bytes'],buffer['slot']
        if type(length) is not int or not 16 <= length <= 65536 or length % 16 or type(slot) is not int or not 0 <= slot <= 13:
            raise ValueError('invalid constant-buffer size/slot')
        path = directory / name
        if path.stat().st_size != length:
            raise ValueError('constant-buffer byte length mismatch')
        raw = path.read_bytes()
        first,count = buffer.get('firstConstant'),buffer.get('numConstants')
        if first is None and count is None:
            begin,end = 0,length
        else:
            if type(first) is not int or type(count) is not int or first < 0 or count < 0 or first > 4096 or count > 4096:
                raise ValueError('invalid partial constant-buffer range')
            begin,end = min(first*16,length),min((first+count)*16,length)
        for offset in range(begin,end-63,16):
            floats = struct.unpack_from('<16f',raw,offset)
            if any(not math.isfinite(x) or abs(x)>1e7 for x in floats):
                continue
            for layout in ('row-major','column-major'):
                values = tuple(tuple(floats[r*4+c if layout=='row-major' else c*4+r] for c in range(4)) for r in range(4))
                matrices.append(Matrix(values,'VS',slot,offset,layout))
    return matrices


def analyze_capture(path: str | Path):
    path = Path(path).resolve()
    if path.stat().st_size > 65536:
        raise ValueError('capture manifest exceeds 64KiB')
    metadata = json.loads(path.read_text(encoding='utf8'))
    if metadata.get('schema') != 1 or metadata.get('timing') != 'before-current-draw':
        raise ValueError('unsupported capture schema/timing')
    viewports = metadata.get('viewports')
    if not isinstance(viewports,list) or len(viewports) != 1 or len(viewports[0]) != 6:
        raise ValueError('single verified-size viewport required for analysis')
    x,y,w,h,lo,hi = viewports[0]
    if any(type(v) not in (int,float) or not math.isfinite(v) for v in viewports[0]) or w<=0 or h<=0 or not 0<=lo<hi<=1:
        raise ValueError('invalid viewport')
    matrices = bound_matrices(path.parent, metadata['constantBuffers'])
    views = [m for m in matrices if affine(m.values)]
    projections = [(m,lens(m.values,w/h)) for m in matrices]
    projections = [(m,l) for m,l in projections if l is not None]
    candidates,seen = [],set()
    for p,l in projections:
        for v in views:
            identity = tuple(round(n,6) for m in (p.values,v.values) for row in m for n in row)
            if identity in seen:
                continue
            full = multiply(p.values,v.values)
            relative_view = tuple(tuple(0 if c==3 and r<3 else v.values[r][c] for c in range(4)) for r in range(4))
            relative = multiply(p.values,relative_view)
            # A zero-translation view does not provide two independent VP tests.
            if matches(full,relative):
                continue
            full_refs = [m.location() for m in matrices if matches(m.values,full)]
            relative_refs = [m.location() for m in matrices if matches(m.values,relative)]
            if not full_refs or not relative_refs:
                continue
            seen.add(identity)
            r = [row[:3] for row in v.values[:3]]
            t = [v.values[i][3] for i in range(3)]
            position = [-sum(r[i][j]*t[i] for i in range(3)) for j in range(3)]
            sign = -1 if l['rightHanded'] else 1
            forward = [sign*n for n in r[2]]
            yaw = math.degrees(math.atan2(forward[1],forward[0]))
            pitch = math.degrees(math.atan2(-forward[2],math.hypot(forward[0],forward[1])))
            candidates.append(dict(projection=p.location(),view=v.location(),worldVP=full_refs,relativeVP=relative_refs,
                mathematicalConsistency=True,sceneVerified=False,position=position,forward=forward,up=list(r[1]),
                sourceYaw=yaw,sourcePitch=pitch,lens=l,viewport=viewports[0],
                unitsVerified=False,coordinateConvention='candidate Source X/Y horizontal, Z up'))
            if len(candidates) >= 8:
                break
        if len(candidates) >= 8:
            break
    return dict(captureId=metadata.get('captureId'),frame=metadata.get('frame'),candidateId=metadata.get('candidateId'),
        candidateDraw=metadata.get('candidateDraw'),timing=metadata['timing'],cameraDepthVerified=False,
        autoSelected=False,candidates=candidates,
        limitation='Math consistency is not scene/pose/timing verification; pre-draw depth is partial.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    report = analyze_capture(args.capture)
    text = json.dumps(report,indent=2,allow_nan=False)
    if args.output:
        args.output.write_text(text+'\n',encoding='utf8')
    else:
        print(text)


if __name__ == '__main__':
    main()
