# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Render theme materials and controls with the real D3D11 Frame UI renderer."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--theme', choices=('BlueGold', 'Legacy'), default='BlueGold')
    parser.add_argument('--config', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--scene', choices=('materials', 'character', 'options', 'hud'), default='character')
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('The native preview currently requires D3D11 on Windows.')
    output = (args.output or ROOT / 'artifacts/ui-theme' / (args.theme.lower() + '-' + args.scene + '-native.png')).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    raw = output.with_suffix('.rgba')
    env = dict(os.environ, MMO_THEME_PREVIEW=args.theme, MMO_THEME_PREVIEW_OUTPUT=str(raw), MMO_THEME_PREVIEW_SCENE=args.scene)
    executable = ROOT / 'bin' / args.config / 'frame_ui_input_tests.exe'
    subprocess.run([str(executable), '[.theme_preview]' if args.scene == 'materials' else '[.theme_windows]'], cwd=ROOT, env=env, check=True)
    Image.frombytes('RGBA', (1920, 1080) if args.scene == 'materials' else (2560, 1440), raw.read_bytes()).save(output)
    print(output)
