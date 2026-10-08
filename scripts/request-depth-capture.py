"""Prepare or trigger opt-in offline captures without sending game input."""
import argparse
import json
import os
from pathlib import Path
import tempfile


def write_control(candidate: Path, initialize=False):
    candidate = candidate.resolve(strict=True)
    if not (candidate/'install-plan.json').is_file():
        raise ValueError('Expected a prepared CounterCraft candidate directory')
    target = candidate/'capture-control.json'
    if initialize:
        if target.exists():
            raise ValueError('Control already exists; refusing to reset request sequence')
        request = 0
        # Exclusive creation: initialize cannot overwrite a concurrent request.
        with target.open('x',encoding='utf8') as output:
            json.dump(dict(manual=True,request=request),output)
    else:
        if target.stat().st_size > 4096:
            raise ValueError('Control exceeds 4KiB')
        data = json.loads(target.read_text(encoding='utf-8-sig'))
        if data.get('manual') is not True or type(data.get('request')) is not int or not 0<=data['request']<1000000:
            raise ValueError('Invalid manual capture control')
        request = data['request']+1
        # Atomic replacement prevents the diagnostic reader seeing partial JSON.
        # Operators must serialize requests; this is a single-writer control file.
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf8',dir=candidate,prefix='capture-control-',suffix='.tmp',delete=False) as output:
            temporary = Path(output.name)
            json.dump(dict(manual=True,request=request),output)
        try:
            os.replace(temporary,target)
        finally:
            temporary.unlink(missing_ok=True)
    return dict(control=str(target),manual=True,request=request,
                notice='Request only; check consumed/queued/written counters. Initialize before launching with -DepthCapture.')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate',type=Path)
    parser.add_argument('--initialize',action='store_true')
    args=parser.parse_args()
    print(json.dumps(write_control(args.candidate,args.initialize),indent=2))
