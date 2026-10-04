# Bug Reporter — Part 5: Web UI Bug Views Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bug list and a bug detail/edit view to the error-report web UI, served behind an nginx proxy that injects the reader API key and protects the site with HTTP basic auth.

**Architecture:** The React app (`H:\mmo-error-report-ui`, CRA 5 + React 19 + MUI 7 + TypeScript) calls **same-origin** `/api/bugs*` without any credentials. In production the UI container's nginx proxies those calls to the backend and adds `X-Api-Key` from the container environment (nginx `templates/` + `envsubst`). In development `src/setupProxy.js` does the same from a Node-side env var. The API key therefore never reaches the browser bundle. The bug pages are not gated on the MSAL login (basic auth protects the whole site).

**Tech Stack:** React 19, react-router 7, MUI 7, axios, TypeScript 4.9, CRA jest; nginx:alpine template substitution; http-proxy-middleware (dev only).

**Spec:** `docs/superpowers/specs/2026-10-03-in-game-bug-reporter-design.md` ("Web UI").

**Depends on:** Part 4 (routes and response shapes of `/api/bugs`).

## Global Constraints

- The reader key must never be referenced from `src/` code or any `REACT_APP_*` variable.
- Never commit `.env` or `build/`.
- API shapes (from Part 4): list `{ bugs: BugSummary[], pagination: { total, page, pages } }`; detail = full bug; PATCH body `{ status?, triage?, prUrl?, duplicateOf?, claimedBy?: null, note?, actor? }`.
- Statuses: `new, triaged, in_progress, pr_open, resolved, wontfix, duplicate`. Subject types: `item, spell, creature, quest, aura, object, generic`.

---

### Task 1: Baseline commit

**Files:** none changed.

- [ ] **Step 1: Inspect what will be committed**

```bash
cd /h/mmo-error-report-ui
git status --short
```

Expected: modified `package.json`, `package-lock.json`, `src/App.tsx`; untracked `.env`, `Dockerfile`, `docker-compose.yml`, `nginx.conf`, `src/auth/`, `src/components/`, `src/context/`, `src/pages/`, `src/services/`.

- [ ] **Step 2: Ignore `.env`, then commit the baseline**

Append to `.gitignore`:

```
# local environment (API URLs, client ids)
.env
```

```bash
git add .gitignore package.json package-lock.json src Dockerfile docker-compose.yml nginx.conf
git status --short | grep -E '\.env$|^A  build/' && echo "STOP: secret or build output staged" || true
git commit -m "chore: baseline of the deployed error report UI"
```

Expected: no "STOP" line.

---

### Task 2: Bug service with query builder

**Files:**
- Create: `src/services/bugService.ts`, `src/services/bugService.test.ts`

**Interfaces:**
- Produces:
  - types `BugStatus`, `SubjectType`, `BugSummary`, `Bug`, `BugListResponse`, `BugFilter`, `BugPatch`
  - `BUG_STATUSES: BugStatus[]`, `SUBJECT_TYPES: SubjectType[]`
  - `buildBugQuery(filter: BugFilter): string`
  - default export with `list(filter) → Promise<BugListResponse>`, `get(id) → Promise<Bug>`, `update(id, patch: BugPatch) → Promise<Bug>`

- [ ] **Step 1: Write the failing test**

`src/services/bugService.test.ts`:

```ts
import { buildBugQuery } from './bugService';

test('empty filter only carries paging', () => {
  expect(buildBugQuery({ page: 1, limit: 20 })).toBe('page=1&limit=20');
});

test('all filters are encoded in a stable order', () => {
  expect(buildBugQuery({ status: 'new', subjectType: 'item', subjectId: '42', page: 2, limit: 50 }))
    .toBe('status=new&subjectType=item&subjectId=42&page=2&limit=50');
});

test('"all" status and empty strings are left out', () => {
  expect(buildBugQuery({ status: 'all', subjectType: '', subjectId: '', page: 1, limit: 20 }))
    .toBe('page=1&limit=20');
});
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `CI=true npx react-scripts test --watchAll=false src/services/bugService.test.ts`
Expected: FAIL, `Cannot find module './bugService'`.

- [ ] **Step 3: Implement**

`src/services/bugService.ts`:

```ts
import axios from 'axios';

// Same-origin on purpose: nginx (production) and src/setupProxy.js (development) forward these
// calls to the bug API and add the reader key server side. No credentials live in this bundle.
const BUGS_URL = '/api/bugs';

export type BugStatus = 'new' | 'triaged' | 'in_progress' | 'pr_open' | 'resolved' | 'wontfix' | 'duplicate';
export type SubjectType = 'item' | 'spell' | 'creature' | 'quest' | 'aura' | 'object' | 'generic';

export const BUG_STATUSES: BugStatus[] = ['new', 'triaged', 'in_progress', 'pr_open', 'resolved', 'wontfix', 'duplicate'];
export const SUBJECT_TYPES: SubjectType[] = ['item', 'spell', 'creature', 'quest', 'aura', 'object', 'generic'];

export interface BugSubject {
  type: SubjectType;
  id: number;
  guid: string;
  name: string;
}

export interface BugReporter {
  accountId?: string;
  characterId?: string;
  characterName?: string;
  realm?: string;
}

export interface BugTriage {
  category?: string;
  severity?: string;
  component?: string;
  summary?: string;
}

export interface BugHistoryEntry {
  at: string;
  actor: string;
  change: string;
}

export interface BugSummary {
  _id: string;
  createdAt: string;
  reporter: BugReporter;
  subject: BugSubject;
  comment: string;
  serverBuild: string;
  worldNode: string;
  status: BugStatus;
  claimedBy: string | null;
  prUrl: string | null;
  triage?: BugTriage;
}

export interface Bug extends BugSummary {
  schemaVersion: number;
  client: Record<string, unknown>;
  server: Record<string, unknown>;
  logTail: string;
  claimedAt: string | null;
  duplicateOf: string | null;
  history: BugHistoryEntry[];
}

export interface BugListResponse {
  bugs: BugSummary[];
  pagination: { total: number; page: number; pages: number };
}

export interface BugFilter {
  status?: BugStatus | 'all' | '';
  subjectType?: SubjectType | '';
  subjectId?: string;
  page: number;
  limit: number;
}

export interface BugPatch {
  status?: BugStatus;
  triage?: BugTriage;
  prUrl?: string;
  note?: string;
  actor?: string;
  claimedBy?: null;
}

export function buildBugQuery(filter: BugFilter): string {
  const params = new URLSearchParams();
  if (filter.status && filter.status !== 'all') {
    params.append('status', filter.status);
  }
  if (filter.subjectType) {
    params.append('subjectType', filter.subjectType);
  }
  if (filter.subjectId) {
    params.append('subjectId', filter.subjectId);
  }
  params.append('page', String(filter.page));
  params.append('limit', String(filter.limit));
  return params.toString();
}

const bugService = {
  async list(filter: BugFilter): Promise<BugListResponse> {
    const response = await axios.get<BugListResponse>(`${BUGS_URL}?${buildBugQuery(filter)}`);
    return response.data;
  },

  async get(id: string): Promise<Bug> {
    const response = await axios.get<Bug>(`${BUGS_URL}/${encodeURIComponent(id)}`);
    return response.data;
  },

  async update(id: string, patch: BugPatch): Promise<Bug> {
    const response = await axios.patch<Bug>(`${BUGS_URL}/${encodeURIComponent(id)}`, patch);
    return response.data;
  }
};

export default bugService;
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `CI=true npx react-scripts test --watchAll=false src/services/bugService.test.ts`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/services/bugService.ts src/services/bugService.test.ts
git commit -m "feat: bug API service with same-origin calls"
```

---

### Task 3: Bug list page

**Files:**
- Create: `src/pages/BugListPage.tsx`
- Modify: `src/App.tsx`, `src/components/Navbar.tsx`

**Interfaces:**
- Consumes: `bugService.list`, `BUG_STATUSES`, `SUBJECT_TYPES`, `BugSummary` (Task 2).
- Produces: route `/bugs`; rows navigate to `/bugs/:id`.

- [ ] **Step 1: Implement the page**

`src/pages/BugListPage.tsx`:

```tsx
import React, { useEffect, useState } from 'react';
import {
  Alert, Box, Chip, CircularProgress, Container, FormControl, InputLabel, MenuItem, Paper, Select,
  Table, TableBody, TableCell, TableContainer, TableHead, TablePagination, TableRow, TextField, Typography
} from '@mui/material';
import { useNavigate } from 'react-router-dom';
import moment from 'moment';
import bugService, { BUG_STATUSES, SUBJECT_TYPES, BugFilter, BugSummary } from '../services/bugService';

export const STATUS_COLORS: Record<string, 'default' | 'primary' | 'secondary' | 'success' | 'warning' | 'error' | 'info'> = {
  new: 'error',
  triaged: 'warning',
  in_progress: 'info',
  pr_open: 'secondary',
  resolved: 'success',
  wontfix: 'default',
  duplicate: 'default'
};

const BugListPage: React.FC = () => {
  const navigate = useNavigate();
  const [filter, setFilter] = useState<BugFilter>({ status: 'new', subjectType: '', subjectId: '', page: 1, limit: 25 });
  const [bugs, setBugs] = useState<BugSummary[]>([]);
  const [total, setTotal] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    bugService.list(filter)
      .then(response => {
        if (!cancelled) {
          setBugs(response.bugs);
          setTotal(response.pagination.total);
          setError(null);
        }
      })
      .catch(err => {
        if (!cancelled) {
          setError(`Failed to load bugs: ${err.message}`);
          setBugs([]);
        }
      })
      .finally(() => {
        if (!cancelled) {
          setLoading(false);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [filter]);

  const updateFilter = (changes: Partial<BugFilter>) => setFilter(prev => ({ ...prev, page: 1, ...changes }));

  return (
    <Container maxWidth="xl">
      <Typography variant="h4" gutterBottom>Bug Reports</Typography>

      <Box display="flex" gap={2} mb={2}>
        <FormControl size="small" sx={{ minWidth: 160 }}>
          <InputLabel>Status</InputLabel>
          <Select label="Status" value={filter.status || 'all'} onChange={e => updateFilter({ status: e.target.value as BugFilter['status'] })}>
            <MenuItem value="all">all</MenuItem>
            {BUG_STATUSES.map(s => <MenuItem key={s} value={s}>{s}</MenuItem>)}
          </Select>
        </FormControl>
        <FormControl size="small" sx={{ minWidth: 160 }}>
          <InputLabel>Subject</InputLabel>
          <Select label="Subject" value={filter.subjectType || ''} onChange={e => updateFilter({ subjectType: e.target.value as BugFilter['subjectType'] })}>
            <MenuItem value="">any</MenuItem>
            {SUBJECT_TYPES.map(t => <MenuItem key={t} value={t}>{t}</MenuItem>)}
          </Select>
        </FormControl>
        <TextField size="small" label="Subject id" value={filter.subjectId}
          onChange={e => updateFilter({ subjectId: e.target.value.replace(/[^0-9]/g, '') })} />
      </Box>

      {error && <Alert severity="error" sx={{ mb: 2 }}>{error}</Alert>}

      <TableContainer component={Paper}>
        <Table size="small">
          <TableHead>
            <TableRow>
              <TableCell>Created</TableCell>
              <TableCell>Status</TableCell>
              <TableCell>Subject</TableCell>
              <TableCell>Comment</TableCell>
              <TableCell>Character</TableCell>
              <TableCell>Claimed by</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {loading ? (
              <TableRow><TableCell colSpan={6} align="center"><CircularProgress size={24} /></TableCell></TableRow>
            ) : bugs.length === 0 ? (
              <TableRow><TableCell colSpan={6} align="center">No bugs match the filter.</TableCell></TableRow>
            ) : bugs.map(bug => (
              <TableRow key={bug._id} hover sx={{ cursor: 'pointer' }} onClick={() => navigate(`/bugs/${bug._id}`)}>
                <TableCell>{moment(bug.createdAt).format('YYYY-MM-DD HH:mm')}</TableCell>
                <TableCell><Chip size="small" label={bug.status} color={STATUS_COLORS[bug.status]} /></TableCell>
                <TableCell>{bug.subject.type}{bug.subject.id ? ` #${bug.subject.id}` : ''} {bug.subject.name}</TableCell>
                <TableCell sx={{ maxWidth: 480, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{bug.comment}</TableCell>
                <TableCell>{bug.reporter?.characterName} ({bug.reporter?.realm})</TableCell>
                <TableCell>{bug.claimedBy || ''}</TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </TableContainer>
      <TablePagination
        component="div"
        count={total}
        page={filter.page - 1}
        rowsPerPage={filter.limit}
        rowsPerPageOptions={[25, 50, 100]}
        onPageChange={(_e, page) => setFilter(prev => ({ ...prev, page: page + 1 }))}
        onRowsPerPageChange={e => updateFilter({ limit: parseInt(e.target.value, 10) })}
      />
    </Container>
  );
};

export default BugListPage;
```

- [ ] **Step 2: Route and navigation**

In `src/App.tsx` add the import below `ReportDetailPage`:

```tsx
import BugListPage from './pages/BugListPage';
```

and the route above the `*` route:

```tsx
                <Route path="/bugs" element={<BugListPage />} />
```

In `src/components/Navbar.tsx`, insert after the title `Typography` (before the `isAuthenticated ?` block):

```tsx
        <Button color="inherit" onClick={() => navigate('/')}>Crashes</Button>
        <Button color="inherit" onClick={() => navigate('/bugs')} sx={{ mr: 2 }}>Bugs</Button>
```

- [ ] **Step 3: Type-check**

Run: `npx tsc --noEmit -p .`
Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add src/pages/BugListPage.tsx src/App.tsx src/components/Navbar.tsx
git commit -m "feat: bug list page with status and subject filters"
```

---

### Task 4: Bug detail page with workflow editing

**Files:**
- Create: `src/pages/BugDetailPage.tsx`
- Modify: `src/App.tsx`

**Interfaces:**
- Consumes: `bugService.get`, `bugService.update`, `Bug`, `BUG_STATUSES` (Task 2); `STATUS_COLORS` (Task 3).
- Produces: route `/bugs/:id`.

- [ ] **Step 1: Implement the page**

`src/pages/BugDetailPage.tsx`:

```tsx
import React, { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import {
  Accordion, AccordionDetails, AccordionSummary, Alert, Box, Button, Chip, CircularProgress, Container,
  FormControl, Grid, InputLabel, Link, List, ListItem, ListItemText, MenuItem, Paper, Select, TextField, Typography
} from '@mui/material';
import { ExpandMore } from '@mui/icons-material';
import moment from 'moment';
import bugService, { BUG_STATUSES, Bug, BugStatus } from '../services/bugService';
import { STATUS_COLORS } from './BugListPage';

const JsonBlock: React.FC<{ value: unknown }> = ({ value }) => (
  <Box component="pre" sx={{ m: 0, p: 1, bgcolor: '#f7f7f7', overflow: 'auto', maxHeight: 600, fontSize: 12 }}>
    {JSON.stringify(value, null, 2)}
  </Box>
);

const BugDetailPage: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const navigate = useNavigate();
  const [bug, setBug] = useState<Bug | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [status, setStatus] = useState<BugStatus>('new');
  const [component, setComponent] = useState('');
  const [severity, setSeverity] = useState('');
  const [prUrl, setPrUrl] = useState('');
  const [note, setNote] = useState('');

  const apply = (loaded: Bug) => {
    setBug(loaded);
    setStatus(loaded.status);
    setComponent(loaded.triage?.component || '');
    setSeverity(loaded.triage?.severity || '');
    setPrUrl(loaded.prUrl || '');
  };

  useEffect(() => {
    if (!id) {
      return;
    }
    bugService.get(id).then(apply).catch(err => setError(`Failed to load bug: ${err.message}`));
  }, [id]);

  const save = async () => {
    if (!bug) {
      return;
    }
    setSaving(true);
    try {
      const updated = await bugService.update(bug._id, {
        actor: 'web-ui',
        status: status !== bug.status ? status : undefined,
        triage: (component !== (bug.triage?.component || '') || severity !== (bug.triage?.severity || ''))
          ? { component, severity } : undefined,
        prUrl: prUrl !== (bug.prUrl || '') ? prUrl : undefined,
        note: note.trim() ? note.trim() : undefined
      });
      apply(updated);
      setNote('');
      setError(null);
    } catch (err: any) {
      setError(`Failed to save: ${err.message}`);
    } finally {
      setSaving(false);
    }
  };

  const release = async () => {
    if (!bug) {
      return;
    }
    apply(await bugService.update(bug._id, { actor: 'web-ui', claimedBy: null }));
  };

  if (error && !bug) {
    return <Container><Alert severity="error">{error}</Alert></Container>;
  }
  if (!bug) {
    return <Container><CircularProgress /></Container>;
  }

  return (
    <Container maxWidth="xl">
      <Button onClick={() => navigate('/bugs')} sx={{ mb: 2 }}>&larr; Back to list</Button>
      <Typography variant="h5" gutterBottom>
        {bug.subject.type}{bug.subject.id ? ` #${bug.subject.id}` : ''} {bug.subject.name}{' '}
        <Chip size="small" label={bug.status} color={STATUS_COLORS[bug.status]} />
      </Typography>
      {error && <Alert severity="error" sx={{ mb: 2 }}>{error}</Alert>}

      <Grid container spacing={2}>
        <Grid size={{ xs: 12, md: 8 }}>
          <Paper sx={{ p: 2, mb: 2 }}>
            <Typography variant="subtitle2">Comment</Typography>
            <Typography sx={{ whiteSpace: 'pre-wrap' }}>{bug.comment}</Typography>
          </Paper>

          <Accordion defaultExpanded>
            <AccordionSummary expandIcon={<ExpandMore />}>Server snapshot</AccordionSummary>
            <AccordionDetails><JsonBlock value={bug.server} /></AccordionDetails>
          </Accordion>
          <Accordion>
            <AccordionSummary expandIcon={<ExpandMore />}>Client</AccordionSummary>
            <AccordionDetails><JsonBlock value={bug.client} /></AccordionDetails>
          </Accordion>
          <Accordion>
            <AccordionSummary expandIcon={<ExpandMore />}>Client log tail</AccordionSummary>
            <AccordionDetails>
              <Box component="pre" sx={{ m: 0, p: 1, bgcolor: '#f7f7f7', overflow: 'auto', maxHeight: 600, fontSize: 12 }}>
                {bug.logTail}
              </Box>
            </AccordionDetails>
          </Accordion>
        </Grid>

        <Grid size={{ xs: 12, md: 4 }}>
          <Paper sx={{ p: 2, mb: 2 }}>
            <Typography variant="subtitle2" gutterBottom>Report</Typography>
            <Typography variant="body2">Created: {moment(bug.createdAt).format('YYYY-MM-DD HH:mm:ss')}</Typography>
            <Typography variant="body2">Character: {bug.reporter?.characterName} (id {bug.reporter?.characterId}, account {bug.reporter?.accountId})</Typography>
            <Typography variant="body2">Realm: {bug.reporter?.realm} / {bug.worldNode}</Typography>
            <Typography variant="body2">Server build: {bug.serverBuild}</Typography>
            <Typography variant="body2">Claimed by: {bug.claimedBy || '-'}{bug.claimedAt ? ` since ${moment(bug.claimedAt).fromNow()}` : ''}</Typography>
            {bug.prUrl && <Typography variant="body2">PR: <Link href={bug.prUrl} target="_blank" rel="noreferrer">{bug.prUrl}</Link></Typography>}
          </Paper>

          <Paper sx={{ p: 2, mb: 2 }}>
            <Typography variant="subtitle2" gutterBottom>Workflow</Typography>
            <FormControl fullWidth size="small" sx={{ mb: 1 }}>
              <InputLabel>Status</InputLabel>
              <Select label="Status" value={status} onChange={e => setStatus(e.target.value as BugStatus)}>
                {BUG_STATUSES.map(s => <MenuItem key={s} value={s}>{s}</MenuItem>)}
              </Select>
            </FormControl>
            <TextField fullWidth size="small" label="Component" value={component} onChange={e => setComponent(e.target.value)} sx={{ mb: 1 }} />
            <TextField fullWidth size="small" label="Severity" value={severity} onChange={e => setSeverity(e.target.value)} sx={{ mb: 1 }} />
            <TextField fullWidth size="small" label="PR URL" value={prUrl} onChange={e => setPrUrl(e.target.value)} sx={{ mb: 1 }} />
            <TextField fullWidth size="small" label="Note" multiline minRows={2} value={note} onChange={e => setNote(e.target.value)} sx={{ mb: 1 }} />
            <Box display="flex" gap={1}>
              <Button variant="contained" onClick={save} disabled={saving}>Save</Button>
              {bug.claimedBy && <Button onClick={release}>Release claim</Button>}
            </Box>
          </Paper>

          <Paper sx={{ p: 2 }}>
            <Typography variant="subtitle2">History</Typography>
            <List dense>
              {[...bug.history].reverse().map((h, i) => (
                <ListItem key={i} disableGutters>
                  <ListItemText primary={h.change} secondary={`${moment(h.at).format('YYYY-MM-DD HH:mm')} · ${h.actor}`} />
                </ListItem>
              ))}
            </List>
          </Paper>
        </Grid>
      </Grid>
    </Container>
  );
};

export default BugDetailPage;
```

Note: MUI 7's `Grid` uses the `size` prop (the v2 grid). If `npx tsc` reports `size` as unknown, check `node_modules/@mui/material/package.json` version — on MUI 6 or older import `Grid2` from `@mui/material/Grid2` instead.

- [ ] **Step 2: Route**

In `src/App.tsx` add `import BugDetailPage from './pages/BugDetailPage';` and below the `/bugs` route:

```tsx
                <Route path="/bugs/:id" element={<BugDetailPage />} />
```

- [ ] **Step 3: Type-check and run the unit tests**

Run: `npx tsc --noEmit -p .`
Expected: no errors.
Run: `CI=true npx react-scripts test --watchAll=false src/services`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/pages/BugDetailPage.tsx src/App.tsx
git commit -m "feat: bug detail page with snapshot views and workflow editing"
```

---

### Task 5: Key-injecting proxies (dev and production) with basic auth

**Files:**
- Create: `src/setupProxy.js`, `nginx/default.conf.template`, `nginx/README.md`
- Delete: `nginx.conf`
- Modify: `Dockerfile`, `docker-compose.yml`, `package.json` (devDependency)

- [ ] **Step 1: Development proxy**

```bash
npm install --save-dev http-proxy-middleware@2
```

`src/setupProxy.js` (CRA loads this in `npm start` only; it runs in Node, never in the browser):

```js
const { createProxyMiddleware } = require('http-proxy-middleware');

// Development only. BUG_API_TARGET / BUG_READER_KEY come from the shell, NOT from .env with a
// REACT_APP_ prefix, so the key never ends up in the bundle.
module.exports = function (app) {
  app.use(
    '/api/bugs',
    createProxyMiddleware({
      target: process.env.BUG_API_TARGET || 'http://localhost:3000',
      changeOrigin: true,
      onProxyReq: (proxyReq) => {
        if (process.env.BUG_READER_KEY) {
          proxyReq.setHeader('X-Api-Key', process.env.BUG_READER_KEY);
        }
      }
    })
  );
};
```

- [ ] **Step 2: Production nginx template**

`nginx/default.conf.template` (the nginx image runs `envsubst` on `/etc/nginx/templates/*.template` at start; `NGINX_ENVSUBST_FILTER=^BUG_` limits substitution to our variables so `$host` etc. stay intact):

```nginx
server {
    listen 80;
    server_name localhost;

    # The whole site requires a login; the bug proxy below acts with the reader key.
    auth_basic "MMO Error Reporter";
    auth_basic_user_file /etc/nginx/htpasswd;

    location / {
        root /usr/share/nginx/html;
        index index.html;
        try_files $uri $uri/ /index.html;
    }

    # Bug API: the reader key is added here and never shipped to the browser.
    location /api/bugs {
        proxy_pass ${BUG_API_UPSTREAM}/api/bugs;
        proxy_http_version 1.1;
        proxy_set_header Host $proxy_host;
        proxy_set_header Authorization "";
        proxy_set_header X-Api-Key "${BUG_READER_KEY}";
    }

    location /api/ {
        proxy_pass http://api:3000/api/;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }

    location /health {
        proxy_pass http://api:3000/health;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }

    error_page 404 =200 /index.html;
}
```

Delete the old `nginx.conf` (`git rm nginx.conf`).

In `Dockerfile` replace

```dockerfile
# Copy custom nginx config
COPY nginx.conf /etc/nginx/conf.d/default.conf
```

with

```dockerfile
# nginx config template; envsubst fills in BUG_* variables at container start
COPY nginx/default.conf.template /etc/nginx/templates/default.conf.template
ENV NGINX_ENVSUBST_FILTER=^BUG_
```

In `docker-compose.yml` replace the `ui` service's `environment` and add a volume:

```yaml
    environment:
      - NODE_ENV=production
      - BUG_API_UPSTREAM=${BUG_API_UPSTREAM:-http://api:3000}
      - BUG_READER_KEY=${BUG_READER_KEY}
    volumes:
      - ./nginx/htpasswd:/etc/nginx/htpasswd:ro
```

`nginx/README.md`:

````markdown
# UI container nginx

- `default.conf.template` is rendered by the nginx image at start (`envsubst`, filtered to `BUG_*`).
- `BUG_READER_KEY` (required): reader key of the bug API, added to every `/api/bugs` request.
- `BUG_API_UPSTREAM` (default `http://api:3000`): where the bug API lives, e.g. `https://error.mmo-dev.net`.
- `nginx/htpasswd` (required, not committed): basic-auth users. Create one with
  `docker run --rm httpd:alpine htpasswd -nbB <user> <password> > nginx/htpasswd`.
````

Add `nginx/htpasswd` to `.gitignore`.

- [ ] **Step 3: Verify the production image renders the template**

```bash
docker run --rm httpd:alpine htpasswd -nbB test test > nginx/htpasswd
docker build -t mmo-error-report-ui:test .
docker run --rm -e BUG_READER_KEY=dummy -e BUG_API_UPSTREAM=http://example.invalid -v "$PWD/nginx/htpasswd:/etc/nginx/htpasswd:ro" mmo-error-report-ui:test sh -c "/docker-entrypoint.sh nginx -t && grep -n 'X-Api-Key\|proxy_set_header Host \$host' /etc/nginx/conf.d/default.conf"
```

Expected: `nginx: configuration file /etc/nginx/nginx.conf test is successful`, the `X-Api-Key "dummy"` line, and `$host` still literally present (not substituted).

If Docker is not available on the machine, skip this step and say so in the task report.

- [ ] **Step 4: Verify no key in the bundle**

```bash
BUG_READER_KEY=should-not-leak npm run build
grep -r "should-not-leak" build/ && echo "LEAK" || echo "clean"
```

Expected: `clean`.

- [ ] **Step 5: Commit**

```bash
git add src/setupProxy.js nginx/default.conf.template nginx/README.md Dockerfile docker-compose.yml package.json package-lock.json .gitignore
git rm --cached nginx.conf 2>/dev/null; git add -A nginx.conf 2>/dev/null
git commit -m "feat: key-injecting bug API proxy with basic auth"
```

---

### Task 6: Manual end-to-end check against a local backend

- [ ] **Step 1: Start a local backend with seeded data**

In `H:\mmo-error-report` (needs a local MongoDB, e.g. `docker run -d -p 27017:27017 --name bugs-mongo mongo:6`):

```bash
MONGODB_URI=mongodb://localhost:27017/bugs-dev BUG_INGEST_KEYS=ingest BUG_READER_KEYS=reader PORT=3000 node src/index.js
```

Seed two bugs:

```bash
node -e "const b=require('./tests/fixtures').validBug();fetch('http://localhost:3000/api/bugs',{method:'POST',headers:{'Content-Type':'application/json','X-Api-Key':'ingest'},body:JSON.stringify(b)}).then(r=>r.json()).then(console.log)"
node -e "const b=require('./tests/fixtures').validBug({subject:{type:'spell',id:7,name:'Fireball'}});fetch('http://localhost:3000/api/bugs',{method:'POST',headers:{'Content-Type':'application/json','X-Api-Key':'ingest'},body:JSON.stringify(b)}).then(r=>r.json()).then(console.log)"
```

Expected: two `{ bugId: ... }` lines.

- [ ] **Step 2: Start the UI dev server with the dev proxy**

```bash
cd /h/mmo-error-report-ui
BUG_READER_KEY=reader BUG_API_TARGET=http://localhost:3000 PORT=3001 BROWSER=none npm start
```

- [ ] **Step 3: Check in the browser pane**

Open `http://localhost:3001/bugs`. Verify: both bugs are listed with status `new`; the subject filter `spell` leaves one row; clicking a row opens the detail page with the server snapshot JSON; setting status `triaged`, a component and a note and pressing Save shows the new status chip and three history entries (`status: new -> triaged`, `triage updated`, `note: ...`).

- [ ] **Step 4: Stop the servers.** No commit (manual verification only).
