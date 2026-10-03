# Bug Reporter — Part 4: Central Bug API + CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an authenticated `/api/bugs` API with a claim/status workflow to the existing crash-report backend, plus a Python CLI in the mmo repo to drive it.

**Architecture:** The backend (`H:\mmo-error-report`, Express 4 + Mongoose 7) gets a `Bug` model, an API-key middleware and a bug router mounted *before* the global 100 KB JSON parser so it can accept 512 KB bodies. The app is split from the server start (`src/app.js` vs `src/index.js`) so jest + supertest + mongodb-memory-server can test it in-process. The CLI `tools/bugs/bugs.py` (stdlib only) wraps the reader routes.

**Tech Stack:** Node 18+ (dev machine has 24), Express 4, Mongoose 7, jest 29, supertest 7, mongodb-memory-server 10; Python 3 stdlib (`urllib`, `argparse`, `unittest`).

**Spec:** `docs/superpowers/specs/2026-10-03-in-game-bug-reporter-design.md` ("Central API", "CLI").

## Global Constraints

- Auth header is `X-Api-Key`. Ingest keys: env `BUG_INGEST_KEYS`; reader keys: env `BUG_READER_KEYS`; both comma-separated. Constant-time comparison. No keys configured → every `/api/bugs` request is rejected with 401 (fail closed).
- Crash routes (`/api/reports`, `/api/tasks`) stay byte-for-byte unchanged in behaviour.
- `POST /api/bugs` body limit 512 KB → 413 above. Schema violations → 400 `{ message, errors: [..] }`.
- Statuses: `new | triaged | in_progress | pr_open | resolved | wontfix | duplicate`.
- Subject types: `item | spell | creature | quest | aura | object | generic`.
- Comment ≤ 1000 characters. `limit` ≤ 100 on lists.
- Claims expire after 2 h.
- All 64-bit ids (account, character, guids) travel as **decimal strings**.
- `H:\mmo-error-report` is not a git repo yet; Task 1 creates it. Never commit `.env`.
- mmo-repo source files start with `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`

## Ingest JSON contract (shared with Part 2)

The world server POSTs exactly this shape (Part 2 produces it, this part validates it):

```json
{
  "schemaVersion": 1,
  "reporter": { "accountId": "17", "characterId": "4294967321", "characterName": "Narith", "realm": "Dev Realm" },
  "subject": { "type": "item", "id": 42, "guid": "0", "name": "Runenverzierte Kupferrute" },
  "comment": "The tooltip shows the wrong sell price.",
  "client": { "version": "0.1.0.1234", "os": "Windows 11", "gpu": "NVIDIA ...", "fps": 144.0, "latencyMs": 23, "locale": "deDE", "settings": { } },
  "logTail": "....\n....",
  "server": { "character": { }, "position": { }, "subjectState": { } },
  "serverBuild": "b84e123d",
  "worldNode": "world-01"
}
```

Required: `schemaVersion` (integer ≥ 1), `reporter.characterName` (string), `subject.type` (enum), `subject.id` (integer ≥ 0), `comment` (1..1000 chars), `server` (object). Optional: everything else; `logTail` ≤ 65536 chars.

---

### Task 1: Repository baseline, app/server split, test harness

**Files (all in `H:\mmo-error-report`):**
- Create: `src/logger.js`, `src/app.js`, `jest.config.js`, `tests/helpers.js`, `tests/health.test.js`
- Modify: `src/index.js`, `package.json`

**Interfaces:**
- Produces: `require('./src/app')` → Express app without DB connect or `listen`; `tests/helpers.js` exports `connect()`, `clear()`, `close()`.

- [ ] **Step 1: Baseline commit**

```bash
cd /h/mmo-error-report
git init -b master
git add -A
git status --short | grep -E '^A  \.env$' && echo "STOP: .env staged" || true
git commit -m "chore: baseline of the deployed crash report server"
```

Expected: no "STOP" line (`.env` is already in `.gitignore`), commit created.

- [ ] **Step 2: Install test dependencies**

```bash
cd /h/mmo-error-report
npm install --save-dev jest@29 supertest@7 mongodb-memory-server@10
```

Then set the `test` script in `package.json` (keep the old manual script under a new name):

```json
  "scripts": {
    "start": "node src/index.js",
    "dev": "nodemon src/index.js",
    "test": "jest --runInBand",
    "test:manual": "node test-report.js"
  }
```

Create `jest.config.js`:

```js
module.exports = {
  testEnvironment: 'node',
  testMatch: ['**/tests/**/*.test.js'],
  testTimeout: 60000
};
```

- [ ] **Step 3: Write the failing smoke test**

`tests/helpers.js`:

```js
const mongoose = require('mongoose');
const { MongoMemoryServer } = require('mongodb-memory-server');

let mongod;

async function connect() {
  mongod = await MongoMemoryServer.create();
  await mongoose.connect(mongod.getUri());
}

async function clear() {
  for (const collection of Object.values(mongoose.connection.collections)) {
    await collection.deleteMany({});
  }
}

async function close() {
  await mongoose.disconnect();
  if (mongod) {
    await mongod.stop();
  }
}

module.exports = { connect, clear, close };
```

`tests/health.test.js`:

```js
const request = require('supertest');
const app = require('../src/app');

test('GET /health answers ok without a database', async () => {
  const res = await request(app).get('/health');
  expect(res.status).toBe(200);
  expect(res.body).toEqual({ status: 'ok' });
});
```

- [ ] **Step 4: Run it to make sure it fails**

Run: `npm test -- tests/health.test.js`
Expected: FAIL, `Cannot find module '../src/app'`.

- [ ] **Step 5: Split logger, app and server**

`src/logger.js`:

```js
const fs = require('fs');
const winston = require('winston');

if (!fs.existsSync('logs')) {
  fs.mkdirSync('logs');
}

const logger = winston.createLogger({
  level: 'info',
  format: winston.format.combine(
    winston.format.timestamp(),
    winston.format.json()
  ),
  transports: [
    new winston.transports.File({ filename: 'logs/error.log', level: 'error' }),
    new winston.transports.File({ filename: 'logs/combined.log' }),
    new winston.transports.Console({
      format: winston.format.combine(
        winston.format.colorize(),
        winston.format.simple()
      )
    })
  ]
});

module.exports = logger;
```

`src/app.js`:

```js
const express = require('express');
const cors = require('cors');
const helmet = require('helmet');
const logger = require('./logger');

const reportRoutes = require('./routes/reportRoutes');
const taskRoutes = require('./routes/taskRoutes');

const app = express();

app.use(helmet());
app.use(cors());

// Bug routes are mounted here in Task 3, BEFORE the global body parsers: they need a larger
// body limit than the global 100 KB parser allows, and they authenticate before parsing.

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

app.use('/api/reports', reportRoutes);
app.use('/api/tasks', taskRoutes);

app.get('/health', (req, res) => {
  res.status(200).json({ status: 'ok' });
});

app.use((err, req, res, next) => {
  logger.error(`${err.status || 500} - ${err.message} - ${req.originalUrl} - ${req.method} - ${req.ip}`);

  res.status(err.status || 500).json({
    message: err.message,
    error: process.env.NODE_ENV === 'development' ? err : {}
  });
});

module.exports = app;
```

Replace `src/index.js` with:

```js
const mongoose = require('mongoose');
const dotenv = require('dotenv');
const fs = require('fs');

dotenv.config();

const logger = require('./logger');
const app = require('./app');

const uploadDir = process.env.UPLOAD_DIR || './uploads';
if (!fs.existsSync(uploadDir)) {
  fs.mkdirSync(uploadDir, { recursive: true });
}

mongoose.connect(process.env.MONGODB_URI, {
  useNewUrlParser: true,
  useUnifiedTopology: true
})
.then(() => {
  logger.info('Connected to MongoDB');

  const PORT = process.env.PORT || 3000;
  app.listen(PORT, () => {
    logger.info(`Server running on port ${PORT}`);
  });
})
.catch(err => {
  logger.error('MongoDB connection error:', err);
  process.exit(1);
});

process.on('unhandledRejection', (err) => {
  logger.error('Unhandled Promise Rejection:', err);
});

module.exports = app;
```

Check that `reportRoutes.js` does not depend on `uploadDir` existing at require time: `grep -n "UPLOAD_DIR\|mkdir" src/routes/reportRoutes.js`. If multer's `diskStorage` destination callback creates/uses the directory lazily, nothing to do. If it requires the directory at module load, move the `uploadDir` creation block from `index.js` to the top of `src/app.js` instead.

- [ ] **Step 6: Run the test to verify it passes**

Run: `npm test -- tests/health.test.js`
Expected: PASS (1 test).

Also start the real server once to prove nothing regressed: `node -e "require('./src/app')"` → exits 0 without output errors.

- [ ] **Step 7: Commit**

```bash
git add package.json package-lock.json jest.config.js src/logger.js src/app.js src/index.js tests/
git commit -m "refactor: split app from server start, add jest harness"
```

---

### Task 2: API key middleware

**Files:**
- Create: `src/middleware/apiKey.js`, `tests/apiKey.test.js`

**Interfaces:**
- Produces: `requireApiKey(envVarName: string)` → Express middleware. Reads the env var **at request time** (tests set it per case).

- [ ] **Step 1: Write the failing test**

`tests/apiKey.test.js`:

```js
const express = require('express');
const request = require('supertest');
const { requireApiKey } = require('../src/middleware/apiKey');

function makeApp() {
  const app = express();
  app.get('/x', requireApiKey('TEST_KEYS'), (req, res) => res.json({ ok: true }));
  return app;
}

afterEach(() => {
  delete process.env.TEST_KEYS;
});

test('rejects everything when no keys are configured', async () => {
  const res = await request(makeApp()).get('/x').set('X-Api-Key', 'anything');
  expect(res.status).toBe(401);
});

test('rejects a missing key', async () => {
  process.env.TEST_KEYS = 'alpha';
  const res = await request(makeApp()).get('/x');
  expect(res.status).toBe(401);
});

test('rejects a wrong key', async () => {
  process.env.TEST_KEYS = 'alpha';
  const res = await request(makeApp()).get('/x').set('X-Api-Key', 'beta');
  expect(res.status).toBe(401);
});

test('accepts any key of a comma separated list, ignoring whitespace', async () => {
  process.env.TEST_KEYS = 'alpha, beta ,gamma';
  for (const key of ['alpha', 'beta', 'gamma']) {
    const res = await request(makeApp()).get('/x').set('X-Api-Key', key);
    expect(res.status).toBe(200);
  }
});

test('a key that is a prefix of a valid key is rejected', async () => {
  process.env.TEST_KEYS = 'alphabet';
  const res = await request(makeApp()).get('/x').set('X-Api-Key', 'alpha');
  expect(res.status).toBe(401);
});
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `npm test -- tests/apiKey.test.js`
Expected: FAIL, `Cannot find module '../src/middleware/apiKey'`.

- [ ] **Step 3: Implement**

`src/middleware/apiKey.js`:

```js
const crypto = require('crypto');

function parseKeys(value) {
  return (value || '')
    .split(',')
    .map(key => key.trim())
    .filter(Boolean);
}

// Hash both sides first: timingSafeEqual needs equal lengths, and hashing keeps the comparison
// constant-time regardless of how long the presented key is.
function safeEqual(a, b) {
  const ha = crypto.createHash('sha256').update(a).digest();
  const hb = crypto.createHash('sha256').update(b).digest();
  return crypto.timingSafeEqual(ha, hb);
}

/**
 * Requires a valid X-Api-Key header. Valid keys come from the comma separated environment
 * variable `envVarName`, read per request. No configured keys means nobody gets in.
 */
function requireApiKey(envVarName) {
  return (req, res, next) => {
    const keys = parseKeys(process.env[envVarName]);
    if (keys.length === 0) {
      return res.status(401).json({ message: 'API key authentication is not configured' });
    }

    const presented = req.get('X-Api-Key');
    if (!presented) {
      return res.status(401).json({ message: 'Missing API key' });
    }

    let match = false;
    for (const key of keys) {
      // No early exit: every configured key is compared.
      match = safeEqual(key, presented) || match;
    }
    if (!match) {
      return res.status(401).json({ message: 'Invalid API key' });
    }

    next();
  };
}

module.exports = { requireApiKey };
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `npm test -- tests/apiKey.test.js`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add src/middleware/apiKey.js tests/apiKey.test.js
git commit -m "feat: X-Api-Key middleware with fail-closed key lists"
```

---

### Task 3: Bug model, validation and `POST /api/bugs`

**Files:**
- Create: `src/models/Bug.js`, `src/utils/validateBug.js`, `src/routes/bugRoutes.js`, `tests/fixtures.js`, `tests/bugIngest.test.js`
- Modify: `src/app.js`

**Interfaces:**
- Consumes: `requireApiKey` (Task 2).
- Produces: `Bug` model with `Bug.STATUSES`, `Bug.SUBJECT_TYPES`; `validateBug(body) → string[]`; router mounted at `/api/bugs`; `tests/fixtures.js` exports `validBug(overrides)`.

- [ ] **Step 1: Write the failing tests**

`tests/fixtures.js`:

```js
function validBug(overrides = {}) {
  return {
    schemaVersion: 1,
    reporter: { accountId: '17', characterId: '4294967321', characterName: 'Narith', realm: 'Dev Realm' },
    subject: { type: 'item', id: 42, guid: '0', name: 'Runenverzierte Kupferrute' },
    comment: 'The tooltip shows the wrong sell price.',
    client: { version: '0.1.0.1234', os: 'Windows 11', fps: 144 },
    logTail: 'line 1\nline 2',
    server: { character: { level: 10 }, position: { map: 0 } },
    serverBuild: 'b84e123d',
    worldNode: 'world-01',
    ...overrides
  };
}

module.exports = { validBug };
```

`tests/bugIngest.test.js`:

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
});

function post(body, key = 'ingest-key') {
  return request(app).post('/api/bugs').set('X-Api-Key', key).send(body);
}

test('stores a valid report and returns its id', async () => {
  const res = await post(validBug());
  expect(res.status).toBe(201);
  expect(res.body.bugId).toMatch(/^[0-9a-f]{24}$/);

  const stored = await Bug.findById(res.body.bugId);
  expect(stored.status).toBe('new');
  expect(stored.subject.type).toBe('item');
  expect(stored.subject.id).toBe(42);
  expect(stored.server.position.map).toBe(0);
  expect(stored.history).toHaveLength(1);
  expect(stored.history[0].change).toBe('created');
});

test('the reader key cannot ingest', async () => {
  const res = await post(validBug(), 'reader-key');
  expect(res.status).toBe(401);
});

test('rejects an unknown subject type', async () => {
  const res = await post(validBug({ subject: { type: 'banana', id: 1 } }));
  expect(res.status).toBe(400);
  expect(res.body.errors.join(' ')).toMatch(/subject.type/);
});

test('rejects an empty and an overlong comment', async () => {
  expect((await post(validBug({ comment: '' }))).status).toBe(400);
  expect((await post(validBug({ comment: 'x'.repeat(1001) }))).status).toBe(400);
});

test('rejects a missing server snapshot', async () => {
  const body = validBug();
  delete body.server;
  const res = await post(body);
  expect(res.status).toBe(400);
  expect(res.body.errors.join(' ')).toMatch(/server/);
});

test('rejects bodies over 512 KB with 413', async () => {
  const res = await post(validBug({ server: { blob: 'x'.repeat(600 * 1024) } }));
  expect(res.status).toBe(413);
});

test('accepts bodies between the global 100 KB limit and 512 KB', async () => {
  const res = await post(validBug({ server: { blob: 'x'.repeat(300 * 1024) } }));
  expect(res.status).toBe(201);
});

test('crash report routes are not affected by bug auth', async () => {
  const res = await request(app).get('/api/tasks');
  expect(res.status).toBe(200);
});
```

- [ ] **Step 2: Run them to make sure they fail**

Run: `npm test -- tests/bugIngest.test.js`
Expected: FAIL, `Cannot find module '../src/models/Bug'`.

- [ ] **Step 3: Implement the model**

`src/models/Bug.js`:

```js
const mongoose = require('mongoose');

const STATUSES = ['new', 'triaged', 'in_progress', 'pr_open', 'resolved', 'wontfix', 'duplicate'];
const SUBJECT_TYPES = ['item', 'spell', 'creature', 'quest', 'aura', 'object', 'generic'];

const historySchema = new mongoose.Schema({
  at: { type: Date, default: Date.now },
  actor: { type: String, default: 'unknown' },
  change: { type: String, required: true }
}, { _id: false });

const subjectSchema = new mongoose.Schema({
  // A path literally called "type" must be declared as { type: { type: ... } }.
  type: { type: String, enum: SUBJECT_TYPES, required: true },
  id: { type: Number, default: 0 },
  guid: { type: String, default: '0' },
  name: { type: String, default: '' }
}, { _id: false });

const bugSchema = new mongoose.Schema({
  schemaVersion: { type: Number, required: true },
  createdAt: { type: Date, default: Date.now, index: true },
  reporter: {
    accountId: String,
    characterId: String,
    characterName: String,
    realm: String
  },
  subject: { type: subjectSchema, required: true },
  comment: { type: String, required: true, maxlength: 1000 },
  client: { type: mongoose.Schema.Types.Mixed, default: {} },
  logTail: { type: String, default: '' },
  server: { type: mongoose.Schema.Types.Mixed, default: {} },
  serverBuild: { type: String, default: '' },
  worldNode: { type: String, default: '' },
  // Reserved for screenshots (v2); always empty in v1.
  attachments: [{
    kind: String,
    fileName: String,
    size: Number,
    mime: String
  }],
  status: { type: String, enum: STATUSES, default: 'new', index: true },
  claimedBy: { type: String, default: null },
  claimedAt: { type: Date, default: null },
  prUrl: { type: String, default: null },
  duplicateOf: { type: mongoose.Schema.Types.ObjectId, ref: 'Bug', default: null },
  triage: {
    category: String,
    severity: String,
    component: String,
    summary: String
  },
  history: [historySchema]
}, { minimize: false });

bugSchema.index({ 'subject.type': 1, 'subject.id': 1 });

const Bug = mongoose.model('Bug', bugSchema);
Bug.STATUSES = STATUSES;
Bug.SUBJECT_TYPES = SUBJECT_TYPES;

module.exports = Bug;
```

- [ ] **Step 4: Implement validation**

`src/utils/validateBug.js`:

```js
const { SUBJECT_TYPES } = require('../models/Bug');

const MAX_COMMENT = 1000;
const MAX_LOG_TAIL = 65536;

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function optionalString(errors, value, path, max = 256) {
  if (value === undefined) {
    return;
  }
  if (typeof value !== 'string' || value.length > max) {
    errors.push(`${path} must be a string of at most ${max} characters`);
  }
}

/** Returns a list of human readable problems; empty means the body is acceptable. */
function validateBug(body) {
  const errors = [];
  if (!isObject(body)) {
    return ['body must be a JSON object'];
  }

  if (!Number.isInteger(body.schemaVersion) || body.schemaVersion < 1) {
    errors.push('schemaVersion must be an integer >= 1');
  }

  if (!isObject(body.reporter)) {
    errors.push('reporter must be an object');
  } else {
    if (typeof body.reporter.characterName !== 'string' || body.reporter.characterName.length === 0) {
      errors.push('reporter.characterName is required');
    }
    optionalString(errors, body.reporter.accountId, 'reporter.accountId', 32);
    optionalString(errors, body.reporter.characterId, 'reporter.characterId', 32);
    optionalString(errors, body.reporter.realm, 'reporter.realm');
  }

  if (!isObject(body.subject)) {
    errors.push('subject must be an object');
  } else {
    if (!SUBJECT_TYPES.includes(body.subject.type)) {
      errors.push(`subject.type must be one of ${SUBJECT_TYPES.join(', ')}`);
    }
    if (!Number.isInteger(body.subject.id) || body.subject.id < 0) {
      errors.push('subject.id must be an integer >= 0');
    }
    optionalString(errors, body.subject.guid, 'subject.guid', 32);
    optionalString(errors, body.subject.name, 'subject.name');
  }

  if (typeof body.comment !== 'string' || body.comment.trim().length === 0 || body.comment.length > MAX_COMMENT) {
    errors.push(`comment must be a non-empty string of at most ${MAX_COMMENT} characters`);
  }

  if (!isObject(body.server)) {
    errors.push('server must be an object');
  }
  if (body.client !== undefined && !isObject(body.client)) {
    errors.push('client must be an object');
  }
  optionalString(errors, body.logTail, 'logTail', MAX_LOG_TAIL);
  optionalString(errors, body.serverBuild, 'serverBuild', 64);
  optionalString(errors, body.worldNode, 'worldNode', 64);

  return errors;
}

module.exports = { validateBug };
```

- [ ] **Step 5: Implement the router (ingest only for now)**

`src/routes/bugRoutes.js`:

```js
const express = require('express');
const Bug = require('../models/Bug');
const { requireApiKey } = require('../middleware/apiKey');
const { validateBug } = require('../utils/validateBug');

const router = express.Router();

const requireIngest = requireApiKey('BUG_INGEST_KEYS');
const requireReader = requireApiKey('BUG_READER_KEYS');

// Auth runs before parsing, so unauthenticated callers never get a 512 KB body parsed.
const parseLargeJson = express.json({ limit: '512kb' });
const parseSmallJson = express.json({ limit: '32kb' });

/**
 * POST /api/bugs - ingest a bug report from a world server.
 */
router.post('/', requireIngest, parseLargeJson, async (req, res, next) => {
  try {
    const errors = validateBug(req.body);
    if (errors.length > 0) {
      return res.status(400).json({ message: 'Invalid bug report', errors });
    }

    const body = req.body;
    const bug = await Bug.create({
      schemaVersion: body.schemaVersion,
      reporter: body.reporter,
      subject: body.subject,
      comment: body.comment,
      client: body.client || {},
      logTail: body.logTail || '',
      server: body.server,
      serverBuild: body.serverBuild || '',
      worldNode: body.worldNode || '',
      history: [{ actor: body.worldNode || 'world', change: 'created' }]
    });

    res.status(201).json({ bugId: bug._id.toString() });
  } catch (error) {
    next(error);
  }
});

module.exports = router;
module.exports.requireReader = requireReader;
module.exports.parseSmallJson = parseSmallJson;
```

Mount it in `src/app.js` — replace the comment block about bug routes with:

```js
// Bug routes are mounted BEFORE the global body parsers: they need a larger body limit than the
// global 100 KB parser allows, and they authenticate before parsing.
app.use('/api/bugs', bugRoutes);
```

and add `const bugRoutes = require('./routes/bugRoutes');` below the `taskRoutes` require.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `npm test`
Expected: PASS — health (1), apiKey (5), bugIngest (8).

- [ ] **Step 7: Commit**

```bash
git add src/models/Bug.js src/utils/validateBug.js src/routes/bugRoutes.js src/app.js tests/fixtures.js tests/bugIngest.test.js
git commit -m "feat: POST /api/bugs ingest with validation and 512 KB limit"
```

---

### Task 4: List and detail routes

**Files:**
- Modify: `src/routes/bugRoutes.js`
- Create: `tests/bugRead.test.js`

**Interfaces:**
- Produces: `GET /api/bugs` → `{ bugs: Bug[] (without server, client, logTail, history), pagination: { total, page, pages } }`; `GET /api/bugs/:id` → full document or 404.

- [ ] **Step 1: Write the failing tests**

`tests/bugRead.test.js`:

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
});

async function seed(overrides) {
  return Bug.create({ ...validBug(), ...overrides, history: [] });
}

function get(path, key = 'reader-key') {
  return request(app).get(path).set('X-Api-Key', key);
}

test('the ingest key cannot read', async () => {
  expect((await get('/api/bugs', 'ingest-key')).status).toBe(401);
});

test('lists newest first without heavy fields', async () => {
  await seed({ comment: 'old', createdAt: new Date('2026-01-01') });
  await seed({ comment: 'new', createdAt: new Date('2026-02-01') });

  const res = await get('/api/bugs');
  expect(res.status).toBe(200);
  expect(res.body.bugs.map(b => b.comment)).toEqual(['new', 'old']);
  expect(res.body.bugs[0].server).toBeUndefined();
  expect(res.body.bugs[0].logTail).toBeUndefined();
  expect(res.body.bugs[0].client).toBeUndefined();
  expect(res.body.pagination).toEqual({ total: 2, page: 1, pages: 1 });
});

test('filters by status, subject and since', async () => {
  await seed({ status: 'new', subject: { type: 'item', id: 42 }, createdAt: new Date('2026-01-01') });
  await seed({ status: 'triaged', subject: { type: 'item', id: 42 }, createdAt: new Date('2026-03-01') });
  await seed({ status: 'new', subject: { type: 'spell', id: 7 }, createdAt: new Date('2026-03-01') });

  expect((await get('/api/bugs?status=new')).body.bugs).toHaveLength(2);
  expect((await get('/api/bugs?subjectType=item&subjectId=42')).body.bugs).toHaveLength(2);
  expect((await get('/api/bugs?subjectType=spell')).body.bugs).toHaveLength(1);
  expect((await get('/api/bugs?since=2026-02-01T00:00:00Z')).body.bugs).toHaveLength(2);
});

test('rejects an unknown status filter', async () => {
  expect((await get('/api/bugs?status=bogus')).status).toBe(400);
});

test('caps limit at 100 and paginates', async () => {
  for (let i = 0; i < 3; ++i) {
    await seed({ comment: `bug ${i}` });
  }
  const res = await get('/api/bugs?limit=2&page=2');
  expect(res.body.bugs).toHaveLength(1);
  expect(res.body.pagination).toEqual({ total: 3, page: 2, pages: 2 });

  const capped = await get('/api/bugs?limit=1000');
  expect(capped.status).toBe(200);
});

test('detail returns the full document', async () => {
  const bug = await seed({});
  const res = await get(`/api/bugs/${bug._id}`);
  expect(res.status).toBe(200);
  expect(res.body.server.position.map).toBe(0);
  expect(res.body.logTail).toBe('line 1\nline 2');
});

test('detail answers 404 for unknown and malformed ids', async () => {
  expect((await get('/api/bugs/0123456789abcdef01234567')).status).toBe(404);
  expect((await get('/api/bugs/not-an-id')).status).toBe(404);
});
```

- [ ] **Step 2: Run them to make sure they fail**

Run: `npm test -- tests/bugRead.test.js`
Expected: FAIL — list requests return 404 (route missing).

- [ ] **Step 3: Implement**

Add to `src/routes/bugRoutes.js` (above `module.exports`):

```js
const mongoose = require('mongoose');

const LIST_EXCLUDE = '-server -client -logTail -history -__v';

/**
 * GET /api/bugs - list bug reports, newest first.
 *
 * Query: status, subjectType, subjectId, since (ISO date), page (default 1), limit (default 20, max 100)
 */
router.get('/', requireReader, async (req, res, next) => {
  try {
    const page = Math.max(parseInt(req.query.page, 10) || 1, 1);
    const limit = Math.min(Math.max(parseInt(req.query.limit, 10) || 20, 1), 100);

    const filter = {};
    if (req.query.status && req.query.status !== 'all') {
      if (!Bug.STATUSES.includes(req.query.status)) {
        return res.status(400).json({ message: `Invalid status '${req.query.status}'` });
      }
      filter.status = req.query.status;
    }
    if (req.query.subjectType) {
      filter['subject.type'] = req.query.subjectType;
    }
    if (req.query.subjectId !== undefined) {
      filter['subject.id'] = parseInt(req.query.subjectId, 10) || 0;
    }
    if (req.query.since) {
      const since = new Date(req.query.since);
      if (isNaN(since.getTime())) {
        return res.status(400).json({ message: `Invalid since '${req.query.since}'` });
      }
      filter.createdAt = { $gte: since };
    }

    const [bugs, total] = await Promise.all([
      Bug.find(filter).sort({ createdAt: -1 }).skip((page - 1) * limit).limit(limit).select(LIST_EXCLUDE),
      Bug.countDocuments(filter)
    ]);

    res.status(200).json({ bugs, pagination: { total, page, pages: Math.ceil(total / limit) } });
  } catch (error) {
    next(error);
  }
});

async function findBug(id) {
  if (!mongoose.isValidObjectId(id)) {
    return null;
  }
  return Bug.findById(id).select('-__v');
}

/**
 * GET /api/bugs/:id - full bug report including server snapshot, client info and log tail.
 */
router.get('/:id', requireReader, async (req, res, next) => {
  try {
    const bug = await findBug(req.params.id);
    if (!bug) {
      return res.status(404).json({ message: 'Bug not found' });
    }
    res.status(200).json(bug);
  } catch (error) {
    next(error);
  }
});
```

Move the `const mongoose = require('mongoose');` line to the top of the file next to the other requires.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `npm test`
Expected: PASS (all suites).

- [ ] **Step 5: Commit**

```bash
git add src/routes/bugRoutes.js tests/bugRead.test.js
git commit -m "feat: GET /api/bugs list with filters and detail route"
```

---

### Task 5: Claim and PATCH with history

**Files:**
- Modify: `src/routes/bugRoutes.js`
- Create: `tests/bugWorkflow.test.js`

**Interfaces:**
- Produces: `POST /api/bugs/:id/claim { worker }` → 200 bug / 404 / 409 `{ message, claimedBy }`; `PATCH /api/bugs/:id { status?, triage?, prUrl?, duplicateOf?, claimedBy?: null, note?, actor? }` → 200 bug / 400 / 404.

- [ ] **Step 1: Write the failing tests**

`tests/bugWorkflow.test.js`:

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
});

function claim(id, worker) {
  return request(app).post(`/api/bugs/${id}/claim`).set('X-Api-Key', 'reader-key').send({ worker });
}

function patch(id, body) {
  return request(app).patch(`/api/bugs/${id}`).set('X-Api-Key', 'reader-key').send(body);
}

test('claim moves a new bug to in_progress', async () => {
  const bug = await Bug.create(validBug());
  const res = await claim(bug._id, 'agent-a');
  expect(res.status).toBe(200);
  expect(res.body.status).toBe('in_progress');
  expect(res.body.claimedBy).toBe('agent-a');
  expect(res.body.history.at(-1).change).toMatch(/claimed by agent-a/);
});

test('a second worker gets 409 while the claim is fresh', async () => {
  const bug = await Bug.create(validBug());
  await claim(bug._id, 'agent-a');
  const res = await claim(bug._id, 'agent-b');
  expect(res.status).toBe(409);
  expect(res.body.claimedBy).toBe('agent-a');
});

test('the same worker may re-claim', async () => {
  const bug = await Bug.create(validBug());
  await claim(bug._id, 'agent-a');
  expect((await claim(bug._id, 'agent-a')).status).toBe(200);
});

test('a claim older than two hours can be taken over', async () => {
  const bug = await Bug.create({ ...validBug(), claimedBy: 'agent-a', claimedAt: new Date(Date.now() - 3 * 3600 * 1000), status: 'in_progress' });
  const res = await claim(bug._id, 'agent-b');
  expect(res.status).toBe(200);
  expect(res.body.claimedBy).toBe('agent-b');
});

test('claim requires a worker name', async () => {
  const bug = await Bug.create(validBug());
  expect((await claim(bug._id, '')).status).toBe(400);
});

test('claim answers 404 for unknown bugs', async () => {
  expect((await claim('0123456789abcdef01234567', 'agent-a')).status).toBe(404);
});

test('patch updates status, triage and pr url and records history', async () => {
  const bug = await Bug.create(validBug());
  const res = await patch(bug._id, {
    actor: 'agent-a',
    status: 'pr_open',
    triage: { category: 'ui', severity: 'minor', component: 'tooltip', summary: 'Wrong sell price' },
    prUrl: 'https://github.com/Kyoril/mmo/pull/1',
    note: 'Price uses buy price instead of sell price'
  });
  expect(res.status).toBe(200);
  expect(res.body.status).toBe('pr_open');
  expect(res.body.triage.component).toBe('tooltip');
  expect(res.body.prUrl).toBe('https://github.com/Kyoril/mmo/pull/1');

  const changes = res.body.history.map(h => h.change);
  expect(changes).toContain('status: new -> pr_open');
  expect(changes).toContain('triage updated');
  expect(changes).toContain('prUrl: https://github.com/Kyoril/mmo/pull/1');
  expect(changes).toContain('note: Price uses buy price instead of sell price');
  expect(res.body.history.every(h => h.actor === 'agent-a')).toBe(true);
});

test('patch rejects an unknown status', async () => {
  const bug = await Bug.create(validBug());
  expect((await patch(bug._id, { status: 'bogus' })).status).toBe(400);
});

test('patch marks duplicates and validates the target id', async () => {
  const original = await Bug.create(validBug());
  const dupe = await Bug.create(validBug());
  expect((await patch(dupe._id, { duplicateOf: 'nope' })).status).toBe(400);

  const res = await patch(dupe._id, { status: 'duplicate', duplicateOf: original._id.toString() });
  expect(res.status).toBe(200);
  expect(res.body.duplicateOf).toBe(original._id.toString());
});

test('patch with claimedBy null releases the claim', async () => {
  const bug = await Bug.create(validBug());
  await claim(bug._id, 'agent-a');
  const res = await patch(bug._id, { claimedBy: null, status: 'triaged' });
  expect(res.body.claimedBy).toBeNull();
  expect(res.body.claimedAt).toBeNull();
});
```

- [ ] **Step 2: Run them to make sure they fail**

Run: `npm test -- tests/bugWorkflow.test.js`
Expected: FAIL — claim/patch return 404.

- [ ] **Step 3: Implement**

Add to `src/routes/bugRoutes.js` (above `module.exports`):

```js
const CLAIM_TTL_MS = 2 * 60 * 60 * 1000;

/**
 * POST /api/bugs/:id/claim - take a bug for processing.
 * Body: { worker: string }. 409 while another worker holds a claim younger than two hours.
 */
router.post('/:id/claim', requireReader, parseSmallJson, async (req, res, next) => {
  try {
    const worker = req.body && typeof req.body.worker === 'string' ? req.body.worker.trim() : '';
    if (!worker) {
      return res.status(400).json({ message: 'worker is required' });
    }
    if (!mongoose.isValidObjectId(req.params.id)) {
      return res.status(404).json({ message: 'Bug not found' });
    }

    const now = new Date();
    const staleBefore = new Date(now.getTime() - CLAIM_TTL_MS);

    // Atomic: two workers racing for the same bug cannot both win.
    const claimed = await Bug.findOneAndUpdate(
      {
        _id: req.params.id,
        $or: [
          { claimedBy: null },
          { claimedBy: worker },
          { claimedAt: { $lt: staleBefore } }
        ]
      },
      {
        $set: { claimedBy: worker, claimedAt: now, status: 'in_progress' },
        $push: { history: { at: now, actor: worker, change: `claimed by ${worker}` } }
      },
      { new: true }
    ).select('-__v');

    if (claimed) {
      return res.status(200).json(claimed);
    }

    const existing = await Bug.findById(req.params.id).select('claimedBy');
    if (!existing) {
      return res.status(404).json({ message: 'Bug not found' });
    }
    res.status(409).json({ message: `Already claimed by ${existing.claimedBy}`, claimedBy: existing.claimedBy });
  } catch (error) {
    next(error);
  }
});

/**
 * PATCH /api/bugs/:id - update workflow fields.
 * Body: { status?, triage?, prUrl?, duplicateOf?, claimedBy?: null, note?, actor? }
 * Every change is appended to history with the given actor.
 */
router.patch('/:id', requireReader, parseSmallJson, async (req, res, next) => {
  try {
    const bug = await findBug(req.params.id);
    if (!bug) {
      return res.status(404).json({ message: 'Bug not found' });
    }

    const body = req.body || {};
    const actor = typeof body.actor === 'string' && body.actor ? body.actor : 'unknown';
    const changes = [];

    if (body.status !== undefined) {
      if (!Bug.STATUSES.includes(body.status)) {
        return res.status(400).json({ message: `Invalid status '${body.status}'` });
      }
      if (body.status !== bug.status) {
        changes.push(`status: ${bug.status} -> ${body.status}`);
        bug.status = body.status;
      }
    }

    if (body.triage !== undefined) {
      if (body.triage === null || typeof body.triage !== 'object') {
        return res.status(400).json({ message: 'triage must be an object' });
      }
      for (const field of ['category', 'severity', 'component', 'summary']) {
        if (body.triage[field] !== undefined) {
          bug.triage[field] = String(body.triage[field]);
        }
      }
      changes.push('triage updated');
    }

    if (body.prUrl !== undefined) {
      bug.prUrl = body.prUrl;
      changes.push(`prUrl: ${body.prUrl}`);
    }

    if (body.duplicateOf !== undefined) {
      if (body.duplicateOf !== null && !mongoose.isValidObjectId(body.duplicateOf)) {
        return res.status(400).json({ message: 'duplicateOf must be a bug id' });
      }
      bug.duplicateOf = body.duplicateOf;
      changes.push(`duplicateOf: ${body.duplicateOf}`);
    }

    if (body.claimedBy === null) {
      bug.claimedBy = null;
      bug.claimedAt = null;
      changes.push('claim released');
    }

    if (typeof body.note === 'string' && body.note.trim()) {
      changes.push(`note: ${body.note.trim().slice(0, 2000)}`);
    }

    const now = new Date();
    for (const change of changes) {
      bug.history.push({ at: now, actor, change });
    }

    await bug.save();
    res.status(200).json(bug);
  } catch (error) {
    next(error);
  }
});
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `npm test`
Expected: PASS (all suites).

- [ ] **Step 5: Commit**

```bash
git add src/routes/bugRoutes.js tests/bugWorkflow.test.js
git commit -m "feat: claim and PATCH workflow for bugs with history"
```

---

### Task 6: Deployment config and docs

**Files:**
- Modify: `docker-compose.yml`, `README.md`

- [ ] **Step 1: Pass the keys into the container**

In `docker-compose.yml`, add under `services.app.environment` (after `NOTIFICATION_EMAIL`):

```yaml
      # Bug API keys (comma separated lists, rotate by adding the new key first)
      - BUG_INGEST_KEYS=${BUG_INGEST_KEYS}
      - BUG_READER_KEYS=${BUG_READER_KEYS}
```

- [ ] **Step 2: Document the API**

Append to `README.md`:

````markdown
## Bug reports (`/api/bugs`)

In-game bug reports, posted by world servers. All routes require an `X-Api-Key` header.
Configure keys as comma separated lists (no keys configured = every request is rejected):

```
BUG_INGEST_KEYS=<key used by world servers>
BUG_READER_KEYS=<key used by tools, AI agents and the web UI proxy>
```

Generate a key with `node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"`.

| Route | Key | Purpose |
|---|---|---|
| `POST /api/bugs` | ingest | store a report (≤ 512 KB), returns `{ bugId }` |
| `GET /api/bugs?status=&subjectType=&subjectId=&since=&page=&limit=` | reader | list, newest first, without heavy fields |
| `GET /api/bugs/:id` | reader | full report |
| `POST /api/bugs/:id/claim` `{ worker }` | reader | take a bug; 409 while another worker's claim is < 2 h old |
| `PATCH /api/bugs/:id` `{ status, triage, prUrl, duplicateOf, claimedBy: null, note, actor }` | reader | workflow update, appended to `history` |

Statuses: `new, triaged, in_progress, pr_open, resolved, wontfix, duplicate`.

Run the tests with `npm test` (downloads a MongoDB binary for mongodb-memory-server on first run).
````

- [ ] **Step 3: Run all tests once more**

Run: `npm test`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add docker-compose.yml README.md
git commit -m "docs: bug API keys and routes"
```

---

### Task 7: CLI `tools/bugs/bugs.py` (mmo repo)

**Files (mmo worktree):**
- Create: `tools/bugs/bugs.py`, `tools/tests/test_bugs_cli.py`

**Interfaces:**
- Produces: `BugApi(base_url: str, api_key: str, opener=urllib.request.urlopen)` with `list(status=None, subject=None, since=None, page=1, limit=20) -> dict`, `show(bug_id) -> dict`, `claim(bug_id, worker) -> dict`, `update(bug_id, **fields) -> dict`; `main(argv) -> int`. Env: `MMO_BUG_API_URL` (default `https://error.mmo-dev.net`), `MMO_BUG_API_KEY` (required).

- [ ] **Step 1: Write the failing test**

`tools/tests/test_bugs_cli.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/bugs/bugs.py, the bug API client.

No network: BugApi takes an opener, and these tests hand it a fake one that records the
request and returns a canned JSON body.

    python tools/tests/test_bugs_cli.py
"""

import importlib.util
import io
import json
import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODULE_PATH = os.path.join(REPO_ROOT, "tools", "bugs", "bugs.py")

_spec = importlib.util.spec_from_file_location("bugs", MODULE_PATH)
bugs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bugs)


class FakeResponse(io.BytesIO):
	def __enter__(self):
		return self

	def __exit__(self, *args):
		self.close()


class FakeOpener:
	def __init__(self, payload):
		self.payload = payload
		self.requests = []

	def __call__(self, request, timeout=None):
		self.requests.append(request)
		return FakeResponse(json.dumps(self.payload).encode("utf-8"))


class BugApiTests(unittest.TestCase):
	def make(self, payload=None):
		opener = FakeOpener(payload if payload is not None else {})
		return bugs.BugApi("https://example.test/", "secret", opener=opener), opener

	def test_list_builds_query_and_sends_key(self):
		api, opener = self.make({"bugs": [], "pagination": {}})
		api.list(status="new", subject="item:42", since="2026-10-01", page=2, limit=5)
		request = opener.requests[0]
		self.assertEqual(request.get_method(), "GET")
		self.assertEqual(
			request.full_url,
			"https://example.test/api/bugs?status=new&subjectType=item&subjectId=42&since=2026-10-01&page=2&limit=5")
		self.assertEqual(request.get_header("X-api-key"), "secret")

	def test_subject_without_id_filters_type_only(self):
		api, opener = self.make({"bugs": []})
		api.list(subject="generic")
		self.assertIn("subjectType=generic", opener.requests[0].full_url)
		self.assertNotIn("subjectId", opener.requests[0].full_url)

	def test_claim_posts_worker(self):
		api, opener = self.make({"_id": "x"})
		api.claim("abc", "agent-a")
		request = opener.requests[0]
		self.assertEqual(request.get_method(), "POST")
		self.assertTrue(request.full_url.endswith("/api/bugs/abc/claim"))
		self.assertEqual(json.loads(request.data), {"worker": "agent-a"})

	def test_update_sends_only_given_fields(self):
		api, opener = self.make({"_id": "x"})
		api.update("abc", status="triaged", note="looks like a data bug", actor="me")
		request = opener.requests[0]
		self.assertEqual(request.get_method(), "PATCH")
		self.assertEqual(json.loads(request.data), {"status": "triaged", "note": "looks like a data bug", "actor": "me"})

	def test_main_requires_api_key(self):
		env = dict(os.environ)
		env.pop("MMO_BUG_API_KEY", None)
		self.assertEqual(bugs.main(["list"], environ=env, out=io.StringIO()), 2)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `python tools/tests/test_bugs_cli.py`
Expected: FAIL / ERROR, `No such file or directory: ...tools/bugs/bugs.py`.

- [ ] **Step 3: Implement**

`tools/bugs/bugs.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Command line client for the central bug API (error.mmo-dev.net /api/bugs).

    python tools/bugs/bugs.py list [--status new] [--subject item:42] [--since 2026-10-01]
    python tools/bugs/bugs.py show <id>
    python tools/bugs/bugs.py claim <id> --worker <name>
    python tools/bugs/bugs.py update <id> [--status triaged] [--note "..."] [--pr <url>]

Environment: MMO_BUG_API_KEY (reader key, required), MMO_BUG_API_URL (default
https://error.mmo-dev.net). Output is JSON on stdout so agents can parse it.
"""

import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

DEFAULT_URL = "https://error.mmo-dev.net"


class BugApi:
	def __init__(self, base_url, api_key, opener=urllib.request.urlopen):
		self.base_url = base_url.rstrip("/")
		self.api_key = api_key
		self.opener = opener

	def _call(self, method, path, query=None, body=None):
		url = self.base_url + path
		if query:
			url += "?" + urllib.parse.urlencode(query)
		data = json.dumps(body).encode("utf-8") if body is not None else None
		request = urllib.request.Request(url, data=data, method=method)
		request.add_header("X-Api-Key", self.api_key)
		request.add_header("Accept", "application/json")
		if data is not None:
			request.add_header("Content-Type", "application/json")
		with self.opener(request, timeout=30) as response:
			return json.loads(response.read().decode("utf-8"))

	def list(self, status=None, subject=None, since=None, page=1, limit=20):
		query = []
		if status:
			query.append(("status", status))
		if subject:
			subject_type, _, subject_id = subject.partition(":")
			query.append(("subjectType", subject_type))
			if subject_id:
				query.append(("subjectId", subject_id))
		if since:
			query.append(("since", since))
		query.append(("page", str(page)))
		query.append(("limit", str(limit)))
		return self._call("GET", "/api/bugs", query=query)

	def show(self, bug_id):
		return self._call("GET", "/api/bugs/" + urllib.parse.quote(bug_id))

	def claim(self, bug_id, worker):
		return self._call("POST", "/api/bugs/" + urllib.parse.quote(bug_id) + "/claim", body={"worker": worker})

	def update(self, bug_id, **fields):
		body = {key: value for key, value in fields.items() if value is not None}
		return self._call("PATCH", "/api/bugs/" + urllib.parse.quote(bug_id), body=body)


def build_parser():
	parser = argparse.ArgumentParser(description="Central bug API client")
	sub = parser.add_subparsers(dest="command", required=True)

	p_list = sub.add_parser("list", help="list bugs, newest first")
	p_list.add_argument("--status")
	p_list.add_argument("--subject", help="type or type:id, e.g. item:42")
	p_list.add_argument("--since", help="ISO date")
	p_list.add_argument("--page", type=int, default=1)
	p_list.add_argument("--limit", type=int, default=20)

	p_show = sub.add_parser("show", help="full bug report")
	p_show.add_argument("id")

	p_claim = sub.add_parser("claim", help="take a bug for processing")
	p_claim.add_argument("id")
	p_claim.add_argument("--worker", required=True)

	p_update = sub.add_parser("update", help="update workflow fields")
	p_update.add_argument("id")
	p_update.add_argument("--status")
	p_update.add_argument("--note")
	p_update.add_argument("--pr", dest="prUrl")
	p_update.add_argument("--duplicate-of", dest="duplicateOf")
	p_update.add_argument("--actor", default=os.environ.get("USERNAME") or os.environ.get("USER") or "cli")
	return parser


def main(argv=None, environ=None, out=None):
	environ = os.environ if environ is None else environ
	out = sys.stdout if out is None else out
	args = build_parser().parse_args(argv)

	api_key = environ.get("MMO_BUG_API_KEY")
	if not api_key:
		print("MMO_BUG_API_KEY is not set (reader key of the bug API)", file=sys.stderr)
		return 2

	api = BugApi(environ.get("MMO_BUG_API_URL", DEFAULT_URL), api_key)
	if args.command == "list":
		result = api.list(args.status, args.subject, args.since, args.page, args.limit)
	elif args.command == "show":
		result = api.show(args.id)
	elif args.command == "claim":
		result = api.claim(args.id, args.worker)
	else:
		result = api.update(args.id, status=args.status, note=args.note, prUrl=args.prUrl,
			duplicateOf=args.duplicateOf, actor=args.actor)

	json.dump(result, out, indent=2, ensure_ascii=False)
	out.write("\n")
	return 0


if __name__ == "__main__":
	sys.exit(main())
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python tools/tests/test_bugs_cli.py`
Expected: `Ran 5 tests ... OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/bugs/bugs.py tools/tests/test_bugs_cli.py
git commit -m "feat(tools): bug API command line client"
```
