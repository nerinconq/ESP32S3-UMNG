/* ═══════════════════════════════════════════════
   Physys Lab v7.0 — Firebase Sync Module (H5)
   IndexedDB cache + Firestore cloud sync
   ═══════════════════════════════════════════════ */

// Firebase configuration (verified from physys-lab-umng project)
const FIREBASE_CONFIG = {
    apiKey: "AIzaSyCZgC3iftMeHMw_MHVhqvTabZ3Z1XffiSM",
    authDomain: "physys-lab-umng.firebaseapp.com",
    projectId: "physys-lab-umng",
    storageBucket: "physys-lab-umng.firebasestorage.app",
    messagingSenderId: "79007327051",
    appId: "1:79007327051:web:01359f2b07d8c8f71cfece"
};

// ─── IndexedDB (Offline Persistence) ───
const DB_NAME = 'physys-lab-cache';
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

/**
 * Saves a completed experiment to IndexedDB for later synchronization.
 * @param {Object} experiment - The data to save.
 */
async function saveToIndexedDB(experiment) {
    try {
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
        
        console.log('[IndexedDB] Experiment saved:', record.id);
        updateSyncBadge();
        
        // Auto-trigger sync if online (and not just connected to ESP32 AP)
        if (navigator.onLine) {
            // We use a small delay to not compete with final WS messages
            setTimeout(syncToCloud, 1000);
        }
        return record.id;
    } catch (err) {
        console.error('[IndexedDB] Error saving experiment:', err);
    }
}

async function getPendingExperiments() {
    try {
        const db = await openDB();
        const tx = db.transaction(STORE_NAME, 'readonly');
        const store = tx.objectStore(STORE_NAME);
        const idx = store.index('synced');
        const req = idx.getAll(false);
        return new Promise((resolve) => {
            req.onsuccess = () => { db.close(); resolve(req.result); };
            req.onerror = () => { db.close(); resolve([]); };
        });
    } catch (e) {
        return [];
    }
}

async function markSynced(id) {
    const db = await openDB();
    const tx = db.transaction(STORE_NAME, 'readwrite');
    const store = tx.objectStore(STORE_NAME);
    const getReq = store.get(id);
    const record = await new Promise(r => { getReq.onsuccess = () => r(getReq.result); });
    
    if (record) {
        record.synced = true;
        store.put(record);
    }
    await new Promise((res) => { tx.oncomplete = res; });
    db.close();
}

async function countPending() {
    const pending = await getPendingExperiments();
    return pending.length;
}

// ─── Firebase Synchronization ───
let firebaseReady = false;
let firebaseApp = null;
let firebaseDb = null;
let fbModules = null;
let isSyncing = false;

/**
 * Loads Firebase SDK dynamically. 
 * Includes a "real internet" check to avoid hanging in ESP32 AP mode.
 */
async function loadFirebaseSDK() {
    if (firebaseReady) return true;
    if (!navigator.onLine) return false;

    // Fast check: can we reach Google?
    const hasInternet = await fetch('https://www.google.com/favicon.ico', { mode: 'no-cors', cache: 'no-store' })
        .then(() => true)
        .catch(() => false);
    
    if (!hasInternet) {
        console.warn('[Firebase] Connected to WiFi but no Internet access (likely ESP32 AP mode).');
        return false;
    }

    try {
        const { initializeApp } = await import('https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js');
        const { getFirestore, doc, setDoc, serverTimestamp, collection } = await import('https://www.gstatic.com/firebasejs/10.12.0/firebase-firestore.js');
        const { getAuth, signInAnonymously } = await import('https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js');

        firebaseApp = initializeApp(FIREBASE_CONFIG);
        const auth = getAuth(firebaseApp);
        firebaseDb = getFirestore(firebaseApp);
        fbModules = { doc, setDoc, serverTimestamp, collection };

        await signInAnonymously(auth);
        firebaseReady = true;
        console.log('[Firebase] Auth successful (Anonymous)');
        return true;
    } catch (e) {
        console.warn('[Firebase] SDK Load/Auth failed:', e.message);
        return false;
    }
}

async function syncToCloud() {
    if (isSyncing || !navigator.onLine) return 0;
    isSyncing = true;

    const badge = document.getElementById('sync-badge');
    if (badge) { 
        badge.textContent = '☁️ Sincronizando...'; 
        badge.classList.add('syncing'); 
    }

    const ready = await loadFirebaseSDK();
    if (!ready) {
        isSyncing = false;
        updateSyncBadge();
        return 0;
    }

    const pending = await getPendingExperiments();
    if (pending.length === 0) {
        isSyncing = false;
        updateSyncBadge();
        return 0;
    }

    let syncCount = 0;
    const { doc, setDoc, serverTimestamp } = fbModules;

    for (const record of pending) {
        try {
            // Using setDoc with record.id for idempotency (prevents duplicates on retry)
            await setDoc(doc(firebaseDb, 'experiments', record.id), {
                ...record.data,
                localId: record.id,
                cloudSyncedAt: serverTimestamp(),
                createdAt: record.timestamp,
                deviceUA: navigator.userAgent,
                labName: localStorage.getItem('physys_lab_name') || 'Physys-Lab',
                institution: localStorage.getItem('physys_institution') || 'UMNG'
            }, { merge: true });
            
            await markSynced(record.id);
            syncCount++;
        } catch (err) {
            console.error('[Firebase] Failed to sync record:', record.id, err);
            // Break loop on serious auth/permission errors
            if (err.code === 'permission-denied') break;
        }
    }

    console.log(`[Firebase] Sync complete. ${syncCount} records uploaded.`);
    isSyncing = false;
    updateSyncBadge();
    return syncCount;
}

// ─── UI Integration ───
async function updateSyncBadge() {
    const badge = document.getElementById('sync-badge');
    if (!badge) return;

    const n = await countPending();
    badge.classList.remove('syncing', 'offline', 'pending', 'synced');

    if (!navigator.onLine) {
        badge.textContent = '☁️ Sin conexión';
        badge.classList.add('offline');
    } else if (n > 0) {
        badge.textContent = `☁️ ${n} pendiente${n > 1 ? 's' : ''}`;
        badge.classList.add('pending');
    } else {
        badge.textContent = '☁️ Sincronizado';
        badge.classList.add('synced');
    }
}

/**
 * Initializes the Firebase synchronization module.
 */
function initFirebaseSync() {
    updateSyncBadge();

    window.addEventListener('online', () => {
        console.log('[Firebase] Online detected. Starting sync...');
        syncToCloud();
    });

    window.addEventListener('offline', () => {
        console.log('[Firebase] Offline detected.');
        updateSyncBadge();
    });

    // Sync on startup if online
    if (navigator.onLine) {
        setTimeout(syncToCloud, 3000);
    }

    // Manual trigger
    const badge = document.getElementById('sync-badge');
    if (badge) {
        badge.addEventListener('click', () => {
            if (isSyncing) return;
            if (navigator.onLine) {
                syncToCloud();
            } else {
                alert('No hay conexión a internet para sincronizar.');
            }
        });
    }
}

