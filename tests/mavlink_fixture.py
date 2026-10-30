"""Use the same reviewed generated headers in native consumer fixtures."""
import importlib.util
import os
from pathlib import Path


def include_flags():
    selected = os.environ.get('ARTOO_MAVLINK_EXPORT')
    if not selected:
        raise RuntimeError('Set ARTOO_MAVLINK_EXPORT for fixtures consuming MCU MAVLink headers')
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location('artoo_build_record', root / 'tools/build_record.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.mavlink_inventory(selected)
    return ['-I', str(Path(selected).resolve() / 'include')]
