/* Physys Lab — Service Worker (Network First + Offline Fallback) */

const CACHE_NAME = 'physys-V_1_16_07_26-1788146350';
const CACHE_FILES = [
    '/',
    '/index.html',
    '/styles.css',
    '/app.js',
    '/firebase-sync.js',
    '/manifest.json'
];

// Install: cache all static assets
self.addEventListener('install', event => {
    event.waitUntil(
        caches.open(CACHE_NAME)
            .then(cache => cache.addAll(CACHE_FILES))
            .then(() => self.skipWaiting())
    );
});

// Activate: clean old caches
self.addEventListener('activate', event => {
    event.waitUntil(
        caches.keys().then(keys =>
            Promise.all(
                keys.filter(k => k !== CACHE_NAME).map(k => caches.delete(k))
            )
        ).then(() => self.clients.claim())
    );
});

// Fetch: NETWORK-FIRST for everything (static + API)
// Tries to get fresh content from ESP32; falls back to cache if offline
self.addEventListener('fetch', event => {
    const url = new URL(event.request.url);

    // WebSocket and non-GET: pass through
    if (event.request.method !== 'GET') return;

    event.respondWith(
        fetch(event.request)
            .then(response => {
                // Got fresh response — update cache
                if (response.ok) {
                    const clone = response.clone();
                    caches.open(CACHE_NAME).then(cache => cache.put(event.request, clone));
                }
                return response;
            })
            .catch(() => {
                // Network failed — serve from cache (offline mode)
                return caches.match(event.request).then(cached => {
                    if (cached) return cached;
                    // API calls return JSON error
                    if (url.pathname.startsWith('/api/')) {
                        return new Response(JSON.stringify({ error: 'offline' }),
                            { headers: { 'Content-Type': 'application/json' } });
                    }
                    return new Response('Offline', { status: 503 });
                });
            })
    );
});
