# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Room volumes and portal links derived by tools/world/wmo_rooms.py, and the .hwmo room reader/writer."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import world_fixtures as fx  # noqa: E402  (also puts tools/world on sys.path)

import wmo_rooms  # noqa: E402
from worldkit.formats.hwmo import ContainmentVolume, parse_hwmo_rooms, write_hwmo_rooms  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
IDENTITY = (1.0, 0.0, 0.0, 0.0)


def group_bytes(name, flags, lo, hi, portal_start, portal_count, refs) -> bytes:
	header = struct.pack("<iiI6fHH", -1, -1, flags, *lo, *hi, portal_start, portal_count) + b"\x00" * 28
	frmm = struct.pack("<I", len(refs))
	for mesh, position, scale in refs:
		frmm += fx.strz32(mesh) + fx.strz32("") + fx.strz32("") + struct.pack("<3f4f3fB", *position, *IDENTITY, *scale, 1)
	return fx.chunk(b"PGOM", header + fx.chunk(b"MNGM", name.encode() + b"\x00") + fx.chunk(b"FRMM", frmm))


def two_rooms(link_to_group=1) -> bytes:
	"""Room A floor x 0..10, room B floor x 10..20 (both z -5..5), a door at x = 10 in the YZ plane.
	Each room also has a wall piece far outside its floor that blows up its AABB. The door's links
	point at `link_to_group` (1 is right, 2 is a third, unrelated room)."""
	floor = "Models/Test/Floor_01.hmsh"
	wall = "Models/Test/Wall_01.hmsh"
	# Cube is 2x2x2 standing on y = 0: scale (2.5, 0.05, 2.5) gives a 5x0.1x5 tile centred on its position.
	tiles_a = [(floor, (x, 0.0, z), (2.5, 0.05, 2.5)) for x in (2.5, 7.5) for z in (-2.5, 2.5)]
	tiles_b = [(floor, (x, 0.0, z), (2.5, 0.05, 2.5)) for x in (12.5, 17.5) for z in (-2.5, 2.5)]
	tiles_c = [(floor, (x, 0.0, 40.0), (2.5, 0.05, 2.5)) for x in (2.5,)]
	door = [(10.0, 0.0, -1.0), (10.0, 0.0, 1.0), (10.0, 3.0, 1.0), (10.0, 3.0, -1.0)]
	interior = 0x2000

	# (group, [(portal, target group, side)]) - the door belongs to A and to whichever room it links
	links = {0: [(0, link_to_group, 1)], 1: [], 2: []}
	links[link_to_group].append((0, 0, -1))
	refs, ranges = b"", []
	for group in range(3):
		ranges.append((len(refs) // 8, len(links[group])))
		refs += b"".join(struct.pack("<HHhH", p, g, s, 0) for p, g, s in links[group])

	groups = (group_bytes("A", interior, (0, 0, -5), (20, 6, 5), *ranges[0], tiles_a + [(wall, (15.0, 0.0, 0.0), (1, 3, 1))])
			  + group_bytes("B", interior, (0, 0, -5), (20, 6, 5), *ranges[1], tiles_b + [(wall, (5.0, 0.0, 0.0), (1, 3, 1))])
			  + group_bytes("C", interior, (0, 0, 35), (5, 6, 45), *ranges[2], tiles_c))
	refs = struct.pack("<I", len(refs) // 8) + refs
	header =struct.pack("<8I", 0, 3, 1, 0, 0, 0, 0, 0) + struct.pack("<6f", 0, 0, -5, 20, 6, 45) + struct.pack("<II", 0, 0)
	vertices = struct.pack("<I", 4) + b"".join(struct.pack("<3f", *v) for v in door)
	info = struct.pack("<I", 1) + struct.pack("<HH4f2f4f", 0, 4, 1, 0, 0, 0, 2.0, 3.0, 0, 0.7071, 0, 0.7071)
	return (fx.chunk(b"REVM", struct.pack("<I", 0x201)) + fx.chunk(b"DHOM", header) + fx.chunk(b"VPOM", vertices)
			+ fx.chunk(b"TPOM", info) + fx.chunk(b"RPOM", refs) + groups)


class WmoRoomsTests(unittest.TestCase):
	def setUp(self):
		self._dir = tempfile.TemporaryDirectory()
		self.client = Path(self._dir.name)
		(self.client / "Models/Test").mkdir(parents=True)
		for name in ("Floor_01", "Wall_01"):
			(self.client / f"Models/Test/{name}.hmsh").write_bytes(fx.cube_hmsh())
		self._client = wmo_rooms.CLIENT
		wmo_rooms.CLIENT = self.client

	def tearDown(self):
		wmo_rooms.CLIENT = self._client
		self._dir.cleanup()

	def model(self, data: bytes) -> Path:
		path = self.client / "Models/Test/Rooms.hwmo"
		path.write_bytes(data)
		return path

	def test_writing_unchanged_rooms_reproduces_the_file(self):
		path = self.model(two_rooms())
		self.assertEqual(write_hwmo_rooms(path, parse_hwmo_rooms(path)), path.read_bytes())

	def test_shipped_dungeon_round_trips(self):
		path = REPO / "data/client/Models/Dungeon/Monastery_001.hwmo"
		if not path.exists():
			self.skipTest("data/client is not checked out")
		self.assertEqual(write_hwmo_rooms(path, parse_hwmo_rooms(path)), path.read_bytes())

	def test_volumes_follow_the_floors_not_the_walls(self):
		rooms = parse_hwmo_rooms(self.model(two_rooms()))
		# Without volumes both rooms are the same AABB: the eye is in both everywhere
		self.assertEqual(wmo_rooms.groups_at(rooms, (5.0, 1.5, 0.0)), [0, 1])

		for group, volumes in zip(rooms.groups, wmo_rooms.derive_volumes(rooms)):
			group.volumes = volumes
		self.assertEqual(len(rooms.groups[0].volumes), 1)
		self.assertEqual(wmo_rooms.groups_at(rooms, (5.0, 1.5, 0.0)), [0])
		self.assertEqual(wmo_rooms.groups_at(rooms, (15.0, 1.5, 0.0)), [1])
		self.assertEqual(wmo_rooms.groups_at(rooms, (5.0, 1.5, 7.0)), [])
		volume = rooms.groups[0].volumes[0]
		np.testing.assert_allclose(volume.bounds_min, (0.0, -1.5, -5.0), atol=1e-4)
		self.assertAlmostEqual(volume.bounds_max[0], 10.0, places=4)

	def test_written_volumes_and_links_load_back(self):
		path = self.model(two_rooms(link_to_group=2))
		rooms = parse_hwmo_rooms(path)
		for group, volumes in zip(rooms.groups, wmo_rooms.derive_volumes(rooms)):
			group.volumes = volumes
		notes = wmo_rooms.relink_portals(rooms)
		self.assertEqual(len(notes), 1)
		self.assertIn("relinked", notes[0])

		path.write_bytes(write_hwmo_rooms(path, rooms))
		loaded = parse_hwmo_rooms(path)
		self.assertEqual([(r.portal, r.group) for r in loaded.groups[0].portal_refs], [(0, 1)])
		self.assertEqual([(r.portal, r.group) for r in loaded.groups[1].portal_refs], [(0, 0)])
		self.assertEqual(loaded.groups[2].portal_refs, [])
		self.assertEqual([len(g.volumes) for g in loaded.groups], [1, 1, 1])
		self.assertEqual(wmo_rooms.relink_portals(loaded), [])

	def test_box_volume_matches_the_engine_plane_convention(self):
		volume = ContainmentVolume.box("v", (0, 0, 0), (2, 2, 2))
		self.assertTrue(volume.contains((1, 1, 1)))
		self.assertTrue(volume.contains((2, 2, 2)))
		self.assertFalse(volume.contains((2.1, 1, 1)))
		self.assertFalse(volume.contains((1, -0.1, 1)))

	def test_cells_merge_into_few_boxes(self):
		grid = np.zeros((4, 6), bool)
		grid[0:2, 0:6] = True   # a bar
		grid[2:4, 0:2] = True   # and a leg: an L
		rects = wmo_rooms.merge_cells(grid)
		covered = np.zeros_like(grid)
		for i0, j0, i1, j1 in rects:
			self.assertFalse(covered[i0:i1, j0:j1].any())
			covered[i0:i1, j0:j1] = True
		self.assertTrue((covered == grid).all())
		self.assertEqual(len(rects), 2)


if __name__ == "__main__":
	unittest.main()
