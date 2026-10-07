# Bug Loop "Implement as Feature" Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The maintainer can turn a rejected report (`not_a_bug` / `design_request`) into a feature with a description; the bug loop implements it like a fix and always parks the result for the maintainer's approval.

**Architecture:** A fourth decision action `implement` in the bug API, accepted only for the two rejected states. The loop handles it like a guided fix on a fresh branch, with the description as a trusted FEATURE REQUEST block that replaces the project as the source of expected behaviour; a persisted `features` set makes every later round of that bug park instead of auto-shipping. The web UI gets a "Feature request" panel and a `Feature` chip.

**Tech Stack:** Python 3 stdlib (`tools/bugs`), Node/Express/Mongoose/Jest (`H:\mmo-error-report`), React/TS/MUI (`H:\mmo-error-report-ui`).

**Spec:** `docs/superpowers/specs/2026-10-08-bug-loop-feature-requests-design.md`. Context: `docs/superpowers/specs/2026-10-07-bug-loop-decisions-design.md`, `docs/bug-loop.md`.

## Global Constraints

- Work locations: mmo in the worktree `H:\mmo\.claude\worktrees\bugloop-features` (branch `feature/bugloop-features`). API and UI in new worktrees `H:\mmo-error-report-features` and `H:\mmo-error-report-ui-features` on branch `feature/bugloop-features` from `master` (other sessions work in the main checkouts — never touch `H:\mmo`, `H:\mmo-error-report`, `H:\mmo-error-report-ui`). Never push, publish or deploy.
- Python in `tools/bugs` uses tabs; new Python/PowerShell files carry `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`; JS/TS 2-space.
- Python tests: `& $env:MMO_GATE_PYTHON -m unittest discover -s tools/tests -p "test_bug*.py"`. API: `npm test`. UI: `$env:CI = "true"; npm test -- --watchAll=false` and `npm run build` with CI unset (a pre-existing lint warning makes CI=true builds fail).
- Commit trailer exactly `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Values: action `implement`; description required, ≤ 4000 chars; eligible: status `wontfix` + `triage.category = 'not_a_bug'`, or status `triaged` + `triage.category = 'design_request'`; accepted bugs get `triage.category = 'feature'`; block label `FEATURE REQUEST`, provenance `maintainer decision via the web UI; trusted and binding`; park reason `feature: shipping needs the maintainer's approval`.
- Features never auto-ship, in any round. Abuse-flagged reports are never eligible.

## Review Focus

- An `implement` decision on an abuse-flagged, resolved or parked bug → `409`, nothing runs (Task 1).
- A feature whose implementation is fully green → still parks with the feature reason and pings; it can only reach develop through the hash-bound "ship as is" (Task 4).
- A guided refix of a feature later on → still parks, never auto-ships (Task 4).
- An implement run that fails before producing a commit → the bug goes back to `triaged` / `design_request` with a note, so the maintainer can decide `implement` again (Task 4).
- A fixer that answers `no_project_basis` for a feature → treated as a failed run (not `wontfix`), because the maintainer's description is the basis (Task 4).

---

### Task 1: Bug API — `implement` action

**Files:**
- Modify: `src/models/Bug.js`, `src/routes/bugRoutes.js`, `README.md`
- Create: `tests/bugImplement.test.js`

**Interfaces:**
- Produces: `POST /api/bugs/:id/decision` accepts `{action: 'implement', guidance}` (guidance required) only for the two eligible states; `Bug.DECISION_ACTIONS` contains `'implement'`.

- [ ] **Step 1: Worktree**

Run: `git -C H:/mmo-error-report worktree add H:/mmo-error-report-features -b feature/bugloop-features master` then `npm ci --no-audit --no-fund` in `H:/mmo-error-report-features`.

- [ ] **Step 2: Failing tests** — `tests/bugImplement.test.js`:

```js
const request = require('supertest');
const app = require('../src/app');
const Bug = require('../src/models/Bug');
const { connect, clear, close } = require('./helpers');
const { validBug } = require('./fixtures');

beforeAll(connect);
afterEach(clear);
afterAll(close);

beforeEach(() => {
  process.env.BUG_INGEST_KEYS = 'ingest-key';
  process.env.BUG_READER_KEYS = 'reader-key';
  process.env.BUG_MAINTAINER_KEYS = 'maintainer-key';
});

function seed(status, category) {
  return Bug.create({ ...validBug(), status, triage: { category }, history: [] });
}

function implement(id, guidance = 'Bandits should also call for help when the caster stands still.', key = 'maintainer-key') {
  return request(app).post(`/api/bugs/${id}/decision`).set('X-Api-Key', key).send({ action: 'implement', guidance });
}

test('implement is accepted for not_a_bug and design_request reports', async () => {
  for (const [status, category] of [['wontfix', 'not_a_bug'], ['triaged', 'design_request']]) {
    const bug = await seed(status, category);
    const res = await implement(bug._id);
    expect(res.status).toBe(200);
    expect(res.body.decision.action).toBe('implement');
    expect(res.body.history.at(-1)).toMatchObject({ actor: 'maintainer' });
    expect(res.body.history.at(-1).change).toMatch(/^decision: implement: Bandits/);
  }
});

test('implement is refused for other states, abuse flags included', async () => {
  for (const [status, category] of [['wontfix', 'abuse'], ['wontfix', 'duplicate'], ['triaged', 'defect'],
    ['pr_open', 'defect'], ['needs_decision', 'defect'], ['resolved', 'feature'], ['new', undefined]]) {
    const bug = await seed(status, category);
    expect((await implement(bug._id)).status).toBe(409);
  }
});

test('implement needs a description and the maintainer key', async () => {
  const bug = await seed('wontfix', 'not_a_bug');
  expect((await implement(bug._id, '   ')).status).toBe(400);
  expect((await implement(bug._id, 'x', 'reader-key')).status).toBe(401);
});

test('the other actions keep their parked-only rule', async () => {
  const bug = await seed('wontfix', 'not_a_bug');
  const res = await request(app).post(`/api/bugs/${bug._id}/decision`).set('X-Api-Key', 'maintainer-key').send({ action: 'discard', guidance: '' });
  expect(res.status).toBe(409);
});
```

- [ ] **Step 3: Run to verify they fail** — `npm test -- tests/bugImplement.test.js` → FAIL (`implement` is not a valid action).

- [ ] **Step 4: Implement**

`src/models/Bug.js`: `const DECISION_ACTIONS = ['refix', 'ship', 'discard', 'implement'];`

`src/routes/bugRoutes.js`, in the decision route:
- change the action error message to `'action must be refix, ship, discard or implement'`;
- replace the refix-guidance check with:

```js
    if ((action === 'refix' || action === 'implement') && !guidance) {
      return res.status(400).json({ message: action === 'refix' ? 'refix needs guidance' : 'implement needs a description' });
    }
```

- replace the status check (`if (!DECIDABLE_STATUSES.includes(bug.status))`) with:

```js
    if (action === 'implement' ? !isImplementable(bug) : !DECIDABLE_STATUSES.includes(bug.status)) {
      return res.status(409).json({ message: action === 'implement'
        ? 'Only reports rejected as not_a_bug or parked as design_request can be implemented as a feature'
        : `Bug is ${bug.status}; only parked bugs take decisions` });
    }
```

and add next to `DECIDABLE_STATUSES`:

```js
// Rejected reports the maintainer may still want as a feature. Abuse flags are never eligible.
function isImplementable(bug) {
  const category = bug.triage && bug.triage.category;
  return (bug.status === 'wontfix' && category === 'not_a_bug') || (bug.status === 'triaged' && category === 'design_request');
}
```

Keep the `diffSha256` handling unchanged (it stays required only for `ship`).

`README.md`: list `implement` among the decision actions with its eligibility rule.

- [ ] **Step 5: Full suite** — `npm test` → all pass.

- [ ] **Step 6: Commit**

```bash
git -C H:/mmo-error-report-features add src/models/Bug.js src/routes/bugRoutes.js tests/bugImplement.test.js README.md
git -C H:/mmo-error-report-features commit -m "feat: implement decision turns rejected reports into features

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Web UI — feature request panel and chip

**Files:**
- Modify: `src/services/bugService.ts`, `src/services/decision.ts`, `src/services/decision.test.ts`, `src/pages/BugDetailPage.tsx`, `src/pages/BugListPage.tsx`
- Create: `src/components/FeatureRequestPanel.tsx`

**Interfaces:**
- Consumes: `bugService.decide(id, action, guidance, diffSha256?)` (read its current signature in bugService.ts and call it accordingly for `implement`, which needs no hash).
- Produces: `DecisionAction` includes `'implement'`; `canImplement(bug)`; `isFeature(bug)`; `implementError(description)`.

- [ ] **Step 1: Worktree** — `git -C H:/mmo-error-report-ui worktree add H:/mmo-error-report-ui-features -b feature/bugloop-features master`, then `npm ci --no-audit --no-fund` there.

- [ ] **Step 2: Failing tests** — append to `src/services/decision.test.ts`:

```ts
import { canImplement, implementError, isFeature } from './decision';

test('only not_a_bug and design_request reports can be implemented', () => {
  expect(canImplement({ status: 'wontfix', triage: { category: 'not_a_bug' }, decision: null })).toBe(true);
  expect(canImplement({ status: 'triaged', triage: { category: 'design_request' }, decision: null })).toBe(true);
  expect(canImplement({ status: 'wontfix', triage: { category: 'abuse' }, decision: null })).toBe(false);
  expect(canImplement({ status: 'triaged', triage: { category: 'defect' }, decision: null })).toBe(false);
  expect(canImplement({ status: 'pr_open', triage: { category: 'not_a_bug' }, decision: null })).toBe(false);
  const pending = { action: 'implement' as const, guidance: 'x', decidedAt: 't', consumedAt: null };
  expect(canImplement({ status: 'wontfix', triage: { category: 'not_a_bug' }, decision: pending })).toBe(false);
});

test('implement needs a description within the limit', () => {
  expect(implementError('   ')).toMatch(/describe/i);
  expect(implementError('x'.repeat(4001))).toMatch(/4000/);
  expect(implementError('Bandits assist stationary casters.')).toBeNull();
});

test('features are recognised by their triage category', () => {
  expect(isFeature({ triage: { category: 'feature' } })).toBe(true);
  expect(isFeature({ triage: { category: 'defect' } })).toBe(false);
  expect(isFeature({})).toBe(false);
});
```

(Merge the new import into the file's existing import from `./decision`.)

- [ ] **Step 3: Run to verify they fail** — `$env:CI = "true"; npm test -- --watchAll=false src/services` → FAIL.

- [ ] **Step 4: Implement**

`bugService.ts`: `export type DecisionAction = 'refix' | 'ship' | 'discard' | 'implement';`

`decision.ts` — add:

```ts
// Rejected reports the maintainer may still turn into a feature. Abuse flags are never eligible.
export function canImplement(bug: { status: BugStatus; triage?: BugTriage; decision?: BugDecision | null }): boolean {
  const category = bug.triage?.category;
  const eligible = (bug.status === 'wontfix' && category === 'not_a_bug')
    || (bug.status === 'triaged' && category === 'design_request');
  return eligible && !isDecisionPending(bug);
}

export function isFeature(bug: { triage?: BugTriage }): boolean {
  return bug.triage?.category === 'feature';
}

export function implementError(description: string): string | null {
  if (!description.trim()) {
    return 'Describe the behaviour you want.';
  }
  if (description.length > 4000) {
    return 'The description is limited to 4000 characters.';
  }
  return null;
}
```

(import `BugTriage` from `./bugService` alongside the existing imports.)

`src/components/FeatureRequestPanel.tsx`:

```tsx
import React, { useState } from 'react';
import { Alert, Button, Card, CardContent, CardHeader, Stack, TextField, Typography } from '@mui/material';
import bugService, { Bug } from '../services/bugService';
import { canImplement, implementError, isDecisionPending } from '../services/decision';
import ConfirmDialog from './ui/ConfirmDialog';

interface FeatureRequestPanelProps {
  bug: Bug;
  onDecided: (updated: Bug) => void;
}

// A report the bug loop rejected as "not a bug": the maintainer can still want it as a feature.
const FeatureRequestPanel: React.FC<FeatureRequestPanelProps> = ({ bug, onDecided }) => {
  const [description, setDescription] = useState('');
  const [confirming, setConfirming] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const pending = isDecisionPending(bug) && bug.decision?.action === 'implement';
  const enabled = canImplement(bug) && !busy;

  const submit = async () => {
    setConfirming(false);
    const problem = implementError(description);
    if (problem) {
      setError(problem);
      return;
    }
    setBusy(true);
    setError(null);
    try {
      onDecided(await bugService.decide(bug._id, 'implement', description.trim()));
      setDescription('');
    } catch (err: any) {
      setError(err?.response?.data?.message || err?.message || 'The decision could not be sent.');
    } finally {
      setBusy(false);
    }
  };

  return (
    <Card>
      <CardHeader title="Feature request" slotProps={{ title: { variant: 'subtitle2' } }} />
      <CardContent sx={{ pt: 0 }}>
        <Stack spacing={2}>
          <Typography variant="body2" color="text.secondary">
            The bug loop found nothing in the project that asks for this behaviour. If you want it anyway, describe it;
            the bug loop implements it as a feature and parks the result for your approval.
          </Typography>
          {bug.triage?.summary && (
            <Typography variant="body2" sx={{ whiteSpace: 'pre-wrap' }}>{bug.triage.summary}</Typography>
          )}
          {pending && <Alert severity="info">Feature requested; the bug loop handles it on its next poll.</Alert>}
          {error && <Alert severity="error" onClose={() => setError(null)}>{error}</Alert>}
          <TextField
            label="Describe the behaviour you want"
            multiline
            minRows={3}
            fullWidth
            size="small"
            value={description}
            disabled={!enabled}
            error={description.length > 4000}
            onChange={e => setDescription(e.target.value)}
            helperText={`${description.length} / 4000`}
          />
          <Stack direction="row">
            <Button variant="contained" disabled={!enabled || !description.trim()} onClick={() => setConfirming(true)}>
              Implement as feature
            </Button>
          </Stack>
        </Stack>
      </CardContent>
      <ConfirmDialog
        open={confirming}
        title="Implement this as a feature?"
        message="The bug loop implements this as a feature. It will not ship without your approval."
        confirmLabel="Implement"
        busy={busy}
        onConfirm={submit}
        onClose={() => setConfirming(false)}
      />
    </Card>
  );
};

export default FeatureRequestPanel;
```

`BugDetailPage.tsx`: import `FeatureRequestPanel`, `Chip` (if not imported) and `canImplement`, `isDecisionPending`, `isFeature` from `../services/decision`. In the left column's `<Stack spacing={2}>`, directly after the existing DecisionPanel block, add:

```tsx
            {(canImplement(bug) || (isDecisionPending(bug) && bug.decision?.action === 'implement')) && (
              <FeatureRequestPanel bug={bug} onDecided={apply} />
            )}
            {isFeature(bug) && (
              <Box><Chip size="small" color="info" label="Feature" /></Box>
            )}
```

`BugListPage.tsx`: import `Chip` (if not imported) and `isFeature`; replace `<TableCell><StatusChip status={bug.status} /></TableCell>` with:

```tsx
                    <TableCell>
                      <StatusChip status={bug.status} />
                      {isFeature(bug) && <Chip size="small" color="info" label="Feature" sx={{ ml: 0.5 }} />}
                    </TableCell>
```

- [ ] **Step 5: Tests and build** — `$env:CI = "true"; npm test -- --watchAll=false` → pass; `Remove-Item Env:CI; npm run build` → compiles (only the pre-existing warning).

- [ ] **Step 6: Commit**

```bash
git -C H:/mmo-error-report-ui-features add src
git -C H:/mmo-error-report-ui-features commit -m "feat: feature request panel and Feature chip

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Loop — state, inputs, prompts, notifier for features

**Files:**
- Modify: `tools/bugs/bugloop/state.py`, `bugloop/inputs.py`, `bugloop/notify.py`, `bugloop/prompts/fix.md`, `bugloop/prompts/review.md`
- Test: `tools/tests/test_bug_loop_config_state.py`, `test_bug_loop_verdicts.py`, `test_bug_loop_notify.py`, `test_bug_loop_fixtures.py`

**Interfaces:**
- Produces: `LoopState.add_feature(bug_id)`, `LoopState.is_feature(bug_id) -> bool` (persisted list `features`); `inputs.build_fix_input(..., guidance=None, previous=None, feature=None)` and `inputs.build_review_input(..., guidance=None, feature=None)` adding a `FEATURE REQUEST` block with provenance `maintainer decision via the web UI; trusted and binding`; `notify.feature_ready_message(notifier, bug_id, summary, branch) -> str`.

- [ ] **Step 1: Failing tests**

`test_bug_loop_config_state.py`, class `StateTests`:

```python
	def test_features_are_remembered(self):
		state = loop_state.LoopState(self.path, "2026-10-08")
		self.assertFalse(state.is_feature("a"))
		state.add_feature("a")
		state.add_feature("a")
		state.save()
		state = loop_state.LoopState(self.path, "2026-10-09")
		self.assertTrue(state.is_feature("a"))
		self.assertEqual(state.data["features"], ["a"])
```

`test_bug_loop_verdicts.py`, class `InputTests`:

```python
	def test_feature_request_block_for_fixer_and_reviewer(self):
		fix_text = inputs.build_fix_input(BUG, verdict(), "bugfix/x", "F.json", nonce="n", feature="Bandits assist stationary casters.")
		self.assertIn("<<<BEGIN FEATURE REQUEST [maintainer decision via the web UI; trusted and binding] n>>>", fix_text)
		self.assertIn("Bandits assist stationary casters.", fix_text)
		review_text = inputs.build_review_input(verdict(), fix(), "d", [], nonce="n", feature="Bandits assist stationary casters.")
		self.assertIn("<<<BEGIN FEATURE REQUEST", review_text)
		self.assertNotIn("FEATURE REQUEST", inputs.build_fix_input(BUG, verdict(), "bugfix/x", "F.json", nonce="n"))
		self.assertNotIn("FEATURE REQUEST", inputs.build_review_input(verdict(), fix(), "d", [], nonce="n"))
```

`test_bug_loop_notify.py`, class `MessageTests`:

```python
	def test_feature_ready(self):
		notifier = notify.Notifier("", "https://ui.example")
		text = notify.feature_ready_message(notifier, "a" * 24, "Bandits ignore stationary casters", "bugfix/aaaaaaaa")
		self.assertIn("Feature ready for review", text)
		self.assertIn("https://ui.example/bugs/" + "a" * 24, text)
		self.assertIn("bugfix/aaaaaaaa", text)
```

`test_bug_loop_fixtures.py`, `PromptTests.test_prompts_carry_their_trust_rules`, add:

```python
		self.assertIn("FEATURE REQUEST", fix)
		self.assertIn("FEATURE REQUEST", self.read("review.md"))
```

- [ ] **Step 2: Run to verify they fail** — the suite command → failures in the new tests.

- [ ] **Step 3: Implement**

`state.py`: add `"features": [],` to `_DEFAULT` and:

```python
	def add_feature(self, bug_id):
		"""A maintainer-accepted feature: no round of this bug ever ships on its own."""
		if bug_id not in self.data["features"]:
			self.data["features"].append(bug_id)

	def is_feature(self, bug_id):
		return bug_id in self.data["features"]
```

`inputs.py`: add a `feature=None` keyword to `build_fix_input` (after `previous`) and to `build_review_input` (after `guidance`); in both, before the return, add:

```python
	if feature:
		parts.append(block("FEATURE REQUEST", _GUIDANCE_PROVENANCE, nonce, feature))
```

`notify.py`:

```python
def feature_ready_message(notifier, bug_id, summary, branch):
	return "**Feature ready for review** — {}\n{}\nBranch: `{}`".format(notifier.bug_link(bug_id), summary[:200], branch)
```

`prompts/fix.md` — after the "## Maintainer guidance" section:

```markdown
## Feature requests

If the input has a FEATURE REQUEST block, the maintainer accepted this report as a feature: the
block defines the expected behaviour, even though nothing in the project asked for it before. Cite
it as `expected_source` ("maintainer feature decision"). `no_project_basis` is not a valid outcome
for a feature. Implement exactly what the block describes, with a regression test for the new
behaviour. All other rules stay: never push, never edit protected paths, never weaken checks or
make rewards more generous — if the feature would need that, write outcome `no_root_cause` and
explain in `notes`.
```

`prompts/review.md` — append to the list:

```markdown
9. If a FEATURE REQUEST block is present, the maintainer accepted the report as a feature: judge
   `fixes_symptom` and `expected_source_supported` against that block (it is the expected
   behaviour), and leave `design_question` empty unless the diff goes beyond the block.
```

- [ ] **Step 4: Run the suite** → all OK.

- [ ] **Step 5: Commit**

```bash
git add tools/bugs/bugloop tools/tests
git commit -m "feat(bug-loop): feature request blocks, feature state and notification

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Loop — handle `implement` decisions; features always park

**Files:**
- Modify: `tools/bugs/bugloop/loop.py`
- Test: `tools/tests/test_bug_loop_orchestrator.py`

**Interfaces:**
- Consumes: Task 3 (`add_feature`, `is_feature`, `feature=` input blocks, `feature_ready_message`); existing `_handle_decisions`, `_fix`, `_verify_and_ship`, `_review`, `_park`, `_summary`, `_safe_release`.
- Produces: `FEATURE_REASON = "feature: shipping needs the maintainer's approval"`; `_implement(bug_id, description)`; `_fix(bug_id, guidance=None, feature=None)`; `_verify_and_ship(..., guidance=None, feature=None)`; `_review(..., guidance=None, feature=None)`; outcomes `implement-without-description`, `implement-without-artifacts`.

- [ ] **Step 1: Failing tests** — add to `class LoopTests` (the file already has `park_with_decision`, `FakeRunner.inputs`, `FakeNotifier`, `GOOD_*`, `BUG_ID`, `BRANCH`):

```python
	def rejected_with_implement(self, description="Bandits also assist stationary casters.", status="wontfix",
			category="not_a_bug", **make_kwargs):
		bug = dict(BUG, status=status, triage={"category": category},
			decision={"action": "implement", "guidance": description, "decidedAt": "t", "consumedAt": None})
		self.make(bugs=[bug], **make_kwargs)
		folder = os.path.join(self.artifacts, BUG_ID)
		os.makedirs(folder, exist_ok=True)
		for name, value in (("report.json", BUG), ("triage.json", GOOD_VERDICT)):
			with open(os.path.join(folder, name), "w", encoding="utf-8") as handle:
				json.dump(value, handle)

	def test_implement_runs_as_feature_and_always_parks(self):
		self.rejected_with_implement()
		self.loop.poll_once()
		self.assertEqual(self.api.updates[0][1].get("decisionConsumed"), True)
		self.assertTrue(self.state.is_feature(BUG_ID))
		self.assertTrue(any((fields.get("triage") or {}).get("category") == "feature" for _, fields in self.api.updates))
		fix_input = dict(self.runner.inputs)["fix"]
		self.assertIn("FEATURE REQUEST", fix_input)
		self.assertIn("Bandits also assist stationary casters.", fix_input)
		review_input = [text for kind, text in self.runner.inputs if kind == "review"][0]
		self.assertIn("FEATURE REQUEST", review_input)
		self.assertEqual(self.worktree.shipped, [])
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "pr_open")
		self.assertIn(loop.FEATURE_REASON, self.api.notes(BUG_ID)[-1])
		self.assertTrue(any("Feature ready for review" in m for m in self.notifier.messages))

	def test_implement_works_for_design_requests(self):
		self.rejected_with_implement(status="triaged", category="design_request")
		self.loop.poll_once()
		self.assertEqual(self.api.bugs[BUG_ID]["status"], "pr_open")

	def test_refix_of_a_feature_still_parks(self):
		self.park_with_decision("refix", "Also check line of sight.")
		self.state.add_feature(BUG_ID)
		self.runner.on_fix = lambda: (setattr(self.worktree, "head_", "head1"), self.worktree.branch_heads.update({BRANCH: "head1"}))
		self.loop.poll_once()
		self.assertEqual(self.worktree.shipped, [])
		self.assertIn(loop.FEATURE_REASON, self.api.notes(BUG_ID)[-1])

	def test_implement_without_description_is_refused(self):
		self.rejected_with_implement(description="   ")
		self.loop.poll_once()
		self.assertNotIn("fix", [kind for kind, _ in self.runner.inputs])
		self.assertIn("implement needs a description", self.api.notes(BUG_ID)[-1])
		self.assertIn("implement-without-description", self.outcomes())

	def test_implement_without_triage_artifacts_is_refused(self):
		self.rejected_with_implement()
		os.remove(os.path.join(self.artifacts, BUG_ID, "triage.json"))
		self.loop.poll_once()
		self.assertNotIn("fix", [kind for kind, _ in self.runner.inputs])
		self.assertIn("triage artifacts missing", self.api.notes(BUG_ID)[-1])

	def test_failed_implement_goes_back_to_design_request(self):
		self.rejected_with_implement()
		self.worktree.head_ = "base1"  # the fixer leaves no commit
		self.loop.poll_once()
		bug = self.api.bugs[BUG_ID]
		self.assertEqual(bug["status"], "triaged")
		self.assertEqual(bug["triage"]["category"], "design_request")
		self.assertIn("decide again", self.api.notes(BUG_ID)[-1])

	def test_no_project_basis_is_not_an_answer_for_a_feature(self):
		self.rejected_with_implement(fix=dict(GOOD_FIX, outcome="no_project_basis", expected_source=""))
		self.loop.poll_once()
		bug = self.api.bugs[BUG_ID]
		self.assertEqual(bug["status"], "triaged")
		self.assertEqual(bug["triage"]["category"], "design_request")

	def test_implement_ignored_in_dry_run_and_waits_for_budget(self):
		self.rejected_with_implement(dry_run=True)
		self.loop.poll_once()
		self.assertIsNone(self.api.bugs[BUG_ID]["decision"]["consumedAt"])
		self.rejected_with_implement(invocation_budget_per_day=1)
		self.loop.poll_once()
		self.assertIsNone(self.api.bugs[BUG_ID]["decision"]["consumedAt"])
```

(If `FakeWorktree.head_` defaults to a value equal to the base returned by `prepare()`, adapt `test_implement_runs_as_feature_and_always_parks` with an `on_fix` that moves `head_` and the branch head to `"head1"`, like the refix test; check the fakes first.)

- [ ] **Step 2: Run to verify they fail** — `& $env:MMO_GATE_PYTHON -m unittest discover -s tools/tests -p test_bug_loop_orchestrator.py -v` → the new tests fail.

- [ ] **Step 3: Implement** — in `loop.py`:

Constant after `MAX_REFIX`: `FEATURE_REASON = "feature: shipping needs the maintainer's approval"`.

In `_handle_decisions`, extend the budget check to `if action in ("refix", "implement") and not self.state.budget_left(...)`. After the refix-without-guidance block add:

```python
			if action == "implement" and (not guidance or not self._has_triage_artifacts(bug_id)):
				try:
					self._update(bug_id, decisionConsumed=True)
					if not guidance:
						self._update(bug_id, note="implement needs a description; decide again")
						self._finish(bug_id, "implement-without-description")
					else:
						self._update(bug_id, note="cannot implement: triage artifacts missing in artifacts/bug-loop/" + bug_id)
						self._finish(bug_id, "implement-without-artifacts")
				except Exception:
					self.log(traceback.format_exc())
				worked = True
				continue
```

and in the action dispatch add `elif action == "implement": self._implement(bug_id, guidance)`.

New methods next to `_refix`:

```python
	def _has_triage_artifacts(self, bug_id):
		folder = os.path.join(self.artifacts_dir, bug_id)
		return all(os.path.exists(os.path.join(folder, name)) for name in ("report.json", "triage.json"))

	def _implement(self, bug_id, description):
		"""The maintainer accepted a rejected report as a feature: implement it, never auto-ship it."""
		self._update(bug_id, triage={"category": "feature"}, note="accepted as a feature by the maintainer")
		self.state.add_feature(bug_id)
		self.state.save()
		self._fix(bug_id, feature=description)
```

`_fix(self, bug_id, guidance=None, feature=None)`:
- replace the `failed_status` line with:

```python
		# A guided refix that fails goes back to the maintainer, who can only decide on parked bugs;
		# a failed feature run goes back to where `implement` can be chosen again.
		failed_status = "pr_open" if guidance else "triaged"
		failed_fields = {"triage": {"category": "design_request"}} if feature else {}
```

  and pass `**failed_fields` to every `self._release(bug_id, failed_status, ...)` call in `_fix`; for feature runs append `"; decide again"` to those notes (e.g. build the note first, then `note + ("; decide again" if feature else "")`).
- the `no_project_basis` branch: when `feature` is set, treat it like the not-fixed branch (release with `failed_status`/`failed_fields`, note "the fixer found no way to implement the feature: <root cause>; decide again", outcome `needs-info`), else keep today's `wontfix` behaviour.
- feature runs always start a fresh branch (`start_branch`), exactly like a first fix; only `guidance` resumes.
- pass `feature=feature` to `inputs.build_fix_input(...)` and to `self._verify_and_ship(...)`.

`_verify_and_ship(..., guidance=None, feature=None)`: pass `feature=feature` to `self._review(...)`; after `reasons = decide(...)` add:

```python
		if self.state.is_feature(bug_id):
			green = not reasons
			reasons = reasons + [FEATURE_REASON]
		else:
			green = False
```

and after the `_park(...)` call in the `if reasons:` branch (before `return`) add:

```python
			if green:
				self.notifier.send(notify.feature_ready_message(self.notifier, bug_id, self._summary(bug_id), branch))
```

(Also add `FEATURE_REASON` in the diff-too-large early park when `self.state.is_feature(bug_id)`, so the note shows it.)

`_review(..., guidance=None, feature=None)`: pass `feature=feature` to `inputs.build_review_input(...)`.

- [ ] **Step 4: Run the suite** → all OK.

- [ ] **Step 5: Commit**

```bash
git add tools/bugs/bugloop/loop.py tools/tests/test_bug_loop_orchestrator.py
git commit -m "feat(bug-loop): implement maintainer-accepted features, never auto-ship them

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Docs, gate, handover

**Files:**
- Modify: `docs/bug-loop.md`

- [ ] **Step 1: Docs** — in "Decisions and notifications" add an "Implement as feature" bullet: eligible states, the description becomes the trusted expected behaviour, fresh branch, `Feature` category, always parks with the feature reason and a "Feature ready for review" ping, release via the hash-bound "ship as is" or refine via refix; failed runs return to `triaged`/`design_request`.

- [ ] **Step 2: Fast gate** in the mmo worktree (if `build/` is missing: `git -c protocol.file.allow=always submodule update --init`, then `. .\tools\gate\gate_worktree.ps1; Invoke-GateConfigure -Source (Get-Location).Path -MainBuild H:\mmo\build`): `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast` → exit 0.

- [ ] **Step 3: Commit** — `git add docs/bug-loop.md` and commit `docs(bug-loop): implement as feature` with the trailer.

- [ ] **Step 4: Hand over** — the user merges/deploys in the same order as before: API and UI first, then develop push, then restart the loop task.
