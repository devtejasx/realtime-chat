// Scenario: WebSocket concurrency and delivery latency.
//
// This is the scenario that actually characterises the service. The REST scenarios
// measure request throughput; this one measures how many *concurrent connections*
// an instance sustains and how quickly a sent message reaches another connected
// client — which is the number users perceive as "is this chat app fast".
//
// Note the executor choice: constant-VUs, not arrival-rate. A WebSocket VU holds a
// connection open for its whole iteration, so VU count maps directly to concurrent
// connections, which is the quantity being measured.
import ws from 'k6/ws';
import { check, sleep } from 'k6';
import {
  WS_URL,
  uniqueUser,
  registerUser,
  createDirectConversation,
  commonThresholds,
  errorRate,
  wsConnectDuration,
  wsMessageLatency,
  wsMessagesReceived,
  wsMessagesSent,
} from './lib/common.js';

// How long each VU keeps its socket open. Long enough to exercise the 30s
// heartbeat at least once, so the ping/pong path is covered rather than assumed.
const CONNECTION_SECONDS = Number(__ENV.WS_HOLD_SECONDS || 45);

export const options = {
  scenarios: {
    websockets: {
      executor: 'ramping-vus',
      startVUs: 0,
      stages: [
        { target: 50, duration: '30s' },
        { target: 200, duration: '1m' },
        { target: 500, duration: '2m' },
        { target: 500, duration: '2m' },  // sustain at peak
        { target: 0, duration: '30s' },
      ],
      gracefulRampDown: '30s',
    },
  },
  thresholds: {
    ...commonThresholds,
    // Handshake includes JWT verification and the room subscription query.
    'rtc_ws_connect_duration': ['p(95)<500', 'p(99)<1500'],
    // Round trip from sending a frame to receiving the broadcast back. This is the
    // headline latency number for the product.
    'rtc_ws_message_latency': ['p(95)<200', 'p(99)<500'],
    'ws_connecting': ['p(95)<500'],
    // A session dropped mid-test means the server closed it — the one failure mode
    // that matters most here.
    'ws_session_duration': [`p(50)>${(CONNECTION_SECONDS - 5) * 1000}`],
  },
};

export function setup() {
  const alice = registerUser(uniqueUser('wsa'));
  const bob = registerUser(uniqueUser('wsb'));
  if (!alice || !bob) {
    throw new Error('setup: could not register the WebSocket test users');
  }

  const conversationId = createDirectConversation(alice.accessToken, bob.userId);
  if (!conversationId) {
    throw new Error('setup: could not create the shared conversation');
  }

  return {
    token: alice.accessToken,
    conversationId,
  };
}

export default function (data) {
  // protocol=2 requests the full envelope, which carries the request_id needed to
  // correlate a reply with the command that caused it — that correlation is what
  // makes an accurate latency measurement possible at all. Set WS_PROTOCOL=1 to
  // load-test the legacy format.
  const protocol = __ENV.WS_PROTOCOL || '2';
  const url = `${WS_URL}/api/v1/ws?token=${data.token}&protocol=${protocol}`;

  const connectStarted = Date.now();
  const pending = {};   // request_id -> send timestamp

  const response = ws.connect(url, {}, (socket) => {
    socket.on('open', () => {
      wsConnectDuration.add(Date.now() - connectStarted);

      // Send a message roughly every two seconds for the life of the connection.
      socket.setInterval(() => {
        const requestId = `${__VU}-${Date.now()}`;
        pending[requestId] = Date.now();

        const frame = protocol === '2'
          ? JSON.stringify({
              event: 'message.send',
              request_id: requestId,
              payload: {
                conversation_id: data.conversationId,
                content: `ws load ${requestId}`,
              },
            })
          : JSON.stringify({
              type: 'message.send',
              data: {
                conversation_id: data.conversationId,
                content: `ws load ${requestId}`,
              },
            });

        socket.send(frame);
        wsMessagesSent.add(1);
      }, 2000);

      // Application-level ping, exercising the heartbeat path.
      socket.setInterval(() => {
        socket.send(
          protocol === '2'
            ? JSON.stringify({ event: 'ping', payload: {} })
            : JSON.stringify({ type: 'ping', data: {} }),
        );
      }, 15000);

      // Close cleanly at the end of the hold window; an abrupt teardown would look
      // like a server-side drop in the metrics.
      socket.setTimeout(() => socket.close(), CONNECTION_SECONDS * 1000);
    });

    socket.on('message', (raw) => {
      wsMessagesReceived.add(1);

      let frame;
      try {
        frame = JSON.parse(raw);
      } catch (_) {
        errorRate.add(true);
        return;
      }

      // Both protocol shapes, so this scenario works against either.
      const event = frame.event || frame.type;
      const payload = frame.payload || frame.data || {};

      if (event === 'ready') {
        check(frame, {
          'ws: server confirms the negotiated protocol': () =>
            protocol === '1' || payload.protocol_version === 2,
        });
        return;
      }

      if (event === 'error') {
        errorRate.add(true);
        return;
      }

      // Measure the round trip on the broadcast of our own message. Matching on
      // content rather than request_id because a *broadcast* is server-initiated
      // and carries no request id — it is a fan-out, not a reply.
      if (event === 'message.created' && typeof payload.content === 'string') {
        const match = payload.content.match(/ws load (\d+-\d+)/);
        if (match && pending[match[1]]) {
          wsMessageLatency.add(Date.now() - pending[match[1]]);
          delete pending[match[1]];
        }
      }
    });

    socket.on('error', (e) => {
      // k6 reports a normal close as an error on some platforms; filter it out so
      // the error rate reflects genuine failures.
      if (e && e.error && e.error() !== 'websocket: close sent') {
        errorRate.add(true);
      }
    });
  });

  check(response, { 'ws: handshake accepted (101)': (r) => r && r.status === 101 });
  sleep(1);
}
