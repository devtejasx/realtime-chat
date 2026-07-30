# Architecture diagrams

Mermaid diagrams for the system as it stands. GitHub renders these inline; any
Mermaid-aware viewer will too.

Prose covering the same ground is in [Architecture.md](Architecture.md); this file
is the visual companion, and each diagram is followed by the reasoning that is not
obvious from the picture.

---

## 1. System architecture

```mermaid
graph TB
    subgraph clients["Clients"]
        Web["Web / mobile client"]
    end

    subgraph edge["Edge"]
        LB["Load balancer / Ingress<br/>TLS, rate limiting, WS upgrade"]
    end

    subgraph app["Application tier (N replicas)"]
        direction TB
        I1["realtime-chat #1"]
        I2["realtime-chat #2"]
        I3["realtime-chat #N"]
    end

    subgraph data["Data tier"]
        PG[("PostgreSQL<br/>source of truth")]
        RD[("Redis<br/>cache · rate limits · Pub/Sub")]
    end

    subgraph obs["Observability"]
        PROM["Prometheus<br/>/metrics"]
        OTEL["OTel Collector"]
        JAEGER["Jaeger / Zipkin / Tempo"]
    end

    Web -->|"HTTPS REST"| LB
    Web -->|"WSS"| LB
    LB --> I1 & I2 & I3

    I1 & I2 & I3 --> PG
    I1 & I2 & I3 --> RD

    I1 <-.->|"Pub/Sub fan-out"| RD
    I2 <-.->|"Pub/Sub fan-out"| RD
    I3 <-.->|"Pub/Sub fan-out"| RD

    PROM -.->|"scrape"| I1 & I2 & I3
    I1 & I2 & I3 -.->|"OTLP spans"| OTEL
    OTEL --> JAEGER
```

The dotted Pub/Sub edges are the load-bearing part. A WebSocket connection is
pinned to whichever replica accepted it, so a message persisted by replica #1 has
no path to a recipient connected to replica #2 without that hop. On a single
replica the problem is invisible; it appears the moment you scale, as intermittent
message loss that does not reproduce locally.

---

## 2. Clean architecture layering

```mermaid
graph TD
    subgraph L1["Presentation — controllers, middlewares, WebSocket"]
        C["AuthController · MessageController · AdminController<br/>SearchController · DocsController · EventDispatcher"]
    end

    subgraph L2["Application — services"]
        S["AuthService · MessageService · ConversationService<br/>AuthorizationService · AuditService · SearchService"]
    end

    subgraph L3["Domain — models, events, policy"]
        D["User · Message · Conversation · AuditLog<br/>Role/Permission table · DomainEvent"]
    end

    subgraph L4["Infrastructure — persistence, cache, transport"]
        R["Pg*Repository · CacheStore · ClusterBus<br/>JwtTokenService · Tracer · FileStorage"]
    end

    C -->|"depends on"| S
    S -->|"depends on"| D
    S -.->|"depends on interfaces"| I(["IUserRepository · ICacheStore<br/>IEventPublisher · IClusterBus"])
    R -.->|"implements"| I
    I --> D

    style D fill:#2d3748,color:#fff
    style I fill:#4a5568,color:#fff
```

Dependencies point inward. The domain layer knows nothing about PostgreSQL, Crow or
Redis. Services depend on *interfaces* that the domain layer owns, and
infrastructure implements them — so swapping the cache backend, or unit-testing a
service against an in-memory fake, requires no change to business logic. That
inversion is why the test suite can exercise real service code with no database at
all.

---

## 3. Database ERD

```mermaid
erDiagram
    users ||--o{ conversation_participants : "belongs to"
    users ||--o{ messages : "sends"
    users ||--o{ sessions : "owns"
    users ||--o{ notifications : "receives"
    users ||--o{ attachments : "uploads"
    users ||--o{ message_reactions : "reacts with"
    users ||--o{ audit_logs : "acts in"
    users ||--o{ users : "banned_by"

    conversations ||--o{ conversation_participants : "has"
    conversations ||--o{ messages : "contains"

    messages ||--o{ message_reactions : "receives"
    messages ||--o{ read_receipts : "tracked by"
    messages ||--o{ attachments : "carries"

    users {
        bigserial id PK
        varchar username UK
        varchar email UK
        varchar password_hash
        varchar display_name
        varchar bio
        varchar avatar_url
        varchar role "user|moderator|admin|super_admin"
        timestamptz banned_at
        varchar ban_reason
        bigint banned_by FK
        timestamptz created_at
        timestamptz updated_at
    }

    conversations {
        bigserial id PK
        varchar type "direct|group"
        varchar name
        bigint owner_id FK
        varchar direct_key UK
        timestamptz last_message_at
        timestamptz created_at
    }

    conversation_participants {
        bigserial id PK
        bigint conversation_id FK
        bigint user_id FK
        varchar role "owner|member"
        bigint last_read_message_id
        timestamptz joined_at
    }

    messages {
        bigserial id PK
        bigint conversation_id FK
        bigint sender_id FK
        varchar type "text|system"
        text content
        tsvector search_vector "GENERATED STORED"
        timestamptz edited_at
        timestamptz deleted_at
        timestamptz created_at
    }

    audit_logs {
        bigserial id PK
        varchar event_id UK "de-duplication"
        varchar event_type
        bigint actor_id FK "ON DELETE SET NULL"
        varchar actor_username "denormalised"
        varchar target_type
        varchar target_id
        varchar ip
        varchar correlation_id
        varchar trace_id
        jsonb metadata
        timestamptz occurred_at
    }

    sessions {
        varchar id PK
        bigint user_id FK
        varchar refresh_token_hash
        varchar user_agent
        varchar ip
        timestamptz expires_at
        timestamptz revoked_at
    }
```

Three details are deliberate and easy to get wrong:

* `audit_logs.actor_id` is `ON DELETE SET NULL`, not `CASCADE`. Deleting a user must
  never erase the record of what they did — that history is the whole point.
  `actor_username` is denormalised alongside it so the trail survives the deletion.
* `audit_logs.event_id` is `UNIQUE`. The writer is at-least-once, so the constraint
  plus `ON CONFLICT DO NOTHING` makes a redelivery a no-op rather than a duplicate.
* `messages.search_vector` is a `GENERATED ... STORED` column, computed on write
  and indexed with GIN. The earlier design computed `to_tsvector` per row at query
  time, which cannot use an index reliably.

---

## 4. Request lifecycle

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant SEC as SecurityMiddleware
    participant MET as MetricsMiddleware
    participant TRC as TracingMiddleware
    participant LOG as LoggingMiddleware
    participant VER as ApiVersionMiddleware
    participant R as Router
    participant H as Controller
    participant SVC as Service
    participant DB as PostgreSQL

    C->>SEC: GET /api/v1/messages
    SEC->>MET: CORS handled, timer not yet started
    MET->>TRC: start timer
    TRC->>TRC: parse traceparent, open SERVER span
    TRC->>LOG: inject X-Request-Id
    LOG->>VER: log "--> GET /api/v1/messages"
    VER->>VER: rewrite to /api/messages
    VER->>R: route
    R->>H: handler
    H->>H: authenticate + authorize
    H->>SVC: list(actor, query, page)
    SVC->>DB: SELECT (child span)
    DB-->>SVC: rows
    SVC-->>H: messages
    H-->>VER: 200 JSON
    VER->>VER: restore original path, set X-API-Version
    VER-->>LOG: unwind
    LOG->>LOG: log "<-- GET /api/v1/messages 200"
    LOG-->>TRC: unwind
    TRC->>TRC: close span, set traceparent
    TRC-->>MET: unwind
    MET->>MET: record duration + status class
    MET-->>SEC: unwind
    SEC->>SEC: security headers
    SEC-->>C: response
```

The middleware order is the interesting part. `ApiVersionMiddleware` is
*innermost*: it rewrites the path immediately before routing and is the first to
unwind, restoring the original path so the outer access log reports what the client
actually requested. Because it is innermost, a rejected API version still unwinds
through metrics, logging and the security headers rather than escaping them.

---

## 5. Authentication flow

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant AC as AuthController
    participant AS as AuthService
    participant US as UserService
    participant SS as SessionService
    participant BUS as EventBus
    participant DB as PostgreSQL

    C->>AC: POST /api/v1/auth/login
    AC->>AS: login(identifier, password)
    AS->>US: find + verify (bcrypt)
    US->>DB: SELECT user
    DB-->>US: row
    US-->>AS: user
    AS->>AS: issue access + refresh JWT
    AS-->>AC: AuthResponse
    AC->>SS: record(user, refresh_token, ip, ua)
    SS->>DB: INSERT session (SHA-256 of refresh token)
    AC->>BUS: publish UserLoggedIn{ip, user_agent}
    AC-->>C: 200 {user, tokens, session_id}

    Note over BUS,DB: asynchronous — off the request path
    BUS-->>DB: INSERT audit_logs

    C->>AC: POST /api/v1/auth/refresh
    AC->>SS: rotate(refresh_token, session_id)
    SS->>DB: compare hash, store NEW hash
    SS-->>C: fresh token pair
```

Refresh tokens are stored only as SHA-256 hashes and are **rotated on every
refresh**. A stolen token that is later replayed no longer matches the stored hash
and is rejected — and the mismatch is itself a signal that a token was
compromised. The audit write happens on the event bus, so an audit outage cannot
fail a login.

---

## 6. Authorisation and RBAC

```mermaid
flowchart TD
    A["Request with Bearer token"] --> B["AuthMiddleware.authenticate"]
    B --> C{"JWT valid?"}
    C -->|no| C1["401 authentication_error"]
    C -->|yes| D["AuthorizationService.require_active"]

    D --> E{"cached?"}
    E -->|hit| G{"suspended?"}
    E -->|miss| F["SELECT banned_at FROM users"]
    F --> F1["cache for AUTHZ_CACHE_TTL_SECONDS"]
    F1 --> G

    G -->|yes| G1["401 Account is suspended"]
    G -->|no| H{"route needs a permission?"}

    H -->|no| Z["handler runs"]
    H -->|yes| I["AuthorizationService.require_permission"]
    I --> J["role_of(user) — cached, DB-authoritative"]
    J --> K{"has_permission(role, permission)?"}
    K -->|no| K1["403 authorization_error"]
    K -->|yes| Z

    style C1 fill:#742a2a,color:#fff
    style G1 fill:#742a2a,color:#fff
    style K1 fill:#742a2a,color:#fff
```

The role is resolved from the database, not read from the JWT. Putting it in the
token would make authorisation free, and would also mean a demoted moderator or a
banned user keeps their old rights until the access token expires — up to fifteen
minutes. A short-TTL cache keeps the cost to one cache hit while preserving
revocability; mutations call `invalidate()` so the change takes effect immediately.

Call sites ask about a `Permission`, never compare `Role` ordinals: comparing roles
couples every check to the ordering, and inserting a role in the middle would
silently re-authorise code paths.

---

## 7. WebSocket flow

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant WS as WebSocketController
    participant ED as EventDispatcher
    participant CM as ConnectionManager
    participant MS as MessageService
    participant DB as PostgreSQL
    participant CB as ClusterBus (Redis)
    participant P as Peer on another replica

    C->>WS: GET /api/v1/ws?token=...&protocol=2
    WS->>WS: verify JWT, negotiate protocol
    WS-->>C: 101 Switching Protocols
    WS->>ED: on_open(conn, user, version)
    ED->>CM: register_session
    ED->>DB: conversation ids → join rooms
    ED->>CM: publish presence.update (online)
    ED-->>C: ready {protocol_version: 2}

    C->>ED: {"event":"message.send","request_id":"r1",...}
    ED->>MS: send(actor, request)
    MS->>DB: INSERT message (persist first)
    DB-->>MS: stored
    MS->>CM: publish(participants, message.created)
    CM->>C: frame encoded for this peer's version
    CM->>CB: publish to rtc:cluster:user
    CB->>P: deliver_local(...) on other replicas

    C->>ED: {"event":"ping","request_id":"r2"}
    ED-->>C: {"event":"pong","request_id":"r2",...}
```

Persist first, broadcast second — always. A client that receives a message the
server failed to store would show history that does not exist. Frames are encoded
once per *distinct protocol version* among the recipients (at most twice), not once
per connection.

The cluster hop uses `deliver_local` on the receiving side. Calling the normal
`publish` there would re-broadcast, and the message would circulate forever; the
origin-node filter in the bus is the second, independent guard against that.

---

## 8. Event flow

```mermaid
flowchart LR
    subgraph producers["Producers"]
        P1["AuthController<br/>login · logout · register"]
        P2["MessageService<br/>sent · edited · deleted"]
        P3["ConversationService<br/>created · members"]
        P4["AdminController<br/>ban · role · flags"]
        P5["EventDispatcher<br/>presence"]
    end

    BUS["InProcessEventBus<br/>publish() noexcept"]
    EXEC["BackgroundExecutor<br/>worker pool"]
    DISP["EventDispatcher<br/>filter + isolate"]

    subgraph subscribers["Subscribers"]
        S1["AuditLogSubscriber<br/>→ audit_logs"]
        S2["FunctionSubscriber<br/>→ metrics counters"]
        S3["(future) broker forwarder"]
    end

    P1 & P2 & P3 & P4 & P5 --> BUS
    BUS -->|"enqueue"| EXEC
    EXEC --> DISP
    DISP -->|"interested_in?"| S1 & S2 & S3

    style BUS fill:#2c5282,color:#fff
    style DISP fill:#2c5282,color:#fff
```

Publishing is `noexcept` and asynchronous: the event has already happened by the
time it is published, so a delivery problem is the bus's concern, never the
producer's. Each subscriber runs inside its own try/catch, because subscribers are
independent side effects — if the audit database is down, metrics and notifications
must still work.

Ordering across events is explicitly not guaranteed. Every subscriber here is
order-independent by design; anything needing strict ordering belongs on a message
broker with a partition key, not on an in-process bus.

---

## 9. Cache flow

```mermaid
flowchart TD
    REQ["Service call"] --> CS["CacheService.remember(ns, key, ttl)"]
    CS --> GET{"cache hit?"}
    GET -->|yes| HIT["record hit → return value"]
    GET -->|no| MISS["record miss"]
    MISS --> LOAD["loader() → PostgreSQL"]
    LOAD --> PUT["put(ns, key, value, ttl)"]
    PUT --> RET["return value"]

    subgraph backends["ICacheStore"]
        MEM["InMemoryCacheStore<br/>single instance / tests"]
        RDS["RedisCacheStore<br/>multi-instance"]
    end

    CS -.-> MEM
    CS -.-> RDS

    WRITE["Mutation<br/>(profile update, role change, ban)"] --> INV["invalidate(ns, key)"]
    INV -.-> MEM
    INV -.-> RDS

    style HIT fill:#22543d,color:#fff
    style LOAD fill:#744210,color:#fff
```

Invalidation is explicit and lives next to the write that requires it, rather than
being inferred. TTL alone is not sufficient for authorisation data: after a ban or
a role change, `AuthorizationService::invalidate` runs immediately, and the TTL is
only the worst case if an invalidation is ever missed — for example when another
replica performed the change and the backend is the process-local store rather than
Redis.

---

## 10. Deployment topology

```mermaid
graph TB
    subgraph internet["Internet"]
        U["Users"]
    end

    subgraph vpc["AWS VPC"]
        subgraph public["Public subnets (multi-AZ)"]
            ALB["Application Load Balancer<br/>TLS 1.2+ · idle_timeout 3600s"]
            NAT["NAT gateway"]
        end

        subgraph private["Private subnets (multi-AZ)"]
            E1["EC2 · realtime-chat"]
            E2["EC2 · realtime-chat"]
        end

        subgraph dbtier["Database subnets — no internet route"]
            RDS[("RDS PostgreSQL<br/>Multi-AZ · encrypted")]
            EC[("ElastiCache Redis<br/>replica + failover")]
        end

        SM["Secrets Manager"]
    end

    U -->|"HTTPS / WSS"| ALB
    ALB -->|"health: /health/ready"| E1 & E2
    E1 & E2 --> RDS
    E1 & E2 --> EC
    E1 & E2 -.->|"instance role"| SM
    E1 & E2 --> NAT
    NAT --> internet

    style dbtier fill:#2d3748,color:#fff
```

The database tier's route table contains only the local VPC route — no NAT, no
internet gateway. A compromised database host therefore has no outbound path for
exfiltration. Security groups reference each other by id rather than by CIDR, so
"only the application may reach the database" stays true regardless of how the
subnets are later resized.

The load balancer health-checks `/health/ready`, not `/health/live`: readiness
fails when a dependency is unavailable, which correctly removes the instance from
rotation. Using liveness would keep an instance receiving traffic while every
request it served failed.
