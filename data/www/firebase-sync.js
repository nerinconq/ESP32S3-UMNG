/* ═══════════════════════════════════════════════
   Physys Lab v6.1 — Firebase Sync Module (H5)
   IndexedDB cache + Firestore cloud sync
   ═══════════════════════════════════════════════ */

// Firebase config
const FIREBASE_CONFIG = {
    apiKey: "AIzaSyCZgC3iftMeHMw_MHVhqvTabZ3Z1XffiSM",
    authDomain: "physys-lab-umng.firebaseapp.com",
    projectId: "physys-lab-umng",
    storageBucket: "physys-lab-umng.firebasestorage.app",
    messagingSenderId: "79007327051",
    appId: "1:79007327051:web:01359f2b07d8c8f71cfece"
};

// ─── IndexedDB (offline cache) ───
const DB_NAME = 'physys-sync';
const DB_VERSION = 1;
const STORE_NAME = 'experiments';

function openDB() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB_NAME, DB_VERSION);
        req.onupgradeneeded = (e) => {
            const db = e.target.result;
            if (!db.objectStoreNames.contains(STORE_NAME)) {
                const store = db.createObjectStore(STORE_NAME, { keyPath: 'id' });
                store.createIndex('synced', 'synced', { unique: false });
                store.createIndex('timestamp', 'timestamp', { unique: false });
            }
        };
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    });
}

// Save experiment to IndexedDB
async function saveToIndexedDB(experiment) {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readwrite');
    const store = tx.objectStore(STORE_NAME);
    const record = {
        id: 'exp_' + Date.now() + '_' + Math.random().toString(36).substr(2, 5),
        data: experiment,
        timestamp: Date.now(),
        synced: false
    };
    store.put(record);
    await new Promise((res, rej) => { tx.oncomplete = res; tx.onerror = rej; });
    db.close();
    updateSyncBadge();
    return record.id;
}

// Get pending (unsynced) experiments
async function getPendingExperiments() {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readonly');
    const store = tx.objectStore(STORE_NAME);
    const idx = store.index('synced');
    const req = idx.getAll(false);
    return new Promise((resolve) => {
        req.onsuccess = () => { db.close(); resolve(req.result); };
        req.onerror = () => { db.close(); resolve([]); };
    });
}

// Mark experiment as synced
async function markSynced(id) {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readwrite');
    const store = tx.objectStore(STORE_NAME);
    const record = await new Promise(r => { const g = store.get(id); g.onsuccess = () => r(g.result); });
    if (record) {
        record.synced = true;
        store.put(record);
    }
    await new Promise((res) => { tx.oncomplete = res; });
    db.close();
}

// Count pending
async function countPending() {
    const pending = await getPendingExperiments();
    return pending.length;
}

// ─── Firebase Cloud Sync ───
let firebaseReady = false;
let firebaseApp = null;
let firebaseAuth = null;
let firebaseDb = null;

async function loadFirebaseSDK() {
    if (firebaseReady) return true;
    try {
        // Dynamic import from CDN (only when online)
        const { initializeApp } = await import('https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js');
        const { getFirestore, collection, addDoc } = await import('https://www.gstatic.com/firebasejs/10.12.0/firebase-firestore.js');
        const { getAuth, signInAnonymously } = await import('https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js');

        firebaseApp = initializeApp(FIREBASE_CONFIG);
        firebaseAuth = getAuth(firebaseApp);
        firebaseDb = getFirestore(firebaseApp);

        // Sign in anonymously
        await signInAnonymously(firebaseAuth);

        // Store module references globally
        window._fbModules = { collection, addDoc };

        firebaseReady = true;
        console.log('[Firebase] SDK loaded, auth OK');
        return true;
    } catch (e) {
        console.warn('[Firebase] SDK load failed (offline?):', e.message);
        return false;
    }
}

async function syncToCloud() {
    if (!navigator.onLine) return 0;

    const badge = document.getElementById('sync-badge');
    if (badge) { badge.textContent = '☁️ Sincronizando...'; badge.className = 'sync-badge syncing'; }

    const loaded = await loadFirebaseSDK();
    if (!loaded) {
        if (badge) { badge.textContent = '☁️ Sin conexión'; badge.className = 'sync-badge offline'; }
        return 0;
    }

    const pending = await getPendingExperiments();
    if (pending.length === 0) {
        if (badge) { badge.textContent = '☁️ Sincronizado'; badge.className = 'sync-badge synced'; }
        return 0;
    }

    const { collection, addDoc } = window._fbModules;
    let synced = 0;

    for (const record of pending) {
        try {
            await addDoc(collection(firebaseDb, 'experiments'), {
                ...record.data,
                localId: record.id,
                syncedAt: new Date().toISOString(),
                deviceId: record.data.device || 'Physys-Lab'
            });
            await markSynced(record.id);
            synced++;
        } catch (e) {
            console.warn('[Firebase] Sync error for', record.id, e.message);
        }
    }

    console.log(`[Firebase] Synced ${synced}/${pending.length} experiments`);
    updateSyncBadge();
    return synced;
}

// ─── UI Badge ───
async function updateSyncBadge() {
    const badge = document.getElementById('sync-badge');
    if (!badge) return;

    const n = await countPending();
    if (n === 0) {
        badge.textContent = '☁️ Sincronizado';
        badge.className = 'sync-badge synced';
    } else {
        badge.textContent = '☁️ ' + n + ' pendiente' + (n > 1 ? 's' : '');
        badge.className = 'sync-badge pending';
    }
}

// ─── Auto-sync on connectivity change ───
function initFirebaseSync() {
    // Update badge on load
    updateSyncBadge();

    // Sync when going online
    window.addEventListener('online', () => {
        console.log('[Sync] Online detected — syncing...');
        syncToCloud();
    });

    // Update badge when going offline
    window.addEventListener('offline', () => {
        const badge = document.getElementById('sync-badge');
        if (badge) { badge.textContent = '☁️ Offline'; badge.className = 'sync-badge offline'; }
    });

    // Try initial sync if already online
    if (navigator.onLine) {
        setTimeout(() => syncToCloud(), 3000);
    }

    // Manual sync button
    const badge = document.getElementById('sync-badge');
    if (badge) {
        badge.addEventListener('click', () => {
            if (navigator.onLine) syncToCloud();
        });
        badge.title = 'Click para sincronizar manualmente';
        badge.style.cursor = 'pointer';
    }
}
