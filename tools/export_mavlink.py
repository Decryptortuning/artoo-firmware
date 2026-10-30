#!/usr/bin/env python3
"""Generate a pinned Solo C1 export into a new directory; no network access."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--archive',type=Path,required=True)
    ap.add_argument('--product-root',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--epoch',type=int,required=True)
    args=ap.parse_args()
    contract_path=ROOT/'vendor/mavlink/contract.json'
    contract=json.loads(contract_path.read_text())
    if args.epoch < 0 or args.output.exists():
        raise SystemExit('Nonnegative epoch and a new output directory required')
    if sha(args.archive)!=contract['archive_sha256']:
        raise SystemExit('Generator archive differs from pinned contract')
    for rel,digest in contract['product_files'].items():
        if sha(args.product_root/rel)!=digest:
            raise SystemExit('Product contract input changed: '+rel)
    for rel,digest in contract['owned_patches'].items():
        if sha(ROOT/rel)!=digest:
            raise SystemExit('Owned generator patch changed: '+rel)
    os.environ['SOURCE_DATE_EPOCH']=str(args.epoch)
    with tempfile.TemporaryDirectory(prefix='artoo-mavlink-') as tmp:
        stage=Path(tmp)
        with tarfile.open(args.archive) as tar:
            if any(m.issym() or m.islnk() for m in tar.getmembers()):
                raise SystemExit('Unexpected links in generator archive')
            tar.extractall(stage,filter='data')
        source=stage/('pymavlink-'+contract['generator_version'])
        patch=next(args.product_root/p for p in contract['product_files'] if p.endswith('.patch'))
        subprocess.run(['patch','--batch','--forward','-p1','-i',str(patch.resolve())],cwd=source,check=True)
        for rel in contract['owned_patches']:
            subprocess.run(['patch','--batch','--forward','-p1','-i',str(ROOT/rel)],cwd=source,check=True)
        spec=importlib.util.spec_from_file_location('pymavlink',source/'__init__.py',submodule_search_locations=[str(source)])
        package=importlib.util.module_from_spec(spec)
        sys.modules['pymavlink']=package
        spec.loader.exec_module(package)
        if package.__version__!=contract['generator_version']:
            raise SystemExit('Unexpected generator version')
        from pymavlink.generator import mavgen, mavparse
        import lxml.etree
        output=args.output.resolve()
        output.mkdir(parents=True,exist_ok=False)
        headers=output/'include/opensolo/mavlink'
        xml=args.product_root/'recipes-mavlink/mavlink/files/solo-dialect/solo.xml'
        options=mavgen.Opts(str(headers),wire_protocol=mavparse.PROTOCOL_1_0,language='C',validate=True)
        if not mavgen.mavgen(options,[str(xml.resolve())]):
            raise SystemExit('Generator failed; incomplete export has no manifest')
        manifest={'schema':1,'wire_protocol':'1.0','generator_version':package.__version__,
                  'source_date_epoch':args.epoch,'contract':contract,'contract_sha256':sha(contract_path),
                  'exporter_sha256':sha(Path(__file__)),
                  'python_version':sys.version,'lxml_version':lxml.etree.LXML_VERSION,
                  'header_sha256':{str(p.relative_to(output)):sha(p) for p in sorted(headers.rglob('*')) if p.is_file()}}
        (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
if __name__=='__main__': main()
