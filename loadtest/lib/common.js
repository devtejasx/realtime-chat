// Shared helpers for the k6 suite.
//
// Kept in one module so every scenario reports the same custom metrics and
// authenticates the same way — otherwise comparing two scenarios' numbers means
// comparing two different measurement methodologies.
import http from 'k6/http';
import { check } from 'k6';
import { Trend, Rate, Counter } from 'k6/metrics';

export const BASE_URL = __ENV.BASE_URL || 'http://localhost:8080';
export const WS_URL = __ENV.WS_URL || BASE_URL.replace(/^http/, 'ws');

// The versioned prefix is what new clients should use. Override with API_PREFIX=/api
// to load-test the legacy alias and confirm the rewrite middleware costs nothing.
export const API = __ENV.API_PREFIX || '/api/v1';

// --- custom metrics --------------------------------------------------------
//
// k6's built-in http_req_duration aggregates every request together, which hides
// the fact that login (bcrypt, deliberately slow) and sending a message (a single
// INSERT) have wildly different cost profiles. Per-operation trends make each one
// readable on its own.
export const loginDuration = new Trend('rtc_login_duration', true);
export const messageSendDuration = new Trend('rtc_message_send_duration', true);
export const messageListDuration = new Trend('rtc_message_list_duration', true);
export const searchDuration = new Trend('rtc_search_duration', true);
export const uploadDuration = new Trend('rtc_upload_duration', true);
export const wsConnectDuration = new Trend('rtc_ws_connect_duration', true);
export const wsMessageLatency = new Trend('rtc_ws_message_latency', true);

export const errorRate = new Rate('rtc_errors');
export const wsMessagesReceived = new Counter('rtc_ws_messages_received');
export const wsMessagesSent = new Counter('rtc_ws_messages_sent');

// Unique-enough identity per virtual user. __VU is unique within a run and
// __ITER within a VU; the timestamp keeps identities distinct across runs so a
// re-run does not collide with users left behind by the previous one.
export function uniqueUser(prefix = 'load') {
  const stamp = Date.now().toString(36);
  return {
    username: `${prefix}_${__VU}_${stamp}`,
    email: `${prefix}_${__VU}_${stamp}@loadtest.local`,
    password: 'LoadTest!Passw0rd',
  };
}

export function jsonHeaders(token) {
  const headers = { 'Content-Type': 'application/json' };
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }
  return headers;
}

// Registers a user and returns their credentials plus tokens.
//
// Registration is used rather than a pre-seeded pool because it exercises the real
// signup path (bcrypt included) and avoids every VU hammering one account, which
// would produce unrealistic cache-hit rates.
export function registerUser(user) {
  const started = Date.now();
  const response = http.post(`${BASE_URL}${API}/auth/register`, JSON.stringify(user), {
    headers: jsonHeaders(),
    tags: { operation: 'register' },
  });

  const ok = check(response, {
    'register: 201': (r) => r.status === 201,
    'register: returns access token': (r) => {
      try {
        return !!r.json('tokens.access_token');
      } catch (_) {
        return false;
      }
    },
  });
  errorRate.add(!ok);
  loginDuration.add(Date.now() - started);

  if (!ok) {
    return null;
  }
  return {
    ...user,
    accessToken: response.json('tokens.access_token'),
    refreshToken: response.json('tokens.refresh_token'),
    sessionId: response.json('session_id'),
    userId: response.json('user.id'),
  };
}

export function login(user) {
  const started = Date.now();
  const response = http.post(
    `${BASE_URL}${API}/auth/login`,
    JSON.stringify({ identifier: user.username, password: user.password }),
    { headers: jsonHeaders(), tags: { operation: 'login' } },
  );

  const ok = check(response, { 'login: 200': (r) => r.status === 200 });
  errorRate.add(!ok);
  loginDuration.add(Date.now() - started);

  if (!ok) {
    return null;
  }
  return {
    accessToken: response.json('tokens.access_token'),
    refreshToken: response.json('tokens.refresh_token'),
    sessionId: response.json('session_id'),
    userId: response.json('user.id'),
  };
}

// Creates a direct conversation with `peerId`. Idempotent server-side, so calling
// it repeatedly is safe.
export function createDirectConversation(token, peerId) {
  const response = http.post(
    `${BASE_URL}${API}/conversations`,
    JSON.stringify({ participant_id: peerId }),
    { headers: jsonHeaders(token), tags: { operation: 'create_conversation' } },
  );
  const ok = check(response, {
    'conversation: created': (r) => r.status === 201 || r.status === 200,
  });
  errorRate.add(!ok);
  return ok ? response.json('id') : null;
}

export function sendMessage(token, conversationId, content) {
  const started = Date.now();
  const response = http.post(
    `${BASE_URL}${API}/messages`,
    JSON.stringify({ conversation_id: conversationId, content }),
    { headers: jsonHeaders(token), tags: { operation: 'send_message' } },
  );

  const ok = check(response, { 'message: 201': (r) => r.status === 201 });
  errorRate.add(!ok);
  messageSendDuration.add(Date.now() - started);
  return ok ? response.json('id') : null;
}

// Shared thresholds.
//
// These are *targets*, not measurements — see loadtest/README.md for how to
// establish real baselines for your own hardware before treating a breach as a
// regression.
export const commonThresholds = {
  // A failure rate above 1% under nominal load means something is genuinely wrong,
  // not merely slow.
  rtc_errors: ['rate<0.01'],
  http_req_failed: ['rate<0.01'],
};
