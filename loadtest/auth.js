// Scenario: authentication.
//
// Deliberately the smallest-throughput scenario in the suite, because login is
// *intentionally* the slowest endpoint: bcrypt is designed to be expensive, and a
// login that is fast is a login that is cheap to brute-force. The thresholds below
// reflect that — a p95 of 500 ms for login would be alarming anywhere else and is
// entirely normal here.
//
// What this scenario is really testing is that the expensive path is bounded: that
// the rate limiter engages, that token rotation works, and that concurrent logins
// do not starve the connection pool.
import http from 'k6/http';
import { check, sleep, group } from 'k6';
import {
  BASE_URL,
  API,
  uniqueUser,
  registerUser,
  login,
  jsonHeaders,
  commonThresholds,
  errorRate,
} from './lib/common.js';

export const options = {
  scenarios: {
    // Ramping arrival rate, not fixed VUs: it models "N logins per second arrive"
    // rather than "N users loop as fast as they can", which is what actually
    // happens at a login endpoint and keeps the load independent of response time.
    authentication: {
      executor: 'ramping-arrival-rate',
      startRate: 5,
      timeUnit: '1s',
      preAllocatedVUs: 50,
      maxVUs: 200,
      stages: [
        { target: 10, duration: '30s' },  // warm up
        { target: 30, duration: '1m' },   // nominal
        { target: 30, duration: '2m' },   // sustain
        { target: 0, duration: '30s' },   // ramp down
      ],
    },
  },
  thresholds: {
    ...commonThresholds,
    // bcrypt dominates. These are generous on purpose.
    'rtc_login_duration': ['p(95)<500', 'p(99)<1000'],
    'http_req_duration{operation:register}': ['p(95)<600'],
    'http_req_duration{operation:login}': ['p(95)<500'],
    'http_req_duration{operation:refresh}': ['p(95)<50'],
    // Token refresh is a hash comparison and two updates — no bcrypt — so it
    // should be an order of magnitude faster than login. If it is not, the session
    // lookup is missing an index.
    'http_req_duration{operation:me}': ['p(95)<50'],
  },
};

export default function () {
  const user = uniqueUser('auth');
  let session = null;

  group('register', () => {
    session = registerUser(user);
  });

  if (!session) {
    // Registration failed (very likely the register rate limit). Back off rather
    // than hammering — retrying immediately would just deepen the queue.
    sleep(2);
    return;
  }

  group('authenticated read', () => {
    const response = http.get(`${BASE_URL}${API}/auth/me`, {
      headers: jsonHeaders(session.accessToken),
      tags: { operation: 'me' },
    });
    const ok = check(response, {
      'me: 200': (r) => r.status === 200,
      'me: correct user': (r) => r.json('username') === user.username,
      // Confirms the version middleware stamped the response.
      'me: X-API-Version present': (r) => !!r.headers['X-Api-Version'],
    });
    errorRate.add(!ok);
  });

  group('token rotation', () => {
    const response = http.post(
      `${BASE_URL}${API}/auth/refresh`,
      JSON.stringify({ refresh_token: session.refreshToken, session_id: session.sessionId }),
      { headers: jsonHeaders(), tags: { operation: 'refresh' } },
    );
    const ok = check(response, {
      'refresh: 200': (r) => r.status === 200,
      'refresh: rotates the refresh token': (r) => {
        // Rotation is what makes a stolen-then-used token detectable; if the value
        // came back unchanged, replay protection is not working.
        try {
          return r.json('refresh_token') !== session.refreshToken;
        } catch (_) {
          return false;
        }
      },
    });
    errorRate.add(!ok);

    if (ok) {
      session.accessToken = response.json('access_token');
      session.refreshToken = response.json('refresh_token');
    }
  });

  group('re-login', () => {
    const relogin = login(user);
    check(relogin, { 'relogin: succeeded': (r) => r !== null });
  });

  group('logout', () => {
    const response = http.post(
      `${BASE_URL}${API}/auth/logout`,
      JSON.stringify({ session_id: session.sessionId }),
      { headers: jsonHeaders(session.accessToken), tags: { operation: 'logout' } },
    );
    check(response, { 'logout: 200': (r) => r.status === 200 });
  });

  sleep(1);
}
