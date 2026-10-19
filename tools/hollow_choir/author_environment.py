# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Author the Hollow Choir environment without touching other gameplay catalogs."""

import argparse
import importlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.agents/skills/mmo-npc-designer/scripts'))
from proto_runtime import compile_proto_modules
from google.protobuf.json_format import MessageToDict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    compile_proto_modules(ROOT)
    review = ROOT / 'generated/hollow_choir/environment'
    review.mkdir(parents=True, exist_ok=True)
    tables = {}
    original = {}
    for name, cls in [('environment_profiles', 'EnvironmentProfiles'), ('maps', 'Maps'), ('zones', 'Zones')]:
        module = importlib.import_module(name + '_pb2')
        tables[name] = []
        for root in ('data/editor/data', 'data/client/ClientDB'):
            path = ROOT / root / (name + '.data')
            original[path] = path.read_bytes()
            message = getattr(module, cls)()
            message.ParseFromString(original[path])
            tables[name].append((path, message))

    profiles = tables['environment_profiles'][0][1]
    existing = next((p for p in profiles.entry if p.name == 'The Hollow Choir - Still Stone'), None)
    profile_id = existing.id if existing else max(p.id for _, m in tables['environment_profiles'] for p in m.entry) + 1
    module = importlib.import_module('environment_profiles_pb2')
    profile = module.EnvironmentProfile(id=profile_id, name='The Hollow Choir - Still Stone')
    # Explicitly constant curves prevent outdoor defaults (including dawn warmth) from leaking in.
    colors = {
        'sky_horizon': (0.055, 0.067, 0.085, 1),
        'sky_zenith': (0.015, 0.021, 0.033, 1),
        'clouds': (0.075, 0.085, 0.105, 1),
        'ambient': (0.026, 0.030, 0.038, 1),
        'sun': (0.62, 0.70, 0.82, 0.14),
        'moon': (0.62, 0.70, 0.82, 0.14),
        'fog': (0.060, 0.071, 0.090, 1),
        'sun_scatter': (0.62, 0.70, 0.82, 0.35),
    }
    for field, (r, g, b, a) in colors.items():
        for time in (0, 1):
            getattr(profile, field).key.add(time=time, r=r, g=g, b=b, a=a)
    settings = {
        'fog_density': 0.0035, 'fog_height_falloff': 0.055, 'fog_base_height': 1,
        'fog_anisotropy': 0.18, 'shaft_strength': 0.45,
        'exposure': 1.05, 'bloom_intensity': 0.12, 'bloom_threshold': 1.05,
        'transition_seconds': 3, 'wind_direction': 45, 'wind_speed': 0.15,
        'wind_gustiness': 0.05, 'fog_noise_amount': 0.16, 'fog_noise_size': 24,
        'light_scattering': 0.75, 'saturation': 0.88, 'contrast': 1.02,
        'color_filter_r': 0.98, 'color_filter_g': 0.99, 'color_filter_b': 1.02,
        'color_lut': '',
    }
    for field, value in settings.items():
        setattr(profile, field, value)
    assert profile.IsInitialized()
    for path, table in tables['environment_profiles']:
        old = next((p for p in table.entry if p.id == profile_id), None)
        assert old is None or old.name == profile.name
        (old if old is not None else table.entry.add()).CopyFrom(profile)
    for path, table in tables['maps']:
        target = next(e for e in table.entry if e.id == 1)
        assert target.name == 'The Hollow Choir'
        target.environment_profile = profile_id
    for path, table in tables['zones']:
        target = next(e for e in table.entry if e.id == 26)
        assert target.name == 'The Hollow Choir'
        target.environment_profile = profile_id

    # Verify every pre-existing row and every field outside the intended assignments.
    for name, copies in tables.items():
        for path, after in copies:
            before = type(after)()
            before.ParseFromString(original[path])
            prior = {e.id: e for e in before.entry}
            for entry in after.entry:
                if name == 'environment_profiles' and entry.id == profile_id:
                    continue
                old = prior[entry.id]
                check = type(entry)(); check.CopyFrom(entry)
                old_check = type(old)(); old_check.CopyFrom(old)
                if (name == 'maps' and entry.id == 1) or (name == 'zones' and entry.id == 26):
                    check.ClearField('environment_profile'); old_check.ClearField('environment_profile')
                assert check == old_check, (name, entry.id)
            assert len(after.entry) == len(before.entry) + (name == 'environment_profiles' and profile_id not in prior)
            assert after.IsInitialized()
        assert copies[0][1] == copies[1][1], f'{name}: editor/client differ; reconcile before applying'
    (review / 'profile.json').write_text(json.dumps(MessageToDict(profile, preserving_proto_field_name=True), indent=2) + '\n', encoding='utf-8')
    if not args.apply:
        print(f'Validated profile {profile_id}; use --apply to save six scoped data files.')
        return
    for path, raw in original.items():
        assert path.read_bytes() == raw, f'{path} changed concurrently; rerun with fresh data'
    for name, copies in tables.items():
        for path, message in copies:
            backup = review / ('before_' + ('editor_' if 'editor' in path.parts else 'client_') + path.name)
            if not backup.exists():
                backup.write_bytes(original[path])
            path.write_bytes(message.SerializeToString())
            saved = type(message)(); saved.ParseFromString(path.read_bytes())
            assert saved == message
    print(f'Saved and verified profile {profile_id}, map 1 and zone 26; all other rows/fields preserved.')


if __name__ == '__main__':
    main()
