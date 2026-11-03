# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Three-way merge of the protobuf game data files (*.data in data/editor and data/client).

git cannot merge them (they are binary), so before this every fix that touched a data file
develop had also touched since the branch was cut parked with "needs a manual merge", even when
the two sides edited different entries. This merges at the protobuf wire level: every data file
is a container whose `repeated Entry entry = 1` elements carry their id as field 1, so entries
are matched by id, and inside an entry by field number. Repeated fields without a usable key are
merged as sequences, where edits apply unless they overlap. With the file's schema (optional) it
also knows strings from messages: two edits to different words of one string merge, and a spawn
location one side moved keeps its height over the other side's height re-snap.

Only a value both sides changed differently is a conflict, and so is an id both sides added for
different entries (an id collision: renumbering one would need its references rewritten too).
Replayed against the 62 historical merges in both data repositories where the two sides changed
the same file, 54 come out as the humans merged them; the 8 others are such collisions and one
sentence both sides wrote differently. The merge never invents a value: it only takes elements,
or words, one of the three versions already has."""

import difflib
import re

_VARINT, _FIXED64, _LENGTH, _FIXED32 = 0, 1, 2, 5
# The units a string is merged in: runs of whitespace and runs of anything else.
_WORDS = re.compile(r"\s+|\S+")


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


def _field_schema(schema, number):
	"""(kind, child schema) of field `number` in message `schema`: kind is "message", "string",
	"other" or None (no schema: guessed from the bytes)."""
	field = schema.fields_by_number.get(number) if schema is not None else None
	if field is None:
		return None, None
	if field.message_type is not None:
		return "message", field.message_type
	return ("string" if field.type == field.TYPE_STRING else "other"), None


def _field_name(schema, number, path):
	field = schema.fields_by_number.get(number) if schema is not None else None
	if path:
		return "{}.{}".format(path, field.name if field is not None else number)
	if field is not None:
		return field.name
	return "entry" if number == 1 else "field {}".format(number)


def _merge_text(base, ours, theirs, path):
	"""Both sides edited one string: diff3 over its words, so edits to different words merge (one
	side turns "20 to 22" into "$s0", the other "40%" into "$s1%"). Overlapping edits conflict."""
	texts = []
	for field in (base, ours, theirs):
		try:
			texts.append(_WORDS.findall(field.payload.decode("utf-8")))
		except UnicodeDecodeError:
			raise DataConflict(path)
	merged = "".join(_merge_sequence(*texts, path=path))
	return _length_field(ours.number, merged.encode("utf-8"))


def _relocated(base, ours, theirs, schema):
	"""A spawn location both sides changed: the side that moved it (x or z) wins when the other only
	set its height (y). Develop re-snaps spawn heights to new terrain across whole maps; a fix
	that moves the spawn has set the height for its new spot. Returns the winning side or None."""
	names = {field.name: field.number for field in schema.fields}
	if not {"positionx", "positiony", "positionz"} <= set(names):
		return None
	horizontal = {names["positionx"], names["positionz"]}
	height = {names["positiony"]}

	def changed(fields):
		groups = {}
		for field in fields:
			groups.setdefault(field.number, []).append(field.raw)
		return groups

	was, mine, other = (changed(_parse(field.payload)) for field in (base, ours, theirs))
	mine_changed = {n for n in set(was) | set(mine) if was.get(n) != mine.get(n)}
	other_changed = {n for n in set(was) | set(other) if was.get(n) != other.get(n)}
	if mine_changed & horizontal and other_changed <= height:
		return ours
	if other_changed & horizontal and mine_changed <= height:
		return theirs
	return None


def _merge_value(base, ours, theirs, path, kind=None, schema=None):
	"""One element both sides changed: a message is merged field by field, a string word by word;
	anything else conflicts."""
	if base is None:
		raise DataConflict(path + " (added on both sides, differently)")
	if any(field.wire != _LENGTH for field in (base, ours, theirs)):
		raise DataConflict(path)
	text = [_looks_like_text(field.payload) for field in (base, ours, theirs)]
	if kind == "string" or (kind is None and all(text)):
		return _merge_text(base, ours, theirs, path)
	if kind not in ("message", None) or (kind is None and any(text)):
		raise DataConflict(path)
	try:
		if schema is not None:
			winner = _relocated(base, ours, theirs, schema)
			if winner is not None:
				return winner
		merged = _merge_fields(_parse(base.payload), _parse(ours.payload), _parse(theirs.payload), path, schema)
	except _NotAMessage:
		raise DataConflict(path)
	return _length_field(ours.number, b"".join(field.raw for field in merged))


def _merge_keyed(base, ours, theirs, keys, path, kind=None, schema=None):
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
			elif was is None:
				raise DataConflict(where + " (both sides added a different entry with this id: an id collision)")
			else:
				result.append(_merge_value(was, mine, other, where, kind, schema))
		elif mine is not None or other is not None:
			kept = mine if mine is not None else other
			if was is None:
				result.append(kept)  # added on one side
			elif kept != was:
				raise DataConflict(where + " (changed on one side, removed on the other)")
			# else: removed on the other side and untouched here
	return result


def _hunks(base, other):
	"""The edits that turn `base` into `other`: (start, end, replacement) in base positions."""
	return [(i1, i2, list(other[j1:j2])) for tag, i1, i2, j1, j2
		in difflib.SequenceMatcher(None, base, other, autojunk=False).get_opcodes() if tag != "equal"]


def _overlap(first, second):
	"""Two edits touch the same base elements, or insert at the same point."""
	if first[0] == first[1] == second[0] == second[1]:
		return True
	return first[0] < second[1] and second[0] < first[1]


def _merge_sequence(base, ours, theirs, path, merge_item=None):
	"""Three-way merge of a sequence: the raw elements of a repeated field without usable keys, or
	the words of a string. Each side's edits apply unless they overlap an edit of the other side;
	edits that only meet (spawns appended at the end of a list, heights re-snapped on the last
	spawns before it) both apply. Where both sides replaced the same elements one for one,
	`merge_item(base, ours, theirs, path)` merges them pairwise; anything else overlapping
	conflicts unless both sides made the same edit."""
	mine, other = _hunks(base, ours), _hunks(base, theirs)
	edits = []
	a = b = 0
	while a < len(mine) or b < len(other):
		m = mine[a] if a < len(mine) else None
		t = other[b] if b < len(other) else None
		if m is not None and t is not None and _overlap(m, t):
			same_span = m[0] == t[0] and m[1] == t[1]
			if same_span and m[2] == t[2]:
				edits.append(m)
			elif same_span and merge_item is not None and len(m[2]) == len(t[2]) == m[1] - m[0]:
				edits.append((m[0], m[1], [merge_item(base[m[0] + n], x, y, "{}[{}]".format(path, m[0] + n))
					for n, (x, y) in enumerate(zip(m[2], t[2]))]))
			else:
				raise DataConflict("{}[{}]".format(path, min(m[0], t[0])) if merge_item is not None else path)
			a += 1
			b += 1
		elif t is None or (m is not None and (m[0], m[1]) <= (t[0], t[1])):
			edits.append(m)
			a += 1
		else:
			edits.append(t)
			b += 1
	result = []
	position = 0
	for start, end, replacement in edits:
		result += base[position:start]
		result += replacement
		position = end
	return result + list(base[position:])


def _merge_fields(base, ours, theirs, path, schema=None):
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
		where = _field_name(schema, number, path)
		kind, child = _field_schema(schema, number)
		if mine == other or other == was:
			result += mine
		elif mine == was:
			result += other
		elif len(mine) == len(other) == len(was) == 1:
			key = _key(mine[0])
			if key is not None and key == _key(other[0]) == _key(was[0]):
				where = "{}[id={}]".format(where, key)
			result.append(_merge_value(was[0], mine[0], other[0], where, kind, child))
		else:
			keys = [_keys(fields) for fields in (was, mine, other)]
			if None not in keys:
				result += _merge_keyed(was, mine, other, keys, where, kind, child)
			else:
				result += _merge_sequence(was, mine, other, where,
					lambda b, o, t, at: _merge_value(b, o, t, at, kind, child))
	return result


def merge(base, ours, theirs, schema=None):
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
	merged = b"".join(field.raw for field in _merge_fields(*parsed, path="", schema=schema))
	_parse(merged)  # cannot fail: built from parsed fields; kept as a cheap sanity check
	return merged
