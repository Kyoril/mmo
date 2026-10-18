# Bot chat

Swarm bots can take part in chat through a language model (Claude Haiku 5.5 by default). They
answer whispers, sometimes reply in party, guild, `/say` and General (`/1`), and now and then
post something unprompted. They write like players: short, casual, the occasional typo
followed by a `*fix`.

Chat is **off by default** because it costs money. The spend has a hard cap.

## Quick start

```powershell
$env:MMO_BOTSWARM_CLAUDE_API_TOKEN = "<your key>"

# One request, printed with its cost (about $0.0001). Checks the key without a server.
bin/Debug/bot_swarm.exe --chat-probe

# The swarm with chat, using the default caps ($0.25/day, $5/month, 120 requests/hour).
bin/Debug/bot_swarm.exe --bots 20 --chat

# The whole chat path with canned lines and no model: free.
bin/Debug/bot_swarm.exe --bots 20 --chat --chat-offline
```

The key is read from the environment: `MMO_BOTSWARM_CLAUDE_API_TOKEN`, falling back to
`ANTHROPIC_API_KEY`. The swarm's own variable wins, so a general-purpose key on the machine is
not spent on bot chat when the swarm has one of its own. There is no command-line flag for it, because a
key on the command line ends up in process listings and shell history. The key never goes into
a prompt and is never logged.

| Flag | Default | Meaning |
|---|---|---|
| `--chat` | off | Enable chat |
| `--chat-offline` | off | Canned lines instead of the model; uses an in-memory ledger |
| `--chat-model` | `claude-haiku-5-5` | Must be a model with a known price (`bot_chat/chat_budget.cpp`) |
| `--chat-daily-usd` | `0.25` | Hard cap per UTC day |
| `--chat-monthly-usd` | `5` | Hard cap per UTC month |
| `--chat-max-per-hour` | `120` | Hard cap on requests per UTC hour |
| `--chat-ledger` | `e2e/runtime/bots/chat_ledger.json` | Spend ledger, kept across runs |
| `--chat-facts` | none | Text file with extra facts about the game for the prompt |
| `--chat-probe` | | Send one request, print the answer and its cost, exit |

## What it costs

A request is about 600 to 900 input tokens and at most 60 output tokens. Thinking is turned off,
because thinking tokens are billed as output. On Haiku 5.5 ($0.10 / $0.50 per million tokens) a
request costs about $0.0001, so the default daily cap pays for roughly 2,000 to 2,500 chat
lines. Most days the hourly request cap (120) is reached before the dollar cap.

## How the cap holds

`ChatBudget` charges every request its **worst-case** cost (prompt length / 3 tokens, plus
max_tokens) in the ledger file *before* the request is sent. When the answer comes back, the
charge is lowered to what the API reports in `usage`. As a result:

- A crash, a hang or a lost connection can only make the ledger overstate the spend. A request
  that failed after it may have reached the API keeps its full reservation.
- An error response (4xx/5xx) is not billed by the API and is refunded completely.
- A restart picks up the same ledger, so it does not get a fresh allowance.
- Several swarm processes can share one ledger. It is re-read before every reservation. It is
  not locked, so two processes reserving in the same millisecond can overshoot by one request
  each.
- An unknown model id is refused outright rather than budgeted with a guessed price.

Once a cap is reached, the bots stop talking until the next UTC hour, day or month. After a
refusal the budget is not asked again for 60 seconds.

Requests go out one at a time on a single worker thread, with at most four queued. A burst of
chat therefore cannot turn into a burst of parallel spend.

## Who says what

`ChatDirector` (`src/shared/bot_chat/`) makes the decisions for the whole swarm:

- **Whispers** from players are always answered. A player who sends several lines in a row gets
  one answer once they stop typing. A bot answers one person at most 8 times per 10 minutes and
  then goes quiet on them, so nobody can spend the budget by spamming a bot.
- **Party, guild, `/say` and General**: all bots in the channel hear a line, and at most one of
  them answers. A bot whose name is mentioned answers about 90% of the time. Otherwise the
  chance depends on the channel (party about 55%, General about 8%, or 25% for a question) and on
  the bot's chattiness. Bot replies in General are at least 45 seconds apart across the swarm.
- **Bots and bots**: bots never answer another bot's whisper, and only sometimes reply to a
  bot's unprompted post. They never answer a bot that was itself replying, so bot-to-bot
  exchanges end after one round.
- **Unprompted posts** happen every 25 to 90 minutes per bot (scaled by chattiness), with at most
  one in General every 4 minutes across the swarm. They are made only while a real player has
  been seen in the last 30 minutes, either through chat or as a non-bot player near any bot. A
  server with nobody on it costs nothing.

Each bot has a persona rolled from its roster seed: temperament, writing style and typo rate.
Its prompt contains its level, race, class, map and current activity, a few recent events
(kills, deaths, level ups), and the zone, race and class names from the game data, so it talks
about places that exist. The model can answer `SKIP` to stay silent. Replies are cut to one line
of at most 200 characters, and lines with links or AI tell-tales are dropped.

Bot chat goes to the telemetry stream as `chat_out` events (with the text) and `chat_error`
events. The summary has `chat_lines_heard`, `chat_lines_sent`, `chat_requests`,
`chat_budget_denied`, `chat_model_errors` and `chat_spent_today_usd`.

## Limits

- Bots only know they are in a guild once they have seen guild chat in the session. They never
  start guild conversations before that.
- The bots are told to pass as human players, and to brush off questions about whether they
  are bots. That is a choice for whoever runs the server, not a technical requirement.
