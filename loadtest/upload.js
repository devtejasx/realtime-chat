// Scenario: file upload and search.
//
// Grouped together because both are heavyweight, low-frequency operations whose
// cost profile differs completely from messaging — mixing them into the messaging
// scenario would smear the percentiles and make neither readable.
//
// Uploads are deliberately run at a low arrival rate: the endpoint is rate limited
// (30/minute by default) and the point is to characterise per-upload cost, not to
// prove the limiter works.
import http from 'k6/http';
import { check, sleep, group } from 'k6';
import {
  BASE_URL,
  API,
  uniqueUser,
  registerUser,
  createDirectConversation,
  jsonHeaders,
  commonThresholds,
  errorRate,
  uploadDuration,
  searchDuration,
} from './lib/common.js';

// Deterministic payloads with correct magic bytes.
//
// The server verifies the declared content type against the file's actual leading
// bytes, so random noise labelled image/png would be rejected with 415 and the
// scenario would measure the rejection path instead of a real upload.
function pngBytes(sizeBytes) {
  const header = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  const buffer = new Uint8Array(sizeBytes);
  buffer.set(header, 0);
  for (let i = header.length; i < sizeBytes; i += 1) {
    buffer[i] = i % 251;
  }
  return buffer.buffer;
}

const SMALL_PNG = pngBytes(16 * 1024);        // 16 KiB — a typical avatar
const LARGE_PNG = pngBytes(2 * 1024 * 1024);  // 2 MiB — a typical screenshot

export const options = {
  scenarios: {
    uploads: {
      executor: 'constant-arrival-rate',
      rate: 5,
      timeUnit: '1s',
      duration: '2m',
      preAllocatedVUs: 20,
      maxVUs: 60,
      exec: 'uploadScenario',
    },
    search: {
      executor: 'constant-arrival-rate',
      rate: 20,
      timeUnit: '1s',
      duration: '2m',
      preAllocatedVUs: 20,
      maxVUs: 100,
      exec: 'searchScenario',
      startTime: '10s',  // let some content exist first
    },
  },
  thresholds: {
    ...commonThresholds,
    // Dominated by transfer plus a disk write; the thumbnail job is asynchronous
    // and must not appear in this number. If it does, thumbnailing has leaked onto
    // the request path.
    'rtc_upload_duration': ['p(95)<2000', 'p(99)<5000'],
    // A GIN index scan plus ts_headline over the matched rows. Highlighting is the
    // expensive half — compare with highlight=false to see its share.
    'rtc_search_duration': ['p(95)<300', 'p(99)<800'],
    'http_req_duration{operation:upload}': ['p(95)<2000'],
    'http_req_duration{operation:search}': ['p(95)<300'],
  },
};

export function setup() {
  const uploader = registerUser(uniqueUser('upl'));
  const peer = registerUser(uniqueUser('uplpeer'));
  if (!uploader || !peer) {
    throw new Error('setup: could not register the upload test users');
  }

  const conversationId = createDirectConversation(uploader.accessToken, peer.userId);

  // Seed content so search has something to match. Without this, every search
  // returns an empty set and measures the index-miss path rather than real work.
  const terms = ['deployment', 'kubernetes', 'postgres', 'latency', 'incident'];
  for (let i = 0; i < 40; i += 1) {
    http.post(
      `${BASE_URL}${API}/messages`,
      JSON.stringify({
        conversation_id: conversationId,
        content: `seed ${i}: the ${terms[i % terms.length]} report looks healthy today`,
      }),
      { headers: jsonHeaders(uploader.accessToken), tags: { operation: 'seed' } },
    );
  }

  return { token: uploader.accessToken, conversationId };
}

export function uploadScenario(data) {
  // Mostly small files with an occasional large one, which is the realistic mix;
  // testing only large files would overstate average cost.
  const useLarge = __ITER % 10 === 0;
  const payload = useLarge ? LARGE_PNG : SMALL_PNG;
  const filename = useLarge ? 'screenshot.png' : 'avatar.png';

  group('upload', () => {
    const started = Date.now();
    const response = http.post(
      `${BASE_URL}${API}/attachments`,
      { file: http.file(payload, filename, 'image/png') },
      {
        headers: { Authorization: `Bearer ${data.token}` },
        tags: { operation: 'upload', size: useLarge ? 'large' : 'small' },
      },
    );

    const ok = check(response, {
      // 429 is a correct answer here, not a failure: the endpoint is rate limited
      // and this scenario deliberately runs near that limit.
      'upload: accepted or rate limited': (r) => r.status === 201 || r.status === 429,
    });
    errorRate.add(!ok);
    if (response.status === 201) {
      uploadDuration.add(Date.now() - started);
    }
  });

  sleep(0.5);
}

export function searchScenario(data) {
  const terms = ['deployment', 'kubernetes', 'postgres', 'latency', 'incident', 'healthy'];
  const term = terms[__ITER % terms.length];

  group('search', () => {
    const started = Date.now();
    const response = http.get(
      `${BASE_URL}${API}/search/messages?q=${term}&limit=20`,
      { headers: jsonHeaders(data.token), tags: { operation: 'search' } },
    );

    const ok = check(response, {
      'search: 200': (r) => r.status === 200,
      'search: returns a result envelope': (r) => {
        try {
          return Array.isArray(r.json('results'));
        } catch (_) {
          return false;
        }
      },
    });
    errorRate.add(!ok);
    searchDuration.add(Date.now() - started);
  });

  // Fuzzy fallback, which only runs when the exact search matches nothing — the
  // more expensive path, worth measuring separately.
  if (__ITER % 7 === 0) {
    group('search fuzzy fallback', () => {
      const response = http.get(
        `${BASE_URL}${API}/search/messages?q=kubernetez&fuzzy=true&limit=20`,
        { headers: jsonHeaders(data.token), tags: { operation: 'search_fuzzy' } },
      );
      errorRate.add(response.status !== 200);
    });
  }

  sleep(0.2);
}
