# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Validation of what the Claude stages return: the triage verdict, the fixer's FIX.json and
the reviewer's answer. Everything here is untrusted model output until it passes."""

import json
import re

CATEGORIES = ("defect", "content_data", "ui", "design_request", "not_a_bug", "abuse_suspected", "duplicate")
FIXABLE = ("defect", "content_data", "ui")
SEVERITIES = ("critical", "high", "medium", "low")
OPEN_STATUSES = ("new", "triaged", "in_progress", "pr_open")
FIX_OUTCOMES = ("fixed", "no_root_cause", "no_project_basis", "not_reproducible")
CONFIDENCES = ("high", "medium", "low")

_SUITE = re.compile(r"[a-z0-9_]+_tests")
_SCENARIO = re.compile(r"[A-Za-z0-9_\-]+")
# A Catch2 test spec, passed as one argv entry: it must never look like an option.
_FILTER = re.compile(r"[^\-\s][^\r\n]*")


class VerdictError(ValueError):
	pass


def _text(obj, key):
	value = obj.get(key)
	if not isinstance(value, str) or not value.strip():
		raise VerdictError("missing or empty " + key)
	return value


def parse_verdict(verdict, known_ids=()):
	"""Validates a triage verdict. duplicate_of must name one of the open bugs the triage was
	shown, so a verdict cannot link an arbitrary bug."""
	if not isinstance(verdict, dict):
		raise VerdictError("verdict is not an object")
	category = verdict.get("category")
	if category not in CATEGORIES:
		raise VerdictError("invalid category {!r}".format(category))
	if verdict.get("severity") not in SEVERITIES:
		raise VerdictError("invalid severity {!r}".format(verdict.get("severity")))
	for key in ("component", "observed", "expected_claim", "reasoning"):
		_text(verdict, key)
	if category == "abuse_suspected":
		_text(verdict, "abuse_evidence")
	if category == "duplicate" and verdict.get("duplicate_of") not in known_ids:
		raise VerdictError("duplicate_of {!r} is not an open bug on this subject".format(verdict.get("duplicate_of")))
	return verdict


def parse_fix(fix):
	"""Validates FIX.json. The regression test is described, never given as a command line:
	the orchestrator builds the command itself."""
	if not isinstance(fix, dict):
		raise VerdictError("FIX.json is not an object")
	outcome = fix.get("outcome")
	if outcome not in FIX_OUTCOMES:
		raise VerdictError("invalid outcome {!r}".format(outcome))
	if fix.get("confidence") not in CONFIDENCES:
		raise VerdictError("invalid confidence {!r}".format(fix.get("confidence")))
	_text(fix, "root_cause")
	if not isinstance(fix.get("data_only"), bool):
		raise VerdictError("data_only must be a boolean")
	if outcome != "fixed":
		return fix
	_text(fix, "expected_source")
	test = fix.get("regression_test")
	if not isinstance(test, dict):
		raise VerdictError("regression_test must be an object")
	kind = test.get("kind")
	if kind == "unit":
		if not _SUITE.fullmatch(str(test.get("suite", ""))):
			raise VerdictError("unit test suite must look like <library>_tests")
		if test.get("filter") is not None and not _FILTER.fullmatch(str(test["filter"])):
			raise VerdictError("invalid unit test filter")
	elif kind == "e2e":
		if not _SCENARIO.fullmatch(str(test.get("scenario", ""))):
			raise VerdictError("invalid e2e scenario name")
	elif kind == "none":
		if not fix["data_only"]:
			raise VerdictError("only data-only fixes may come without a regression test")
	else:
		raise VerdictError("invalid regression_test kind {!r}".format(kind))
	return fix


def load_fix(path):
	try:
		with open(path, "r", encoding="utf-8-sig") as handle:
			fix = json.load(handle)
	except (OSError, ValueError) as error:
		raise VerdictError("cannot read FIX.json: {}".format(error))
	return parse_fix(fix)


def review_blockers(review):
	"""Reasons the reviewer's answer forbids auto-ship. A missing or malformed answer blocks."""
	if not isinstance(review, dict):
		return ["review unavailable: no answer"]
	if "error" in review:
		return ["review unavailable: " + str(review["error"])[:300]]
	reasons = []
	if review.get("fixes_symptom") is not True:
		reasons.append("review: the diff does not fix the stated symptom")
	if review.get("expected_source_supported") is not True:
		reasons.append("review: the cited expected source does not support the change")
	if review.get("reduces_security") is not False:
		reasons.append("review: the diff reduces a security, permission or integrity property")
	if review.get("out_of_scope_changes") is not False:
		reasons.append("review: the diff changes behaviour beyond the stated bug")
	for issue in review.get("blocking_issues") or []:
		reasons.append("review: " + str(issue)[:300])
	return reasons
