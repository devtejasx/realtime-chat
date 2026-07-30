// Scenario: REST messaging throughput.
//
// The core write path — persist, then broadcast. Setup registers a small pool of
// users once and shares it with every VU, because registering per-iteration would
// mean measuring bcrypt rather than messaging.
import http from 'k6/http';
import { check, sleep, group } from 'k6';
import {
  BASE_URL,
  API,
  uniqueUser,
  registerUser,
  createDirectConversation,
  sendMessage,
  jsonHeaders,
  commonThresholds,
  errorRate,
  messageListDuration,
} from './lib/common.js';

export const options = {
  scenarios: {
    messaging: {
      // Constant arrival rate: models a fixed message rate arriving at the service,
      // which is what capacity planning actually needs to answer.
      executor: 'ramping-arrival-rate',
      startRate: 20,
      timeUnit: '1s',
      preAllocatedVUs: 100,
      maxVUs: 500,
      stages: [
        { target: 50, duration: '30s' },
        { target: 200, duration: '1m' },
        { target: 200, duration: '3m' },
        { target: 400, duration: '1m' },   // push past nominal to find the knee
        { target: 0, duration: '30s' },
      ],
    },
  },
  thresholds: {
    ...commonThresholds,
    // A send is one INSERT plus one UPDATE in a single transaction, then an
    // in-memory fan-out. It should be fast; if p95 climbs, look at the connection
    // pool before the query.
    'rtc_message_send_duration': ['p(95)<150', 'p(99)<400'],
    // A history read is an index scan on (conversation_id, id DESC) — no sort.
    'rtc_message_list_duration': ['p(95)<100', 'p(99)<250'],
    'http_req_duration{operation:send_message}': ['p(95)<150'],
  },
};

// setup() runs once. Its return value is serialised to every VU, so it must stay
// small — a big fixture would be copied into every virtual user.
export function setup() {
  const users = [];
  const poolSize = Number(__ENV.USER_POOL || 10);

  for (let i = 0; i < poolSize; i += 1) {
    const created = registerUser(uniqueUser(`msg${i}`));
    if (created) {
      users.push({
        userId: created.userId,
        accessToken: created.accessToken,
        username: created.username,
      });
    }
  }

  if (users.length < 2) {
    throw new Error('setup: need at least two users; is the register rate limit too low?');
  }

  // One shared conversation. Deliberate: it concentrates writes on a single
  // conversation row, which is the worst case for the last_message_at update and
  // therefore the interesting one.
  const conversationId = createDirectConversation(users[0].accessToken, users[1].userId);
  if (!conversationId) {
    throw new Error('setup: could not create the shared conversation');
  }

  return { users, conversationId };
}

export default function (data) {
  // Spread VUs across the user pool so no single account dominates and cache-hit
  // rates stay realistic.
  const actor = data.users[__VU % data.users.length];

  group('send', () => {
    sendMessage(
      actor.accessToken,
      data.conversationId,
      `load test message from VU ${__VU} iteration ${__ITER}`,
    );
  });

  group('read history', () => {
    const started = Date.now();
    const response = http.get(
      `${BASE_URL}${API}/messages?conversation_id=${data.conversationId}&limit=50`,
      { headers: jsonHeaders(actor.accessToken), tags: { operation: 'list_messages' } },
    );
    const ok = check(response, {
      'list: 200': (r) => r.status === 200,
      'list: returns messages': (r) => {
        try {
          return Array.isArray(r.json('messages'));
        } catch (_) {
          return false;
        }
      },
    });
    errorRate.add(!ok);
    messageListDuration.add(Date.now() - started);
  });

  // Keyset pagination, which is the path a client scrolling back actually takes.
  // Worth exercising separately: deep OFFSET pagination degrades badly and this
  // confirms the cursor path does not.
  if (__ITER % 5 === 0) {
    group('paginate', () => {
      const response = http.get(
        `${BASE_URL}${API}/messages?conversation_id=${data.conversationId}&limit=50&before=1000000`,
        { headers: jsonHeaders(actor.accessToken), tags: { operation: 'list_messages_cursor' } },
      );
      errorRate.add(response.status !== 200);
    });
  }

  sleep(0.1);
}
