# WebSocket protocol

Reference for the real-time wire protocol. Two versions are supported; version 1 is
the historical format and stays frozen, version 2 adds correlation metadata.

## Connecting

```
wss://<host>/api/v1/ws?token=<access_token>&protocol=2
```

Also reachable at `/ws` (unversioned, permanently supported).

| Parameter | Required | Meaning |
| --- | --- | --- |
| `token` | yes* | JWT access token. Browsers cannot set headers on a WebSocket upgrade, which is why it goes in the query string. |
| `protocol` | no | `1` or `2`. Absent or unrecognised → `1`. |

\* An `Authorization: Bearer` header is accepted instead when the client can set
one. Authentication happens during the handshake, so an invalid token produces a
failed upgrade rather than an open socket that later errors.

> **Note on the token in the URL.** Query strings are routinely written to proxy
> and server access logs. Access tokens are short-lived (15 minutes by default),
> which bounds the exposure, but configure your edge proxy not to log the query
> string for `/ws` — `nginx/conf.d/realtime-chat.conf` shows the pattern.

### Version negotiation

Negotiation happens once, at the handshake, rather than per frame. The alternative
— emitting a union of both shapes on every frame — would carry the payload twice on
the hottest path in the system.

An unknown version falls back to v1 rather than failing the handshake: a client
asking for a future version gets a working connection. The `ready` frame reports
what was actually negotiated, so a client can verify rather than assume.

## Frame formats

### Version 1 (default, frozen)

```json
{ "type": "message.created", "data": { "...": "..." } }
```

Nothing will be added to this shape. Strict v1 clients exist, and freezing it is the
entire reason version negotiation exists.

### Version 2

```json
{
  "event": "message.created",
  "version": 2,
  "timestamp": "2026-07-30T12:00:00Z",
  "request_id": "r1",
  "correlation_id": "0af7651916cd43dd8448eb211c80319c",
  "payload": { "...": "..." }
}
```

`request_id` and `correlation_id` are always present as keys, `null` when unknown,
so client code can read them unconditionally. A reply to a client command echoes
that command's `request_id`; a server-initiated broadcast has none.

### Inbound frames

The server accepts either shape on any connection, regardless of what was
negotiated, so a client may upgrade mid-connection simply by sending the newer
form. A frame that is not valid JSON, or that carries no event name, produces an
`error` frame rather than closing the socket.

## Client → server events

| Event | Payload | Notes |
| --- | --- | --- |
| `ping` | `{}` | Replies `pong`. Application-level keepalive, distinct from protocol-level ping frames. |
| `message.send` | `{conversation_id, content, type?, attachment_ids?}` | Same DTO and service path as `POST /api/v1/messages` — no duplicated logic. |
| `typing.start` | `{conversation_id}` | Never persisted. Gated by `ENABLE_TYPING`. |
| `typing.stop` | `{conversation_id}` | As above. |
| `mark_delivered` | `{message_id}` | Gated by `ENABLE_READ_RECEIPTS`. |
| `mark_read` | `{conversation_id, up_to_message_id}` | High-water mark, not per-message. |

## Server → client events

### Lifecycle

| Event | Payload |
| --- | --- |
| `ready` | `{user_id, username, protocol_version}` — sent once, immediately after `on_open`. |
| `pong` | `{}` — reply to `ping`, echoing its `request_id` on v2. |
| `error` | `{code, message}` — `code` matches the REST error codes. |

### Messages

| Event | Payload |
| --- | --- |
| `message.created` | Full message object, including `attachment_ids`. |
| `message.updated` | Full message object with `edited_at` set. |
| `message.deleted` | Full message object with `deleted_at` set. Soft delete — the row remains. |

### Conversations

| Event | Payload |
| --- | --- |
| `conversation.created` | Conversation with participants. Also sent on rename. |
| `conversation.deleted` | `{conversation_id}` |
| `conversation.member_added` | `{conversation_id, user_id}` |
| `conversation.member_removed` | `{conversation_id, user_id}` |

### Presence, typing and receipts

| Event | Payload |
| --- | --- |
| `presence.update` | `{user_id, status}` — `online` on the first session, `offline` on the last. |
| `typing.start` / `typing.stop` | `{conversation_id, user_id, username}` — room members only, excluding the sender. |
| `receipt.update` | `{message_id, user_id, delivered_at}` |
| `read.update` | `{conversation_id, user_id, up_to_message_id}` |

### Reactions, attachments, notifications

| Event | Payload |
| --- | --- |
| `reaction.added` / `reaction.removed` | `{message_id, user_id, emoji}` |
| `attachment.uploaded` | Attachment object |
| `notification` | Notification object |
| `session.expired` | `{session_id}` — the session was revoked elsewhere. |

## Delivery semantics

**Persist first, broadcast second.** A message is written to PostgreSQL before any
frame is emitted. A client must never see a message the server failed to store.

**At-most-once delivery.** Frames are fire-and-forget. A client that misses one —
because it was reconnecting, or because a cluster hop failed — recovers by
re-reading history from `GET /api/v1/messages`, which is the durable record. This is
deliberate: a real-time frame that arrives late is worse than useless, and
durability already lives in PostgreSQL.

**The sender receives its own broadcast.** All of a user's sessions are targeted,
including the one that sent the message, so every client converges on the persisted
state rather than on its own optimistic copy.

**Ordering is per-connection.** Frames are written to a socket in the order they
were produced, but no ordering is guaranteed *between* connections or across a
cluster hop. Clients should order by `message.id`, which is monotonic.

## Multi-instance behaviour

A connection is pinned to the replica that accepted it. Each replica delivers to its
own connections and republishes on Redis Pub/Sub; peers deliver to theirs.

Two independent guards prevent a message circulating forever: peers use the
local-only delivery path (which never republishes), and every cluster message
carries its origin node id so a receiver drops its own broadcasts.

Verify fan-out is active:

```bash
curl -s localhost:8080/health/ready | jq .cluster
```

`{"distributed": false}` with more than one replica means messages are only
reaching recipients on the sending instance.

## Heartbeat

The server sends a ping every `WS_HEARTBEAT_INTERVAL_SECONDS` (30) and closes a
connection idle for `WS_HEARTBEAT_TIMEOUT_SECONDS` (90). Any inbound frame counts
as activity.

Proxy timeouts must sit **above** these values, or the proxy will sever healthy
idle connections before the application notices anything is wrong. The supplied
Ingress and ALB configurations use 3600s for exactly this reason.

## Client example

```javascript
const ws = new WebSocket(`wss://chat.example.com/api/v1/ws?token=${accessToken}&protocol=2`);

ws.onmessage = (raw) => {
  const frame = JSON.parse(raw.data);

  switch (frame.event) {
    case 'ready':
      console.log('protocol', frame.payload.protocol_version); // expect 2
      break;
    case 'message.created':
      appendMessage(frame.payload);
      break;
    case 'error':
      console.error(frame.payload.code, frame.payload.message);
      break;
  }
};

function send(conversationId, content) {
  ws.send(JSON.stringify({
    event: 'message.send',
    request_id: crypto.randomUUID(),
    payload: { conversation_id: conversationId, content },
  }));
}
```

Reconnect with exponential backoff and jitter. A deploy rolls every replica, so
without jitter all clients reconnect simultaneously and the resulting thundering
herd is self-inflicted. On reconnect, re-fetch history since the last message id
you hold rather than assuming nothing was missed.
