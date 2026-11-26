/* Service worker : la page s'ouvre aussi sans réseau.
   Réseau d'abord (toujours la dernière version), copie gardée pour le hors-ligne. */
const CACHE = 'romsounds-v3';
const SHELL = ['./', 'index.html', 'romsounds.js', 'fm1ota.js', 'preview.wasm', 'sndh.wasm', 'manifest.webmanifest', 'icon-192.png'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});
self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim()));
});
self.addEventListener('fetch', e => {
  const r = e.request;
  if (r.method !== 'GET') return;
  const u = new URL(r.url);
  if (u.origin !== location.origin || u.pathname.endsWith('latest.json')) return; /* polices, état du paquet : pas touchés */
  e.respondWith(fetch(r).then(res => {
    if (res.ok) { const cp = res.clone(); caches.open(CACHE).then(c => c.put(r, cp)); }
    return res;
  }).catch(() => caches.match(r, { ignoreSearch: true }).then(m => m || caches.match('index.html'))));
});
