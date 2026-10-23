# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Three-way merge of the protobuf game data files (*.data in data/editor and data/client).

git cannot merge them (they are binary), so before this every fix that touched a data file
develop had also touched since the branch was cut parked with "needs a manual merge", even when
the two sides edited different entries. This merges at the protobuf wire level and needs no
schema: every data file is a container whose `repeated Entry entry = 1` elements carry their id
as field 1, so entries are matched by id, and inside an entry by field number. Repeated fields
without a usable key are merged as sequences (diff3 over the raw elements). Only a value both
sides changed differently is a conflict; the merge never invents a value, it only picks bytes
one of the three versions already has."""

import difflib

_VARINT, _FIXED64, _LENGTH, _FIXED32 = 0, 1, 2, 5


class DataConflict(Exception):
	"""Both sides changed the same value differently; str() names where."""


class _NotAMessage(Exception):
	pass


class _Field:
	__slots__ = ("number", "wire", "payload", "raw")

	def __init__(self, number, wire, payload, raw):
		self.number = number
		self.wire = wire
		self.payload = payload  # the content of a length-delimited field, else the value bytes
		self.raw = raw  # the exact bytes of tag and value, so unchanged fields are written back unchanged

	def __eq__(self, other):
		return isinstance(other, _Field) and self.raw == other.raw

	def __hash__(self):
		return hash(self.raw)


def _read_varint(data, pos):
	result = shift = 0
	while True:
		if pos >= len(data) or shift > 63:
			raise _NotAMessage()
		byte = data[pos]
		pos += 1
		result |= (byte & 0x7F) << shift
		if not byte & 0x80:
			return result, pos
		shift += 7


def _varint(value):
	out = bytearray()
	while True:
		byte = value & 0x7F
		value >>= 7
		if value:
			out.append(byte | 0x80)
		else:
			out.append(byte)
			return bytes(out)


def _parse(data):
	fields = []
	pos = 0
	while pos < len(data):
		start = pos
		tag, pos = _read_varint(data, pos)
		number, wire = tag >> 3, tag & 7
		if number == 0:
			raise _NotAMessage()
		if wire == _VARINT:
			value_start = pos
			_, pos = _read_varint(data, pos)
			payload = data[value_start:pos]
		elif wire == _FIXED64:
			payload, pos = data[pos:pos + 8], pos + 8
		elif wire == _FIXED32:
			payload, pos = data[pos:pos + 4], pos + 4
		elif wire == _LENGTH:
			length, pos = _read_varint(data, pos)
			payload, pos = data[pos:pos + length], pos + length
		else:
			raise _NotAMessage()  # groups are not used by the game data
		if pos > len(data):
			raise _NotAMessage()
		fields.append(_Field(number, wire, payload, data[start:pos]))
	return fields


def _length_field(number, payload):
	return _Field(number, _LENGTH, payload, _varint(number << 3 | _LENGTH) + _varint(len(payload)) + payload)


def _looks_like_text(payload):
	"""A string is never merged as a message: printable UTF-8 is treated as an opaque value. A
	message whose bytes are all printable is treated the same way, which only costs a conflict."""
	try:
		text = payload.decode("utf-8")
	except UnicodeDecodeError:
		return False
	return all(char.isprintable() or char in "\t\r\n" for char in text)


def _key(field):
	"""The id of an entry: its single varint field 1, or None."""
	if field.wire != _LENGTH or _looks_like_text(field.payload):
		return None
	try:
		inner = _parse(field.payload)
	except _NotAMessage:
		return None
	ids = [item for item in inner if item.number == 1]
	if len(ids) != 1 or ids[0].wire != _VARINT:
		return None
	return _read_varint(ids[0].payload, 0)[0]


def _keys(fields):
	keys = [_key(field) for field in fields]
	if None in keys or len(set(keys)) != len(keys):
		return None
	return keys


def _merge_value(base, ours, theirs, path):
	"""One element both sides changed: merge it as a message, field by field."""
	if base is None or any(field.wire != _LENGTH or _looks_like_text(field.payload) for field in (base, ours, theirs)):
		raise DataConflict(path)
	try:
		merged = _merge_fields(_parse(base.payload), _parse(ours.payload), _parse(theirs.payload), path)
	except _NotAMessage:
		raise DataConflict(path)
	return _length_field(ours.number, b"".join(field.raw for field in merged))


def _merge_keyed(base, ours, theirs, keys, path):
	b, o, t = (dict(zip(key_list, fields)) for key_list, fields in zip(keys, (base, ours, theirs)))
	order = list(keys[1]) + [key for key in keys[2] if key not in o]
	result = []
	for key in order:
		was, mine, other = b.get(key), o.get(key), t.get(key)
		where = "{}[id={}]".format(path, key)
		if mine is not None and other is not None:
			if mine == other or other == was:
				result.append(mine)
			elif mine == was:
				result.append(other)
			else:
				result.append(_merge_value(was, mine, other, where))
		elif mine is not None or other is not None:
			kept = mine if mine is not None else other
			if was is None:
				result.append(kept)  # added on one side
			elif kept != was:
				raise DataConflict(where + " (changed on one side, removed on the other)")
			# else: removed on the other side and untouched here
	return result


def _matches(base, other):
	found = {}
	for start, other_start, size in difflib.SequenceMatcher(None, base, other, autojunk=False).get_matching_blocks():
		for offset in range(size):
			found[start + offset] = other_start + offset
	return found


def _merge_sequence(base, ours, theirs, path):
	"""diff3 over the raw elements of a repeated field without usable keys."""
	in_ours, in_theirs = _matches(base, ours), _matches(base, theirs)
	result = []
	i = j = k = 0
	while True:
		anchor = next((x for x in range(i, len(base)) if x in in_ours and x in in_theirs), None)
		if anchor is None:
			ends = len(base), len(ours), len(theirs)
		else:
			ends = anchor, in_ours[anchor], in_theirs[anchor]
		was, mine, other = base[i:ends[0]], ours[j:ends[1]], theirs[k:ends[2]]
		if mine == was or mine == other:
			result += other
		elif other == was:
			result += mine
		elif len(was) == len(mine) == len(other):
			result += [_merge_value(b, o, t, "{}[{}]".format(path, j + n)) for n, (b, o, t) in enumerate(zip(was, mine, other))]
		else:
			raise DataConflict("{}[{}]".format(path, j))
		if anchor is None:
			return result
		result.append(base[anchor])
		i, j, k = anchor + 1, ends[1] + 1, ends[2] + 1


def _merge_fields(base, ours, theirs, path):
	groups = []
	for fields in (base, ours, theirs):
		grouped = {}
		for field in fields:
			grouped.setdefault(field.number, []).append(field)
		groups.append(grouped)
	b, o, t = groups
	numbers = list(o) + [number for number in t if number not in o]
	result = []
	for number in numbers:
		was, mine, other = b.get(number, []), o.get(number, []), t.get(number, [])
		if path:
			where = "{}.{}".format(path, number)
		else:
			where = "entry" if number == 1 else "field {}".format(number)
		if mine == other or other == was:
			result += mine
		elif mine == was:
			result += other
		elif len(mine) == len(other) == len(was) == 1:
			key = _key(mine[0])
			if key is not None and key == _key(other[0]) == _key(was[0]):
				where = "{}[id={}]".format(where, key)
			result.append(_merge_value(was[0], mine[0], other[0], where))
		else:
			keys = [_keys(fields) for fields in (was, mine, other)]
			if None not in keys:
				result += _merge_keyed(was, mine, other, keys, where)
			else:
				result += _merge_sequence(was, mine, other, where)
	return result


def merge(base, ours, theirs):
	"""The three-way merge of one data file. `base` is None for a file both sides added. Returns
	the merged bytes; raises DataConflict when both sides changed the same value differently and
	ValueError when a version is not a protobuf message."""
	if ours == theirs:
		return ours
	if base is not None and ours == base:
		return theirs
	if base is not None and theirs == base:
		return ours
	try:
		parsed = [_parse(data) for data in (base or b"", ours, theirs)]
	except _NotAMessage:
		raise ValueError("not a protobuf message")
	merged = b"".join(field.raw for field in _merge_fields(*parsed, path=""))
	_parse(merged)  # cannot fail: built from parsed fields; kept as a cheap sanity check
	return merged
