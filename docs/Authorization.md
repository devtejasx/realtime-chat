# Authorization and audit

Roles, permissions, account suspension, and the audit trail.

## Roles

| Role | Intended holder |
| --- | --- |
| `user` | Every account by default. Acts only on its own data. |
| `moderator` | Content moderation: delete or edit any message, manage groups. |
| `admin` | Day-to-day operations: suspend accounts, view sessions, read the audit log, view metrics. |
| `super_admin` | Everything, plus the two capabilities that can escalate privilege: assigning roles and toggling feature flags. |

Roles are a strict hierarchy — each tier holds everything below it — but that
hierarchy is *derived from* the permission table rather than assumed.

## Permissions

| Permission | `user` | `moderator` | `admin` | `super_admin` |
| --- | :-: | :-: | :-: | :-: |
| `message.delete_any` | | ✓ | ✓ | ✓ |
| `message.edit_any` | | ✓ | ✓ | ✓ |
| `group.manage` | | ✓ | ✓ | ✓ |
| `conversation.view_any` | | | ✓ | ✓ |
| `user.ban` | | | ✓ | ✓ |
| `user.manage` | | | ✓ | ✓ |
| `session.view` | | | ✓ | ✓ |
| `session.revoke` | | | ✓ | ✓ |
| `audit.view` | | | ✓ | ✓ |
| `system.metrics` | | | ✓ | ✓ |
| `user.manage_roles` | | | | ✓ |
| `system.feature_flags` | | | | ✓ |

`user` holds no entry here because every listed capability is about acting on
*someone else's* data. Ordinary users act on their own through the normal API,
which authorises by ownership rather than by role.

The two super-admin-only permissions are separated deliberately: role assignment is
a direct escalation vector, and feature flags can disable safety controls such as
rate limiting or audit logging.

## Why call sites ask about permissions, never roles

```cpp
// Correct.
authz.require_permission(actor_id, security::Permission::kViewAuditLogs);

// Wrong, even though it looks equivalent today.
if (role >= security::Role::kAdmin) { ... }
```

Comparing role ordinals couples every call site to the ordering. Insert a role in
the middle later — a `support` tier between `moderator` and `admin`, say — and every
such comparison silently changes meaning, granting or revoking access nobody
reviewed. `has_permission` is `constexpr`, so the safe form costs nothing at run
time.

## Where the role comes from

**The database, on every authorisation decision — not the JWT.**

Putting the role in the token would make lookups free, and it is what most examples
do. It is also wrong for anything that must be revocable: a token is a bearer
credential valid until it expires, so a demoted moderator or a suspended user keeps
their old rights for the remainder of the access TTL. "Banned" that takes effect in
fifteen minutes is not banned.

So `AuthorizationService` reads the store, behind a short-TTL cache:

* a decision costs one cache hit, not one query;
* every mutation calls `invalidate()`, so a change takes effect immediately;
* `AUTHZ_CACHE_TTL_SECONDS` (default 30) is the *worst case* if an invalidation is
  ever missed — for instance when another replica made the change and the cache
  backend is process-local rather than Redis.

That last point is a real reason to run Redis in a multi-replica deployment even if
you do not think you need a shared cache.

## Assigning roles

```bash
curl -X PUT https://chat.example.com/api/v1/admin/users/42/role \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"role":"moderator"}'
```

Requires `user.manage_roles`. A caller may never assign a role **at or above their
own tier** (`can_assign_role`), so an account with the permission cannot mint a peer
or promote itself. Without that rule the hierarchy would be decorative.

## Suspension

```bash
curl -X POST https://chat.example.com/api/v1/admin/users/42/ban \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"reason":"spam"}'
```

Suspension does two things, and both are necessary:

1. Sets `banned_at`, which `AuthMiddleware` checks on every authenticated request —
   so existing access tokens stop working on the target's next call.
2. Revokes every refresh session. Without this the account could keep minting fresh
   access tokens indefinitely.

An administrator cannot suspend themselves, or an account at or above their own
role.

Two endpoints deliberately remain reachable while suspended: `POST /auth/logout`
and `GET /auth/me`. A suspended user must still be able to sign out cleanly, and a
client needs to be able to explain *why* the rest of the API is refusing it.

## Audit log

Written from the event bus, never inline in a request handler. The action has
already happened by the time the event is published, so an audit failure must not
fail the user's action.

### What is recorded

Authentication (`user.logged_in`, `user.logged_out`, `user.registered`), credential
and profile changes (`user.password_changed`, `user.profile_updated`,
`user.role_changed`), membership changes (`conversation.created`,
`conversation.deleted`, `conversation.member_added`, `conversation.member_removed`),
destructive content actions (`message.deleted`), and every `admin.action`.

### What is not, and why

`message.sent`, `message.edited`, reactions, presence transitions, attachment
uploads and notifications are ordinary high-volume traffic. Recording them would
bury the security-relevant entries and roughly double the write load on the hottest
paths in the system. If you need message-level forensics, that is a data-retention
question for the `messages` table, not an audit-log one.

### Properties worth knowing

* **Append-only.** The repository has no update or delete method. The value of an
  audit log is that it cannot be quietly rewritten.
* **Idempotent.** `event_id` is `UNIQUE` and inserts use `ON CONFLICT DO NOTHING`.
  The writer is at-least-once, so a redelivery is normal traffic, not an error.
* **Survives user deletion.** `actor_id` is `ON DELETE SET NULL`, and
  `actor_username` is denormalised alongside it — deleting a user must not erase
  the record of what they did.
* **Correlated.** Each row carries the `correlation_id` and W3C `trace_id` of the
  request that caused it, so an audit entry links directly to its access-log line
  and its distributed trace.

### Searching

```bash
curl -G https://chat.example.com/api/v1/admin/audit-logs \
  -H "Authorization: Bearer $TOKEN" \
  -d event_type=user.logged_in -d actor_id=42 -d limit=50
```

Filters: `actor_id`, `event_type`, `target_type`, `target_id`, `correlation_id`,
`from` / `to` (Unix epoch seconds), plus `limit` / `offset`. Requires `audit.view`.

`GET /api/v1/admin/audit-logs/summary` returns the same filter applied as an
event-type histogram.

### Retention

Not handled by application code, deliberately — no code path exists that could be
abused to erase evidence. Use a scheduled partition drop or a `DELETE` job run by
your operations tooling; see [Monitoring.md](Monitoring.md).

## Creating the first super admin

There is no bootstrap endpoint, on purpose: an endpoint that grants
`super_admin` is an endpoint that can be abused. Promote the first one directly:

```sql
UPDATE users SET role = 'super_admin' WHERE username = 'your-admin-account';
```

Then invalidate the cached decision, or wait `AUTHZ_CACHE_TTL_SECONDS`. Every
subsequent role change should go through the API so it lands in the audit log.
