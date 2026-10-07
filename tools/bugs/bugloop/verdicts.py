# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Validation of what the Claude stages return: the triage verdict, the fixer's FIX.json and
the reviewer's answer. Everything here is untrusted model output until it passes."""

CATEGORIES = ("defect", "content_data", "ui", "design_request", "not_a_bug", "abuse_suspected", "duplicate")
FIXABLE = ("defect", "content_data", "ui")
SEVERITIES = ("critical", "high", "medium", "low")
OPEN_STATUSES = ("new", "triaged", "in_progress", "pr_open")
FIX_OUTCOMES = ("fixed", "no_root_cause", "no_project_basis", "not_reproducible")
CONFIDENCES = ("high", "medium", "low")
