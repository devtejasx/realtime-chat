# Load testing (k6)

Four scenarios covering the paths that determine capacity: authentication,
REST messaging, WebSocket concurrency, and upload/search.

## Running

Install [k6](https://k6.io/docs/get-started/installation/), start the stack, then:

```bash
k6 run --env BASE_URL=http://localhost:8080 loadtest/messaging.js
```

```bash
k6 run --env BASE_URL=http://localhost:8080 loadtest/websocket.js
```

```bash
k6 run --env BASE_URL=http://localhost:8080 loadtest/auth.js
```

```bash
k6 run --env BASE_URL=http://localhost:8080 loadtest/upload.js
```

Useful environment variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `BASE_URL` | `http://localhost:8080` | Target host |
| `WS_URL` | derived from `BASE_URL` | Override if WebSockets terminate elsewhere |
| `API_PREFIX` | `/api/v1` | Set to `/api` to exercise the legacy alias |
| `WS_PROTOCOL` | `2` | Set to `1` for the legacy WebSocket format |
| `WS_HOLD_SECONDS` | `45` | How long each VU holds its socket open |
| `USER_POOL` | `10` | Users registered by the messaging scenario |

Disable rate limiting when measuring raw capacity, or the limiter — not the
service — is what you will be measuring:

```bash
RATE_LIMIT_ENABLED=false docker compose up -d
```

## Reading the results

Each scenario reports custom per-operation trends (`rtc_login_duration`,
`rtc_message_send_duration`, `rtc_ws_message_latency`, …) alongside k6's built-in
metrics. The custom ones are the useful ones: k6's `http_req_duration` aggregates
every request in the run, which hides the fact that login (bcrypt, deliberately
slow) and sending a message (one INSERT) have entirely different cost profiles.

## Expected performance

**These are shapes to expect, not guarantees.** They come from the design of each
path, not from a benchmark run on your hardware — the absolute numbers depend
entirely on CPU, disk, network and database sizing. Use them to sanity-check that
an endpoint is in the right *order of magnitude*, and to notice when the
relationships between endpoints stop holding.

| Operation | Expected p95 | Why it lands there |
| --- | --- | --- |
| `POST /auth/login` | 150–500 ms | bcrypt dominates, by design. A *fast* login is a cheap-to-brute-force login. |
| `POST /auth/register` | 150–600 ms | bcrypt again, plus one INSERT. |
| `POST /auth/refresh` | 5–50 ms | SHA-256 comparison and two UPDATEs — no bcrypt. Should be ~10× faster than login. |
| `GET /auth/me` | 2–20 ms | JWT verification plus a cached authorisation lookup. |
| `POST /messages` | 10–150 ms | One INSERT + one UPDATE in a transaction, then in-memory fan-out. |
| `GET /messages` | 5–100 ms | Index scan on `(conversation_id, id DESC)` — no sort. |
| `GET /search/messages` | 20–300 ms | GIN index scan plus `ts_headline`; highlighting is the expensive half. |
| `POST /attachments` | 100–2000 ms | Transfer + disk write. Thumbnailing is asynchronous and must **not** appear here. |
| WebSocket handshake | 50–500 ms | JWT verification plus the room-subscription query. |
| WebSocket round trip | 10–200 ms | Persist, then fan out. The number users actually perceive. |

The **relationships** are more diagnostic than the absolute values:

* `refresh` should be roughly an order of magnitude faster than `login`. If it is
  not, the session lookup is missing an index.
* `GET /messages` should not degrade as a conversation grows. If it does, the query
  has stopped using the composite index — check for a sort in `EXPLAIN`.
* Upload latency should be flat in file size for the *response*; if large files
  are disproportionately slow, thumbnail generation has leaked onto the request
  path.
* WebSocket round trip should stay flat as connection count rises, until it
  suddenly does not — that knee is the instance's real concurrency ceiling.

## Establishing your own baseline

Thresholds in these scripts are targets, not measurements. Before treating a
breach as a regression, record a baseline on the hardware you actually run on:

```bash
k6 run --summary-export=baseline-messaging.json loadtest/messaging.js
```

Re-run after a change and compare. A single run on a laptop is noisy — take the
median of three, and never baseline on a machine that is also running the database
under test.

## Capacity notes

**WebSocket connections are the binding constraint,** not request throughput. Each
connection costs a socket, a `Session` object and its room memberships. Watch
`rtc_ws_connections` on `/metrics` and scale on that rather than on CPU — the
HPA's CPU target is a proxy, and `deploy/k8s/hpa.yaml` documents how to switch it
to the connection metric via prometheus-adapter.

**Test more than one replica.** A single-instance run cannot exercise the Redis
Pub/Sub fan-out, so it will not catch the failure mode where a message reaches only
the recipients connected to the sending instance. Run the WebSocket scenario
against at least two replicas with `CLUSTER_ENABLED=true` and confirm delivery
latency does not diverge between them.

**Watch the database pool.** `DB_POOL_SIZE × replicas` must stay comfortably below
PostgreSQL's `max_connections`. Pool exhaustion shows up as a latency cliff on
every endpoint at once, which is easy to misread as a slow query.
