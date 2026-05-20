/* ═══════════════════════════════════════════════════════
   Physys Lab v9.0 — App Controller
   WebSocket Streaming + Canvas Chart + Sensor Dropdown
   Per-sensor controls + Time unit selector + Export modal
   ═══════════════════════════════════════════════════════ */

// ─── Tab Definitions ───
const TAB_CONFIG = {
    movimiento: {
        title: 'Cinemática Lineal',
        sensor: 'TOF',
        variables: [
            { key: 'dist', label: 'Posición', unit: 'mm', color: '#00d4ff' },
            { key: 'vel', label: 'Rapidez', unit: 'm/s', color: '#14f0c5' },
            { key: 'acc', label: 'Aceleración', unit: 'm/s²', color: '#a78bfa' }
        ],
        subMetrics: [
            { key: 'vel', label: 'Rapidez', unit: 'm/s' },
            { key: 'acc', label: 'Aceleración', unit: 'm/s²' }
        ]
    },
    rotacion: {
        title: 'Cinemática Angular',
        sensor: 'ENC',
        variables: [
            { key: 'angleDeg', label: 'Ángulo (°)', unit: '°', color: '#00d4ff' },
            { key: 'angleRad', label: 'Ángulo (rad)', unit: 'rad', color: '#00d4ff' },
            { key: 'angVel', label: 'Vel. Angular', unit: 'rad/s', color: '#14f0c5' },
            { key: 'angAcc', label: 'Acel. Angular', unit: 'rad/s²', color: '#a78bfa' }
        ],
        subMetrics: [
            { key: 'angVel', label: 'Vel. Angular', unit: 'rad/s' },
            { key: 'angAcc', label: 'Acel. Angular', unit: 'rad/s²' }
        ]
    },
    fuerza: {
        title: 'Dinámica',
        sensor: 'HX',
        variables: [
            { key: 'weight', label: 'Masa (g)', unit: 'g', color: '#f0b429' },
            { key: 'mass', label: 'Masa (kg)', unit: 'kg', color: '#f0b429' },
            { key: 'weightN', label: 'Peso Vertical', unit: 'N', color: '#ff4d6a' }
        ],
        subMetrics: [
            { key: 'mass', label: 'Masa', unit: 'kg' },
            { key: 'weightN', label: 'Peso (N)', unit: 'N' }
        ]
    }
};

// ─── Per-sensor recording state ───
const sensorRecording = { TOF: false, ENC: false, HX: false };
const sensorData = { TOF: [], ENC: [], HX: [] };

// ─── Time unit ───
let timeUnit = 'ms'; // 'ms', 's', 'min'
function convertTime(ms) {
    if (timeUnit === 's') return ms / 1000;
    if (timeUnit === 'min') return ms / 60000;
    return ms;
}
function timeUnitLabel() {
    if (timeUnit === 's') return 's';
    if (timeUnit === 'min') return 'min';
    return 'ms';
}

// ─── State ───
let currentTab = 'movimiento';
let lastSensorTab = 'movimiento';
let selectedVariable = 'dist';
let ws = null;
let isRecording = false;
let lowPowerMode = false;
let recordedData = [];
let chartData = [];
const MAX_CHART_POINTS = 200;
let lastData = {};
let currentChartStartTime = null;
let lastDrawTime = 0; // Para throttling de dibujo

// --- MULTI-USER ROLES & HARDWARE PROFILE VARIABLES ---
let currentUserRole = sessionStorage.getItem('user_role') || "student";
let isLeaderActive = false;
let leaderIp = "";
let selectedAuthRole = "student";
let activeHardwareProfile = { camera_detected: false, type: "standard_base", pins: {} };

// ─── DOM Elements ───
const $ = id => document.getElementById(id);
const $$ = sel => document.querySelectorAll(sel);

// ─── Status Semaphore (Sync with Logo) ───
function setStatus(text, color) {
    const statusEl = $('sys-status');
    const logoEl = $('sys-logo');
    if (statusEl) {
        statusEl.textContent = text;
        statusEl.style.color = color;
    }
    if (logoEl) {
        logoEl.style.borderColor = color;
        logoEl.style.boxShadow = `0 0 15px ${color}66`; // 66 adds alpha to the hex
    }
}

// ─── WebSocket Connection ───
function connectWS() {
    const host = location.hostname || '192.168.4.1';
    ws = new WebSocket('ws://' + host + '/ws');

    ws.onopen = () => {
        setStatus('Conectado', '#14f0c5');
        updateSensorBadges({ tof: true, encoder: true, loadcell: true });
        syncRoleUI();
    };

    ws.onclose = () => {
        setStatus('Desconectado — Reconectando...', '#ff4d6a');
        setTimeout(connectWS, 2000);
    };

    ws.onerror = () => {
        ws.close();
    };

    ws.onmessage = (evt) => {
        try {
            const data = JSON.parse(evt.data);
            
            // --- NUEVOS CONTROLADORES DE ROL Y HARDWARE ---
            if (data.auth) {
                if (data.auth === "leader" && data.status === "success") {
                    currentUserRole = "leader";
                    sessionStorage.setItem('user_role', 'leader');
                    showNotification("⭐ Autenticado como Líder de Mesa", "#eab308");
                    closeAuthModal();
                    syncRoleUI();
                } else if (data.auth === "teacher" && data.status === "success") {
                    currentUserRole = "teacher";
                    sessionStorage.setItem('user_role', 'teacher');
                    showNotification("🎓 Autenticado como Docente (Admin)", "#38bdf8");
                    closeAuthModal();
                    syncRoleUI();
                } else if (data.auth === "student" && data.status === "busy") {
                    showNotification("⚠️ ¡La mesa ya tiene un líder activo desde IP: " + data.ip + "!", "#ff4d6a");
                } else if (data.auth === "student" && data.status === "fail") {
                    showNotification("❌ PIN o Contraseña incorrecta", "#ff4d6a");
                } else if (data.auth === "student") {
                    currentUserRole = "student";
                    sessionStorage.setItem('user_role', 'student');
                    showNotification("🔒 Sesión cerrada. Rol de Estudiante activo.", "#94a3b8");
                    syncRoleUI();
                }
                return;
            }
            
            if (data.status) {
                if (data.status === "leader_active") {
                    isLeaderActive = true;
                    leaderIp = data.ip;
                    updateLeaderStatusBanner();
                    showNotification("⭐ Líder de Mesa activo (IP: " + data.ip + ")", "#eab308");
                } else if (data.status === "leader_deauthorized" || data.status === "leader_disconnected" || data.status === "leader_timeout") {
                    isLeaderActive = false;
                    leaderIp = "";
                    if (currentUserRole === "leader") {
                        currentUserRole = "student";
                        sessionStorage.setItem('user_role', 'student');
                        syncRoleUI();
                    }
                    updateLeaderStatusBanner();
                    if (data.status === "leader_timeout") {
                        showNotification("🔒 Control liberado por inactividad del Líder", "#ff4d6a");
                    } else {
                        showNotification("🔓 Control de mesa liberado", "#14f0c5");
                    }
                } else if (data.status === "teacher_timeout" || data.status === "teacher_disconnected") {
                    if (currentUserRole === "teacher") {
                        currentUserRole = "student";
                        sessionStorage.setItem('user_role', 'student');
                        syncRoleUI();
                        showNotification("🔒 Sesión de Docente cerrada por inactividad o desconexión", "#ff4d6a");
                    }
                } else if (data.status === "pins_updated") {
                    showNotification("🔌 Pines de hardware actualizados correctamente. Reiniciando tarjeta...", "#38bdf8");
                    return;
                }
            }

            if (data.hardware) {
                activeHardwareProfile = data.hardware;
                updateHardwareStatusBanner();
                populatePinSelectors();
            }
            
            if (data.error === "unauthorized") {
                showNotification("⚠️ Acción rechazada: La mesa está controlada por el Líder activo en IP " + data.leader_ip, "#ff4d6a");
                return;
            }
            
            if (data.command === 'START' || data.command === 'RESET') {
                currentChartStartTime = null;
                chartData = [];
                resetAngleState();
                return;
            }
            
            if (data.command === 'WAITING_TRIGGER') {
                setStatus('Esperando movimiento...', '#f0b429');
                $('btn-trigger').classList.add('recording');
                $('btn-trigger').innerHTML = '🎯 Esperando...';
                $('btn-trigger').style.display = 'none';
                $('btn-trigger-stop').style.display = '';
                return;
            }

            if (data.command === 'TRIGGER_START') {
                setStatus('Grabando (Auto)', '#ff4d6a'); // Cambiado a rojo para grabar
                $('btn-trigger-stop').innerHTML = '⏹ Grabando...';
                sensorRecording.TOF = true;
                sensorData.TOF = [];
                chartData = [];
                currentChartStartTime = null;
                return;
            }

            if (data.command === 'TRIGGER_STOP') {
                setStatus('Toma completa', '#a78bfa');
                $('btn-trigger').classList.remove('recording');
                $('btn-trigger').innerHTML = '🎯 Toma Auto';
                $('btn-trigger').style.display = '';
                $('btn-trigger-stop').style.display = 'none';
                
                ['TOF', 'ENC', 'HX'].forEach(s => sensorRecording[s] = false);
                
                const btnRec = $('btn-sensor-rec');
                if (btnRec) {
                    btnRec.classList.remove('recording');
                    btnRec.innerHTML = '<span class="dot"></span> ▶️ Medir';
                }
                
                if (isRecording) {
                    isRecording = false;
                    const gBtn = $('btn-record');
                    if (gBtn) {
                        gBtn.classList.remove('recording');
                        gBtn.innerHTML = '<span class="dot"></span> Grabar';
                    }
                }
                return;
            }

            // Ignorar actualizaciones si la pestaña actual no es de datos (config, python, etc)
            // a menos que sean mensajes de configuración.
            const isDataTab = ['movimiento', 'rotacion', 'fuerza'].includes(currentTab);

            if (currentChartStartTime === null && typeof data.t === 'number') {
                currentChartStartTime = data.t;
            }
            if (typeof data.t === 'number') {
                data.rawT = data.t;
                data.t = data.t - currentChartStartTime;
            }
            
            lastData = data;

            // Grabación por sensor (siempre activa en segundo plano si está marcada)
            if (sensorRecording.TOF) sensorData.TOF.push({ ...data, recorded: Date.now() });
            if (sensorRecording.ENC) sensorData.ENC.push({ ...data, recorded: Date.now() });
            if (sensorRecording.HX)  sensorData.HX.push({ ...data, recorded: Date.now() });
            
            if (isRecording) {
                recordedData.push({ ...data, recorded: Date.now() });
            }

            // Actualizar UI solo si es necesario
            if (isDataTab) {
                updateSensorCount();
                updateDisplay(data);
                pushChartPoint(data);
                
                const now = Date.now();
                if (now - lastDrawTime > 60) { // ~16 FPS para la gráfica es suficiente y suave
                    drawChart();
                    lastDrawTime = now;
                }
            } else if (data.config) {
                // Si estamos en Config, solo actualizar los badges y checkboxes
                updateDisplay(data);
            }
        } catch (e) {
            console.warn('[WS] Parse error', e);
        }
    };
}

// ─── Display Update ───
function updateDisplay(data) {
    const tabConf = TAB_CONFIG[currentTab];
    if (!tabConf) return;
    const varConf = tabConf.variables.find(v => v.key === selectedVariable);
    if (!varConf) return;

    const val = data[selectedVariable];
    $('main-val').textContent = typeof val === 'number' ? val.toFixed(2) : '---';
    $('main-unit').textContent = varConf.unit;

    // Sub-metrics
    tabConf.subMetrics.forEach((m, i) => {
        const idx = i + 1;
        $('sub-label-' + idx).textContent = m.label;
        const mVal = data[m.key];
        $('sub-val-' + idx).innerHTML =
            (typeof mVal === 'number' ? mVal.toFixed(3) : '---') +
            ' <small>' + m.unit + '</small>';
    });

    // Sensor badges
    if (data.sensors) {
        updateSensorBadges(data.sensors);
    }
    
    // Config updates (feedback loop protection)
    if (data.config) {
        // Solo actualizamos si el usuario NO está interactuando con el panel de configuración
        const configVisible = $('config-panel') && $('config-panel').style.display !== 'none';
        if (configVisible) {
            // Si el panel de configuración está activo, bloqueamos actualizaciones por WebSocket para evitar feedback loops
            return;
        }
        const tofEl = $('tof_model');
        const srEl = $('sample_rate');
        
        if (!configVisible || (tofEl && document.activeElement !== tofEl)) {
            if (data.config.tof_model && tofEl && tofEl.value != data.config.tof_model) {
                tofEl.value = data.config.tof_model;
            }
        }
        
        if (!configVisible || (srEl && document.activeElement !== srEl)) {
            // Si recibimos sample_rate, nos aseguramos de que el selector lo refleje
            if (data.config.sample_rate && srEl && srEl.value != data.config.sample_rate) {
                srEl.value = data.config.sample_rate;
            } else if (!data.config.sample_rate && srEl) {
                srEl.value = "10"; // Default to 100Hz (10ms) if not set
            }
        }

        const usbLogEl = $('usb-auto-log');
        if (data.config.usb_log !== undefined && usbLogEl && (!configVisible || document.activeElement !== usbLogEl)) {
            usbLogEl.checked = data.config.usb_log;
        }

        const invEncEl = $('invert-encoder-check');
        if (data.config.invert_encoder !== undefined && invEncEl && (!configVisible || document.activeElement !== invEncEl)) {
            invEncEl.checked = data.config.invert_encoder;
        }

        // HX711 Filter & Stability Sync
        const hxFilterEl = $('hx-filter-check');
        const hxFilterCfgEl = $('config-hx-filter-check');
        if (data.config.hx_filter !== undefined) {
            if (hxFilterEl) hxFilterEl.checked = data.config.hx_filter;
            if (hxFilterCfgEl) hxFilterCfgEl.checked = data.config.hx_filter;
        }

        const hxStabEl = $('hx-stability-check');
        const hxStabCfgEl = $('config-hx-stability-check');
        if (data.config.hx_high_stab !== undefined) {
            if (hxStabEl) hxStabEl.checked = data.config.hx_high_stab;
            if (hxStabCfgEl) hxStabCfgEl.checked = data.config.hx_high_stab;
        }

        if (data.config.tube_length !== undefined) {
            if ($('tube_length') && document.activeElement !== $('tube_length')) {
                $('tube_length').value = data.config.tube_length;
            }
            if ($('auto-stop-dist') && document.activeElement !== $('auto-stop-dist')) {
                $('auto-stop-dist').value = data.config.tube_length;
            }
        }
    }
}

function updateSensorBadges(sensors) {
    $('badge-tof').className = 'badge ' + (sensors.tof ? 'online' : 'offline');
    $('badge-enc').className = 'badge ' + (sensors.encoder ? 'online' : 'offline');
    $('badge-hx').className = 'badge ' + (sensors.loadcell ? 'online' : 'offline');
}

// ─── Canvas Chart (Zero Dependencies) ───
function pushChartPoint(data) {
    const val = data[selectedVariable];
    if (typeof val !== 'number') return;
    chartData.push({ t: data.t || 0, v: val });
    if (chartData.length > MAX_CHART_POINTS) chartData.shift();
}

function drawChart() {
    const canvas = $('realtimeChart');
    if (!canvas) return;
    const ctx = canvas.getContext('2d', { alpha: false }); // Optimización
    const w = canvas.parentElement.clientWidth;
    const h = canvas.parentElement.clientHeight;
    
    if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
    }

    const pts = chartData;
    if (pts.length < 2) {
        ctx.fillStyle = '#0a0e1a';
        ctx.fillRect(0, 0, w, h);
        ctx.fillStyle = '#4a5568';
        ctx.font = '13px system-ui';
        ctx.textAlign = 'center';
        ctx.fillText('Esperando datos...', w / 2, h / 2);
        return;
    }

    // Find value range
    let minV = Infinity, maxV = -Infinity;
    for (let i = 0; i < pts.length; i++) {
        const v = pts[i].v;
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }
    const range = maxV - minV || 1;
    const padY = range * 0.15;
    minV -= padY;
    maxV += padY;

    const padL = 50, padR = 10, padT = 10, padB = 28;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    ctx.fillStyle = '#0a0e1a';
    ctx.fillRect(0, 0, w, h);

    // Grid lines
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 1;
    ctx.fillStyle = '#4a5568';
    ctx.font = '10px system-ui';
    ctx.textAlign = 'right';
    const numGridLines = 5;
    for (let i = 0; i <= numGridLines; i++) {
        const y = padT + (plotH / numGridLines) * i;
        ctx.beginPath();
        ctx.moveTo(padL, y);
        ctx.lineTo(w - padR, y);
        ctx.stroke();
        const labelVal = maxV - ((maxV - minV) / numGridLines) * i;
        ctx.fillText(labelVal.toFixed(1), padL - 6, y + 3);
    }

    // Line color
    const tabConf = TAB_CONFIG[currentTab];
    const varConf = tabConf.variables.find(v => v.key === selectedVariable);
    const lineColor = varConf ? varConf.color : '#00d4ff';

    // Line
    ctx.beginPath();
    ctx.strokeStyle = lineColor;
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    for (let i = 0; i < pts.length; i++) {
        const x = padL + (i / (pts.length - 1)) * plotW;
        const y = padT + plotH - ((pts[i].v - minV) / (maxV - minV)) * plotH;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();

    // Gradient fill
    const r = parseInt(lineColor.slice(1, 3), 16) || 0;
    const g = parseInt(lineColor.slice(3, 5), 16) || 0;
    const b = parseInt(lineColor.slice(5, 7), 16) || 0;
    const gradFill = ctx.createLinearGradient(0, padT, 0, padT + plotH);
    gradFill.addColorStop(0, `rgba(${r},${g},${b},0.2)`);
    gradFill.addColorStop(1, `rgba(${r},${g},${b},0.0)`);
    ctx.lineTo(padL + plotW, padT + plotH);
    ctx.lineTo(padL, padT + plotH);
    ctx.closePath();
    ctx.fillStyle = gradFill;
    ctx.fill();

    // Current value dot
    if (pts.length > 0) {
        const last = pts[pts.length - 1];
        const lx = padL + plotW;
        const ly = padT + plotH - ((last.v - minV) / (maxV - minV)) * plotH;
        ctx.beginPath();
        ctx.arc(lx, ly, 4, 0, Math.PI * 2);
        ctx.fillStyle = lineColor;
        ctx.fill();
    }

    // X Axis label
    ctx.fillStyle = '#4a5568';
    ctx.font = '10px system-ui';
    ctx.textAlign = 'center';
    ctx.fillText('Tiempo (' + timeUnitLabel() + ') →', w / 2, h - 4);
}

// (Dead switchTab removed — active version is below at line ~1140)

function updateSensorCount() {
    const tabConf = TAB_CONFIG[currentTab];
    const el = $('sensor-count');
    if (!el || !tabConf || !tabConf.sensor) return;
    const n = sensorData[tabConf.sensor].length;
    el.textContent = n + ' muestras';
}

// ─── Per-sensor Recording ───
function toggleSensorRecording() {
    const tabConf = TAB_CONFIG[currentTab];
    if (!tabConf || !tabConf.sensor) return toggleRecording();
    const sensor = tabConf.sensor;
    sensorRecording[sensor] = !sensorRecording[sensor];
    const btn = $('btn-sensor-rec');
    if (sensorRecording[sensor]) {
        sensorData[sensor] = [];
        chartData = []; 
        currentChartStartTime = null; 
        btn.classList.add('recording');
        btn.innerHTML = '<span class="dot"></span> ⏹ Detener';
        setStatus('Midiendo...', '#ff4d6a'); // Rojo para medir
        if (ws && ws.readyState === WebSocket.OPEN) ws.send('START_' + sensor);
    } else {
        btn.classList.remove('recording');
        btn.innerHTML = '<span class="dot"></span> ▶️ Medir';
        setStatus('Conectado', '#14f0c5');
        if (ws && ws.readyState === WebSocket.OPEN) ws.send('STOP_' + sensor);
        
        // Auto-save to IndexedDB for offline sync (H5)
        if (sensorData[sensor].length > 0) {
            const autoExportData = prepareExportData(sensor);
            if (typeof saveToIndexedDB === 'function') {
                saveToIndexedDB(autoExportData).then(() => {
                    console.log('[Auto-Save] Experiment cached in IndexedDB');
                }).catch(err => console.error('[Auto-Save] Error:', err));
            }
        }
    }
}

// Cumulative angle state
let prevAngleRaw = null;
let cumulativeAngle = 0;

function resetAngleState() {
    prevAngleRaw = null;
    cumulativeAngle = 0;
}

function unwrapAngle(raw) {
    if (raw === undefined || raw === null) return 0;
    if (prevAngleRaw === null) {
        prevAngleRaw = raw;
        cumulativeAngle = raw;
        return cumulativeAngle;
    }
    let diff = raw - prevAngleRaw;
    // Handle wrap around for 0-360 sensor
    if (diff > 180) diff -= 360;
    else if (diff < -180) diff += 360;
    
    cumulativeAngle += diff;
    prevAngleRaw = raw;
    return cumulativeAngle;
}

// Helper to prepare data for export/sync
function prepareExportData(sensor) {
    const tabConf = Object.values(TAB_CONFIG).find(t => t.sensor === sensor);
    const data = sensorData[sensor];
    return {
        device: 'Physys-Lab', 
        version: 'v9.0', 
        sensor: sensor,
        tab: tabConf ? tabConf.title : 'Desconocido',
        exported: new Date().toISOString(),
        timeUnit: timeUnit,
        duration_ms: data.length > 1 ? data[data.length - 1].t - data[0].t : 0,
        samples: data.length,
        data: data.map(d => {
            const row = { t: convertTime(d.t) };
            if (tabConf) {
                tabConf.variables.forEach(v => { 
                    // Export specific keys
                    if (d[v.key] !== undefined) {
                        row[v.key] = d[v.key];
                    } else if (sensor === 'ENC' && v.key === 'angleDeg') {
                        row[v.key] = d.angleDeg || 0;
                    }
                });
            }
            return row;
        })
    };
}

// ─── Global Recording (header) ───
function toggleRecording() {
    isRecording = !isRecording;
    const btn = $('btn-record');
    if (isRecording) {
        recordedData = [];
        chartData = [];
        currentChartStartTime = null;
        btn.classList.add('recording');
        btn.innerHTML = '<span class="dot"></span> Grabando...';
        setStatus('Grabando...', '#ff4d6a');
        if (ws && ws.readyState === WebSocket.OPEN) ws.send('START');
    } else {
        btn.classList.remove('recording');
        btn.innerHTML = '<span class="dot"></span> Grabar';
        setStatus('Conectado', '#14f0c5');
        if (ws && ws.readyState === WebSocket.OPEN) ws.send('STOP');

        // Auto-save global recording to IndexedDB (H5)
        if (recordedData.length > 0) {
            const autoExportData = {
                device: 'Physys-Lab',
                version: 'v9.0',
                sensor: 'GLOBAL',
                tab: 'Global Recording',
                exported: new Date().toISOString(),
                timeUnit: timeUnit,
                duration_ms: recordedData.length > 1 ? recordedData[recordedData.length - 1].t - recordedData[0].t : 0,
                samples: recordedData.length,
                data: recordedData.map(d => ({ ...d, t: convertTime(d.t) }))
            };
            if (typeof saveToIndexedDB === 'function') {
                saveToIndexedDB(autoExportData).catch(err => console.error('[Auto-Save Global] Error:', err));
            }
        }
    }
}

// ─── Tare (per tab) ───
function tareSensor() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('TARE');
        const btn = $('btn-sensor-tare');
        if (btn) { btn.textContent = '✓ Tarado'; setTimeout(() => { btn.textContent = '⚖️ Tara'; }, 2000); }
    }
}

// ─── Digital Filter & Stability (Load Cell) ───
function toggleHxFilter() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('TOGGLE_HX_FILTER');
    }
}

function toggleHxStability() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('TOGGLE_HX_STABILITY');
    }
}

function toggleTriggerMode() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const stopDist = parseInt($('auto-stop-dist').value) || 0;
        ws.send('SET_TUBE:' + stopDist);
        ws.send('START_TRIGGER');
    }
}

function stopTriggerMode() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('STOP');
    }
    $('btn-trigger').style.display = '';
    $('btn-trigger-stop').style.display = 'none';
    $('btn-trigger').innerHTML = '🎯 Toma Auto';
    $('btn-trigger').classList.remove('recording');
    sensorRecording.TOF = false;
}

function exportToUsb() {
    if (confirm('¿Deseas exportar los datos al USB?\n\n1. El ESP32 se reiniciará.\n2. El LED parpadeará AZUL.\n3. Conecta el pendrive en ese momento.\n4. Cuando el LED parpadee VERDE, ya puedes retirarlo.')) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('USB_EXPORT');
            alert('Reiniciando... El sistema entrará en modo copia en 3 segundos.');
        } else {
            alert('Error: No hay conexión con el dispositivo.');
        }
    }
}

function rebootESP() {
    if (confirm('¿Deseas reiniciar el dispositivo?')) {
        if (ws && ws.readyState === WebSocket.OPEN) ws.send('REBOOT');
    }
}

function toggleInvertEncoder() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('INVERT_ENC');
        // El ESP32 enviará el estado actualizado en el siguiente mensaje de config
    }
}

// ─── Export per sensor (CSV) ───
function exportSensorCSV() {
    const tabConf = TAB_CONFIG[currentTab];
    if (!tabConf || !tabConf.sensor) return exportJSON();
    const sensor = tabConf.sensor;
    const data = sensorData[sensor];
    if (!data || data.length === 0) { alert('No hay datos grabados para ' + tabConf.title); return; }
    
    // Header
    const exportData = prepareExportData(sensor);
    
    // Generar CSV string
    let csv = 't(' + timeUnitLabel() + ')';
    tabConf.variables.forEach(v => {
        // Sanitizar unidades (evitar símbolos especiales en CSV para mejor compatibilidad móvil)
        let unit = v.unit.replace('°', 'deg');
        csv += ',' + v.key + '(' + unit + ')';
    });
    csv += '\n';
    
    // Rows
    exportData.data.forEach(d => {
        csv += d.t.toFixed(3);
        tabConf.variables.forEach(v => {
            let val = d[v.key];
            if (typeof val !== 'number') val = 0;
            csv += ',' + val.toFixed(4);
        });
        csv += '\r\n'; // Usar CRLF para mejor compatibilidad con Excel y Móviles
    });

    const filename = 'physys_' + sensor + '_' + Date.now() + '.csv';
    
    // Descargar/Compartir en el celular
    downloadCSV(csv, filename);
    
    const btn = $('btn-sensor-export');
    const oldText = btn.innerHTML;
    btn.innerHTML = '✅ Exportando...';
    setTimeout(() => btn.innerHTML = oldText, 2500);

    // Backup en ESP32
    fetch('/api/data', { 
        method: 'POST', 
        headers: {'Content-Type':'application/json'}, 
        body: JSON.stringify(exportData) 
    }).catch(() => {});
    
    if (typeof saveToIndexedDB === 'function') saveToIndexedDB(exportData).catch(() => {});

    // Mobile feedback and easy copy for WhatsApp
    if (/Android|iPhone|iPad/i.test(navigator.userAgent)) {
        setTimeout(() => {
            const copyBtn = confirm('¿Deseas COPIAR los datos al portapapeles para pegarlos directamente en WhatsApp?');
            if (copyBtn) {
                // Formato simplificado para WhatsApp (pocas líneas o resumen)
                const header = 't(' + timeUnitLabel() + '),' + tabConf.variables.map(v => v.label).join(',') + '\n';
                const rows = exportData.data.slice(0, 100).map(d => {
                    let r = [d.t.toFixed(2)];
                    tabConf.variables.forEach(v => r.push((d[v.key] || 0).toFixed(2)));
                    return r.join(',');
                }).join('\n');
                const footer = data.length > 100 ? '\n... (truncado a 100 muestras)' : '';
                
                navigator.clipboard.writeText(header + rows + footer).then(() => {
                    alert('¡Copiado! Ya puedes pegarlo en el chat.');
                }).catch(() => {
                    // Fallback
                    const area = document.createElement('textarea');
                    area.value = header + rows + footer;
                    document.body.appendChild(area);
                    area.select();
                    document.execCommand('copy');
                    document.body.removeChild(area);
                    alert('¡Copiado (método alternativo)!');
                });
            }
        }, 1500);
    }
}

// ─── Export All (header button - JSON) ───
function exportJSON() {
    const tabConf = TAB_CONFIG[currentTab];
    if (!tabConf || !tabConf.sensor) {
        alert('No hay un sensor activo para exportar.');
        return;
    }
    const sensor = tabConf.sensor;
    const data = sensorData[sensor];
    if (!data || data.length === 0) { 
        alert('No hay datos grabados para ' + tabConf.title); 
        return; 
    }
    
    const exportData = prepareExportData(sensor);
    
    downloadJSON(exportData, 'physys_' + sensor + '_' + Date.now() + '.json');
    
    const btn = $('btn-export');
    const oldText = btn.innerHTML;
    btn.innerHTML = '✅ JSON Guardado';
    setTimeout(() => btn.innerHTML = oldText, 2500);

    fetch('/api/data', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(exportData) })
        .then(r=>r.json()).catch(()=>{});
        
    if (typeof saveToIndexedDB === 'function') saveToIndexedDB(exportData).catch(()=>{});
}

function downloadJSON(data, filename) {
    const jsonStr = JSON.stringify(data, null, 2);
    
    // Intento de usar Web Share API (optimizado para celulares)
    if (navigator.share && navigator.canShare) {
        // En móviles es más seguro compartir como .txt si .json es bloqueado
        const safeFilename = filename.replace('.json', '.txt');
        const file = new File([jsonStr], safeFilename, { type: 'text/plain' });
        
        if (navigator.canShare({ files: [file] })) {
            navigator.share({
                title: 'Datos Physys Lab',
                text: 'Aquí están los datos del experimento.',
                files: [file]
            }).then(() => {
                console.log('Archivo compartido con éxito');
            }).catch(e => {
                console.log('Error compartiendo o cancelado:', e);
                showExportModal(jsonStr, filename);
            });
            return; // Exit here if share was triggered
        }
    }
    
    // Fallback para PC o si Share API no está disponible
    showExportModal(jsonStr, filename);
}

function downloadCSV(csvStr, filename) {
    if (navigator.share && navigator.canShare) {
        const file = new File([csvStr], filename, { type: 'text/csv' });
        if (navigator.canShare({ files: [file] })) {
            navigator.share({
                title: 'Datos Physys Lab CSV',
                text: 'Aquí están los datos del experimento en formato CSV.',
                files: [file]
            }).then(() => {
                console.log('Archivo compartido con éxito');
            }).catch(e => {
                console.log('Error compartiendo o cancelado:', e);
                showExportModal(csvStr, filename);
            });
            return;
        }
    }
    showExportModal(csvStr, filename);
}

function showExportModal(contentStr, filename) {
    const isCSV = filename.endsWith('.csv');
    const overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.style.position = 'fixed';
    overlay.style.top = '0'; overlay.style.left = '0';
    overlay.style.width = '100%'; overlay.style.height = '100%';
    overlay.style.backgroundColor = 'rgba(0,0,0,0.85)';
    overlay.style.display = 'flex';
    overlay.style.justifyContent = 'center';
    overlay.style.alignItems = 'center';
    overlay.style.zIndex = '9999';
    overlay.style.backdropFilter = 'blur(4px)';

    const modal = document.createElement('div');
    modal.className = 'glass-card';
    modal.style.background = '#1e293b';
    modal.style.padding = '24px';
    modal.style.borderRadius = '16px';
    modal.style.width = '90%';
    modal.style.maxWidth = '450px';
    modal.style.color = '#fff';
    modal.style.boxShadow = '0 20px 50px rgba(0,0,0,0.6)';
    modal.style.border = '1px solid rgba(255,255,255,0.1)';

    const title = document.createElement('h3');
    title.textContent = isCSV ? '📥 Exportar CSV' : '📤 Exportar JSON';
    title.style.margin = '0 0 10px 0';
    title.style.color = isCSV ? '#14f0c5' : '#f0b429';

    const desc = document.createElement('p');
    desc.textContent = 'Archivo: ' + filename;
    desc.style.fontSize = '13px';
    desc.style.color = '#94a3b8';
    desc.style.marginBottom = '20px';

    const area = document.createElement('textarea');
    area.value = contentStr;
    area.readOnly = true;
    area.style.width = '100%';
    area.style.height = '120px';
    area.style.background = '#0f172a';
    area.style.color = '#94a3b8';
    area.style.border = '1px solid #334155';
    area.style.borderRadius = '8px';
    area.style.padding = '10px';
    area.style.fontSize = '11px';
    area.style.fontFamily = 'monospace';
    area.style.marginBottom = '15px';
    area.style.resize = 'none';

    // Botón Compartir (Solo si está disponible)
    if (navigator.share) {
        const btnShare = document.createElement('button');
        btnShare.innerHTML = '<span>🔗</span> Compartir Archivo';
        btnShare.className = 'btn-action btn-teal';
        btnShare.style.width = '100%';
        btnShare.style.marginBottom = '12px';
        btnShare.style.padding = '14px';
        btnShare.style.backgroundColor = '#0ea5e9';
        btnShare.onclick = () => {
            const file = new File([contentStr], filename, { type: isCSV ? 'text/csv' : 'application/json' });
            navigator.share({
                title: 'Physys Lab Export',
                files: [file]
            }).catch(e => console.warn('Share failed', e));
        };
        modal.appendChild(btnShare);
    }

    const btnDescargar = document.createElement('button');
    btnDescargar.innerHTML = '<span>⬇️</span> Descargar Archivo';
    btnDescargar.className = 'btn-action btn-gold';
    btnDescargar.style.width = '100%';
    btnDescargar.style.marginBottom = '12px';
    btnDescargar.style.padding = '14px';
    btnDescargar.onclick = () => { triggerDownloadLink(contentStr, filename); };

    const btnCopiar = document.createElement('button');
    btnCopiar.textContent = '📋 Copiar al Portapapeles';
    btnCopiar.className = 'btn-action btn-teal';
    btnCopiar.style.width = '100%';
    btnCopiar.style.padding = '14px';
    btnCopiar.style.marginBottom = '12px';
    btnCopiar.onclick = () => {
        area.select();
        area.setSelectionRange(0, 99999);
        try {
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(contentStr).then(() => {
                    btnCopiar.textContent = '✅ ¡Copiado!';
                    setTimeout(() => btnCopiar.textContent = '📋 Copiar al Portapapeles', 2000);
                });
            } else {
                document.execCommand('copy');
                btnCopiar.textContent = '✅ ¡Copiado!';
                setTimeout(() => btnCopiar.textContent = '📋 Copiar al Portapapeles', 2000);
            }
        } catch (err) {
            alert('Por favor, selecciona el texto de la caja y cópialo manualmente.');
        }
    };

    // ─── Separador Desmos ───
    const desmosSep = document.createElement('div');
    desmosSep.style.cssText = 'text-align:center;color:#64748b;font-size:11px;margin:14px 0 8px;border-top:1px solid #334155;padding-top:10px;letter-spacing:0.5px;text-transform:uppercase';
    desmosSep.textContent = '📊 Desmos & Hojas de Cálculo';

    // ─── Botón A: Copiar para Desmos (TSV) ───
    const btnDesmosTSV = document.createElement('button');
    btnDesmosTSV.innerHTML = '<span>📊</span> Copiar para Desmos (TSV)';
    btnDesmosTSV.className = 'btn-action';
    btnDesmosTSV.style.cssText = 'width:100%;margin-bottom:12px;padding:14px;background:linear-gradient(135deg,#059669,#10b981);color:#fff;border:none;border-radius:8px;cursor:pointer;font-weight:600;font-size:14px';
    btnDesmosTSV.onclick = () => {
        const tabConf = TAB_CONFIG[currentTab];
        const sensor = tabConf ? tabConf.sensor : null;
        const data = sensor ? sensorData[sensor] : null;
        if (!data || data.length === 0) { alert('No hay datos para copiar.'); return; }
        const vars = tabConf.variables;
        const tsvHeader = 't(' + timeUnitLabel() + ')\t' + vars.map(v => v.label).join('\t');
        const tsvRows = data.slice(0, 1000).map(d => {
            let cols = [convertTime(d.t).toFixed(3)];
            vars.forEach(v => cols.push((d[v.key] !== undefined ? d[v.key] : 0).toFixed(4)));
            return cols.join('\t');
        }).join('\n');
        const tsv = tsvHeader + '\n' + tsvRows;
        const doCopy = (text) => {
            if (navigator.clipboard && navigator.clipboard.writeText) {
                return navigator.clipboard.writeText(text);
            }
            const ta = document.createElement('textarea');
            ta.value = text; document.body.appendChild(ta); ta.select();
            document.execCommand('copy'); document.body.removeChild(ta);
            return Promise.resolve();
        };
        doCopy(tsv).then(() => {
            const truncMsg = data.length > 1000 ? ' (1000/' + data.length + ' filas)' : '';
            btnDesmosTSV.innerHTML = '✅ ¡TSV Copiado!' + truncMsg;
            setTimeout(() => btnDesmosTSV.innerHTML = '<span>📊</span> Copiar para Desmos (TSV)', 3000);
        }).catch(() => alert('Error al copiar. Intenta desde el textarea.'));
    };

    // ─── Botón B: Abrir en Desmos ───
    const btnOpenDesmos = document.createElement('button');
    btnOpenDesmos.innerHTML = '<span>🔗</span> Abrir en Desmos';
    btnOpenDesmos.className = 'btn-action';
    btnOpenDesmos.style.cssText = 'width:100%;margin-bottom:12px;padding:14px;background:linear-gradient(135deg,#2563eb,#3b82f6);color:#fff;border:none;border-radius:8px;cursor:pointer;font-weight:600;font-size:14px';
    btnOpenDesmos.onclick = () => {
        const tabConf = TAB_CONFIG[currentTab];
        const sensor = tabConf ? tabConf.sensor : null;
        const data = sensor ? sensorData[sensor] : null;
        if (!data || data.length === 0) { alert('No hay datos para enviar a Desmos.'); return; }
        const vars = tabConf.variables;
        const tsvHeader = 't(' + timeUnitLabel() + ')\t' + vars.map(v => v.label).join('\t');
        const tsvRows = data.slice(0, 1000).map(d => {
            let cols = [convertTime(d.t).toFixed(3)];
            vars.forEach(v => cols.push((d[v.key] !== undefined ? d[v.key] : 0).toFixed(4)));
            return cols.join('\t');
        }).join('\n');
        const tsv = tsvHeader + '\n' + tsvRows;
        const doCopy = (text) => {
            if (navigator.clipboard && navigator.clipboard.writeText) {
                return navigator.clipboard.writeText(text);
            }
            const ta = document.createElement('textarea');
            ta.value = text; document.body.appendChild(ta); ta.select();
            document.execCommand('copy'); document.body.removeChild(ta);
            return Promise.resolve();
        };
        doCopy(tsv).then(() => {
            window.open('https://www.desmos.com/calculator', '_blank');
            btnOpenDesmos.innerHTML = '✅ Desmos abierto — Pega (Ctrl+V) en línea vacía';
            setTimeout(() => btnOpenDesmos.innerHTML = '<span>🔗</span> Abrir en Desmos', 4000);
        }).catch(() => {
            window.open('https://www.desmos.com/calculator', '_blank');
            alert('Copia los datos del textarea y pégalos en Desmos.');
        });
    };

    const btnCerrar = document.createElement('button');
    btnCerrar.textContent = 'Cerrar';
    btnCerrar.className = 'btn-action btn-warn';
    btnCerrar.style.width = '100%';
    btnCerrar.style.padding = '10px';
    btnCerrar.style.marginTop = '10px';
    btnCerrar.style.background = 'transparent';
    btnCerrar.style.border = '1px solid #ef4444';
    btnCerrar.onclick = () => document.body.removeChild(overlay);

    modal.appendChild(title);
    modal.appendChild(desc);
    modal.appendChild(area);
    modal.appendChild(btnCopiar);
    modal.appendChild(btnDescargar);
    modal.appendChild(desmosSep);
    modal.appendChild(btnDesmosTSV);
    modal.appendChild(btnOpenDesmos);
    modal.appendChild(btnCerrar);
    overlay.appendChild(modal);
    document.body.appendChild(overlay);
}

function triggerDownloadLink(content, filename) {
    const isCSV = filename.endsWith('.csv');
    const mime = isCSV ? 'text/csv' : 'application/json';
    const blob = new Blob([content], { type: mime });
    const url = URL.createObjectURL(blob);
    
    const a = document.createElement('a'); 
    a.href = url; 
    a.download = filename;
    a.style.display = 'none';
    document.body.appendChild(a);
    
    // Pequeño truco para algunos navegadores móviles
    setTimeout(() => {
        a.click();
        console.log('Download triggered for:', filename);
        // Dejamos el objeto vivo por 30 segundos para dar tiempo al sistema de descarga
        setTimeout(() => {
            if (document.body.contains(a)) document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }, 30000); 
    }, 100);
}

// ─── Save Config ───
function saveSampleRate() {
    const rate = $('sample_rate').value;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('SET_RATE:' + rate);
        const btn = $('btn-save-rate');
        btn.textContent = '✓ Guardado';
        alert('Frecuencia de muestreo actualizada a ' + (1000/rate).toFixed(0) + ' Hz. El cambio se aplicará de inmediato y persistirá tras el reinicio.');
        setTimeout(() => btn.textContent = '💾 Aplicar Frecuencia', 2000);
    }
}

// ─── Clear Local (Browser) data ───
function clearLocalData() {
    const tabConf = TAB_CONFIG[currentTab];
    if (!tabConf || !tabConf.sensor) return;
    const sensor = tabConf.sensor;
    if (!confirm('¿Limpiar gráfica y borrar ' + (sensorData[sensor] ? sensorData[sensor].length : 0) + ' muestras de pantalla?')) return;
    sensorData[sensor] = [];
    chartData = [];
    updateSensorCount();
    drawChart();
}

// ─── Clear ESP32 memory (Internal Flash) ───
function clearEspFiles() {
    if (!confirm('¿Borrar permanentemente TODOS los archivos de experimentos guardados en la memoria interna del ESP32?')) return;
    fetch('/api/data/clear', { method: 'DELETE' })
        .then(r => r.json())
        .then(d => { 
            alert('✓ ' + d.deleted + ' archivos eliminados.\nMemoria libre: ' + (d.free/1024).toFixed(0) + ' KB'); 
            fetchSystemInfo(); 
        })
        .catch(e => alert('Error al borrar archivos: ' + e.message));
}

// ─── Low Power Mode ───
function toggleLowPower() {
    lowPowerMode = !lowPowerMode;
    const btn = $('btn-save');
    if (lowPowerMode) {
        btn.style.background = 'rgba(20, 240, 197, 0.3)';
        btn.title = 'Modo Ahorro ACTIVO';
    } else {
        btn.style.background = '';
        btn.title = 'Interruptor Modo Ahorro';
    }
}

// ─── Fetch System Info ───
function fetchSystemInfo() {
    fetch('/api/status')
        .then(r => r.json())
        .then(data => {
            $('heap-info').textContent = (data.heap / 1024).toFixed(0) + ' KB';
            $('psram-info').textContent = (data.psram / 1024).toFixed(0) + ' KB';
            $('uptime-info').textContent = data.uptime;
            
            // Sincronizar selectores de configuración si vienen en el status
            if (data.config) {
                if (data.config.tof_model) $('tof_model').value = data.config.tof_model;
                if (data.config.sample_rate) $('sample_rate').value = data.config.sample_rate;
                if (data.config.usb_log !== undefined && $('usb-auto-log')) $('usb-auto-log').checked = data.config.usb_log;
            }
            if (data.sensors) {
                updateSensorBadges(data.sensors);
            }
            if (data.hardware) {
                activeHardwareProfile = data.hardware;
                updateHardwareStatusBanner();
                populatePinSelectors();
            }
            if (data.auth) {
                isLeaderActive = data.auth.leader_active;
                leaderIp = data.auth.leader_ip || "";
                updateLeaderStatusBanner();
            }
        })
        .catch(() => {});

    fetch('/api/storage')
        .then(r => r.json())
        .then(data => {
            const pct = data.data ? (data.data.used / data.data.total * 100) : 0;
            $('storage-fill').style.width = pct.toFixed(1) + '%';
            $('storage-text').textContent =
                (data.data.used / 1024).toFixed(0) + ' KB / ' +
                (data.data.total / 1024).toFixed(0) + ' KB (' + pct.toFixed(1) + '%)';
        })
        .catch(() => {});
}

// ─── Python Examples ───
const PYTHON_EXAMPLES = {
    tof: `# ═════════════════════════════════════════════
# Ejemplo 1: Cinemática Lineal con VL53L0X (ToF)
# ═════════════════════════════════════════════
# Sensor: VL53L0X (Time-of-Flight) en I2C 0x29
# Variables: dist (mm), vel (m/s), acc (m/s²)

# Configuración del experimento
nombre = "Caída Libre"
sensor = "VL53L0X"
frecuencia_hz = 20
duracion_s = 10

# Rango válido de mediciones
dist_min = 30    # mm (mínimo del sensor)
dist_max = 2000  # mm (máximo práctico)

# Cálculos derivados
# La velocidad y aceleración se calculan automáticamente
# a partir de las lecturas de distancia (derivada numérica).
#
# Resultados esperados para caída libre:
#   acc ≈ -9.81 m/s² (gravedad)
#   vel = vel_0 + acc * t
#   dist = dist_0 + vel_0 * t + 0.5 * acc * t²

# Filtro de datos
filtrar_outliers = True
ventana_promedio = 3  # Promedio móvil de N muestras
`,
    encoder: `# ═════════════════════════════════════════════
# Ejemplo 2: Cinemática Angular con AS5600
# ═════════════════════════════════════════════
# Sensor: AS5600 (Encoder Magnético) en I2C 0x36
# Variables: angleDeg (°), angleRad (rad),
#            angVel (rad/s), angAcc (rad/s²)

# Configuración del experimento
nombre = "Péndulo Simple"
sensor = "AS5600"
frecuencia_hz = 20
duracion_s = 30

# Parámetros del péndulo
longitud_m = 0.5       # Longitud del hilo (metros)
masa_kg = 0.1          # Masa de la pesa (kg)
angulo_inicial_deg = 15 # Ángulo inicial (grados)

# Período teórico (pequeñas oscilaciones)
import math
g = 9.81
T_teorico = 2 * math.pi * math.sqrt(longitud_m / g)
# T ≈ 1.42 s para L = 0.5 m

# Frecuencia angular natural
omega_n = math.sqrt(g / longitud_m)
# ω ≈ 4.43 rad/s

# Análisis esperado:
#   θ(t) = θ_0 * cos(ω_n * t)
#   ω(t) = -θ_0 * ω_n * sin(ω_n * t)
#   α(t) = -θ_0 * ω_n² * cos(ω_n * t)
`,
    loadcell: `# ═════════════════════════════════════════════
# Ejemplo 3: Dinámica con HX711 (Celda de Carga)
# ═════════════════════════════════════════════
# Sensor: HX711 en GPIO6 (DT) + GPIO7 (SCK)
# Variables: weight (g), mass (kg), weightN (N)

# Configuración del experimento
nombre = "Ley de Hooke"
sensor = "HX711"
frecuencia_hz = 10
duracion_s = 60

# Parámetros del resorte
k_resorte = 25.0    # Constante elástica (N/m)
x_natural = 0.15    # Longitud natural (metros)

# Procedimiento:
# 1. Tarar la celda (enviar comando TARE desde la web)
# 2. Colgar masas incrementales: 50g, 100g, 150g, 200g
# 3. Registrar peso (N) y elongación (m) para cada masa
#
# Ley de Hooke: F = k * Δx
# Donde:
#   F = peso medido (weightN) en Newtons
#   k = constante del resorte (N/m)
#   Δx = elongación = x_actual - x_natural

# Calibración de la celda
# La escala (set_scale) se ajusta experimentalmente.
# Valor actual: 420.0 (modificar en main.cpp si es necesario)
#
# Para calibrar:
# 1. Colocar masa conocida (ej: 100g)
# 2. Leer valor crudo
# 3. Dividir: escala = valor_crudo / masa_conocida
`
};

// ─── GPIO Viewer ───
let gvInterval = null;
let gvActive = false;

// ESP32-S3 WROOM-1 physical top-to-bottom layout mapping
const leftOrder = [3, 46, 9, 10, 11, 12, 13, 14, 21, 47, 48, 45, 0, 35, 36, 37, 38, 39, 40, 41, 42, 2];
const rightOrder = [4, 5, 6, 7, 15, 16, 17, 18, 8, 19, 20, 26, 43, 44, 1, 33, 34];

function renderGpioPins(pins) {
    const leftCol = $('gv-left-pins');
    const rightCol = $('gv-right-pins');
    if (!leftCol || !rightCol) return;

    // Filter and sort left pins
    const leftPins = pins.filter(p => leftOrder.includes(p.g));
    leftPins.sort((a, b) => leftOrder.indexOf(a.g) - leftOrder.indexOf(b.g));

    // Filter and sort right pins (anything not in left row goes to right column)
    const rightPins = pins.filter(p => !leftOrder.includes(p.g));
    rightPins.sort((a, b) => {
        const idxA = rightOrder.indexOf(a.g);
        const idxB = rightOrder.indexOf(b.g);
        if (idxA === -1 && idxB === -1) return a.g - b.g;
        if (idxA === -1) return 1;
        if (idxB === -1) return -1;
        return idxA - idxB;
    });

    leftCol.innerHTML = leftPins.map(p =>
        `<div class="gv-pin fn-${p.f}" data-gpio="${p.g}">
            <span class="gv-pin-state ${p.v ? 'high' : 'low'}"></span>
            <span class="gv-pin-num">${p.g}</span>
            <span class="gv-pin-label">${p.l}</span>
        </div>`
    ).join('');

    rightCol.innerHTML = rightPins.map(p =>
        `<div class="gv-pin fn-${p.f}" data-gpio="${p.g}">
            <span class="gv-pin-state ${p.v ? 'high' : 'low'}"></span>
            <span class="gv-pin-num">${p.g}</span>
            <span class="gv-pin-label">${p.l}</span>
        </div>`
    ).join('');
}

function pollGpio() {
    fetch('/api/gpio')
        .then(r => r.json())
        .then(data => {
            renderGpioPins(data.pins);
            $('gv-timestamp').textContent = 'Uptime: ' + (data.t / 1000).toFixed(1) + 's';
        })
        .catch(() => {
            $('gv-timestamp').textContent = 'Sin conexión';
        });
}

function startGpioPolling() {
    if (gvInterval) clearInterval(gvInterval);
    const rate = parseInt($('gv-rate').value) || 1000;
    pollGpio();
    gvInterval = setInterval(pollGpio, rate);
    gvActive = true;
}

function stopGpioPolling() {
    if (gvInterval) { clearInterval(gvInterval); gvInterval = null; }
    gvActive = false;
}

// ─── Tab Switch (Sensor Dropdown + Icon Nav) ───
function switchTab(tabName) {
    try {
        currentTab = tabName;
        chartData = [];

        const pythonPanel = $('python-panel');
        const gpioPanel = $('gpio-viewer-panel');
        const configPanel = $('config-panel');
        const mainLayout = document.querySelector('.main-layout');

        // Reset visibility
        if (mainLayout) mainLayout.style.display = 'none';
        if (pythonPanel) pythonPanel.style.display = 'none';
        if (gpioPanel) gpioPanel.style.display = 'none';
        if (configPanel) configPanel.style.display = 'none';
        
        stopGpioPolling();

        // Sync nav icon active states
        const cfgBtn = $('btn-nav-config');
        const gpioBtn = $('btn-nav-gpio');
        const chartBtn = $('btn-nav-chart');
        if (cfgBtn) cfgBtn.classList.toggle('active', tabName === 'config');
        if (gpioBtn) gpioBtn.classList.toggle('active', tabName === 'gpioview');
        if (chartBtn) chartBtn.classList.toggle('active', ['movimiento', 'rotacion', 'fuerza'].includes(tabName));

        // Sync sensor dropdown
        const sensorSelect = $('sensor-select');
        if (sensorSelect && ['movimiento', 'rotacion', 'fuerza'].includes(tabName)) {
            sensorSelect.value = tabName;
            lastSensorTab = tabName;
        }

        if (tabName === 'python') {
            if (pythonPanel) pythonPanel.style.display = 'block';
            updateLineNumbers();
        } else if (tabName === 'gpioview') {
            if (gpioPanel) gpioPanel.style.display = 'block';
            startGpioPolling();
        } else if (tabName === 'config') {
            if (configPanel) configPanel.style.display = 'block';
            fetchConfig();
        } else {
            // Tab de sensor normal
            if (mainLayout) mainLayout.style.display = '';
            
            const tabConf = TAB_CONFIG[tabName];
            if (!tabConf) return;

            $('display-title').textContent = tabConf.title;

            // Populate variable selector
            const sel = $('graph-variable');
            sel.innerHTML = '';
            tabConf.variables.forEach(v => {
                const opt = document.createElement('option');
                opt.value = v.key;
                opt.textContent = v.label + ' (' + v.unit + ')';
                sel.appendChild(opt);
            });
            selectedVariable = tabConf.variables[0].key;

            // Sync sensor toolbar with current tab
            const sensor = tabConf.sensor;
            const btn = $('btn-sensor-rec');
            if (btn) {
                if (sensorRecording[sensor]) {
                    btn.classList.add('recording');
                    btn.innerHTML = '<span class="dot"></span> ⏹ Detener';
                } else {
                    btn.classList.remove('recording');
                    btn.innerHTML = '<span class="dot"></span> ▶️ Medir';
                }
            }
            
            // Per-tab buttons visibility
            const invertBtn = $('btn-sensor-invert');
            const resetEncBtn = $('btn-sensor-reset-enc');
            const tareBtn = $('btn-sensor-tare');
            const hxControls = $('hx-controls-container');

            if (invertBtn) invertBtn.style.display = (tabName === 'rotacion') ? '' : 'none';
            if (resetEncBtn) resetEncBtn.style.display = (tabName === 'rotacion') ? '' : 'none';
            if (tareBtn) tareBtn.style.display = (tabName === 'fuerza') ? '' : 'none';
            if (hxControls) hxControls.style.display = (tabName === 'fuerza') ? 'flex' : 'none';
            
            // Update display with last data if available
            if (Object.keys(lastData).length > 0) {
                updateDisplay(lastData);
            }
            
            // Force chart redraw after panel switch
            setTimeout(drawChart, 50);
        }
        
        console.log('[Tab] switched to:', tabName);
        updateSensorCount();
        
    } catch (err) {
        console.error('[Tab] Switch error:', err);
        const mainLayout = document.querySelector('.main-layout');
        if (mainLayout) mainLayout.style.display = '';
    }
}

// ─── Consolidated Save All Config ───
function saveAllConfig() {
    if (!ws || ws.readyState !== WebSocket.OPEN) { alert('Sin conexión'); return; }
    const model = $('tof_model').value;
    const rate = $('sample_rate').value;
    const tube = $('tube_length') ? $('tube_length').value : '0';
    ws.send('SET_TOF:' + model);
    ws.send('SET_RATE:' + rate);
    ws.send('SET_TUBE:' + tube);
    if ($('auto-stop-dist')) $('auto-stop-dist').value = tube; // Sincronizar visualmente
    const btn = $('btn-save-config');
    if (btn) { btn.textContent = '✓ Guardado'; setTimeout(() => { btn.textContent = '💾 Guardar Configuración'; }, 2000); }
    alert('Configuración guardada (ToF: ' + model + ', Freq: ' + (1000/rate).toFixed(0) + ' Hz, Tubo: ' + tube + ' mm).\n\nSe aplicará tras reinicio si cambió el sensor ToF.');
}

// ─── Export Modal (unified: CSV, JSON, USB, Share) ───
function openExportModal() {
    const tabConf = TAB_CONFIG[currentTab];
    const sensor = tabConf ? tabConf.sensor : null;
    const data = sensor ? sensorData[sensor] : [];
    const count = data.length;

    const overlay = document.createElement('div');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.85);display:flex;justify-content:center;align-items:center;z-index:9999;backdrop-filter:blur(4px)';

    // Popstate Hack to prevent exiting the application on Android Back Button press
    history.pushState({ modal: 'export' }, '');
    
    const handlePopState = (e) => {
        closeOverlay(true); // Close modal cleanly without backing history again
    };
    window.addEventListener('popstate', handlePopState);
    
    function closeOverlay(fromPopState = false) {
        overlay.remove();
        window.removeEventListener('popstate', handlePopState);
        if (!fromPopState && history.state && history.state.modal === 'export') {
            history.back(); // Restore pristine state history
        }
    }

    const modal = document.createElement('div');
    modal.className = 'glass-card';
    modal.style.cssText = 'background:#1e293b;padding:24px;border-radius:16px;width:90%;max-width:400px;color:#fff;box-shadow:0 20px 50px rgba(0,0,0,0.6);border:1px solid rgba(255,255,255,0.1)';

    // Elegant Header with Title and Premium '✕' Close Button
    const headerContainer = document.createElement('div');
    headerContainer.style.cssText = 'display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;';
    
    const title = document.createElement('h3');
    title.textContent = '📤 Exportar Datos';
    title.style.cssText = 'margin:0;color:#f0b429';
    
    const closeX = document.createElement('button');
    closeX.innerHTML = '✕';
    closeX.style.cssText = 'background:transparent;border:none;color:#ef4444;font-size:24px;cursor:pointer;padding:4px 8px;font-weight:bold;line-height:1;transition:transform 0.2s;';
    closeX.onmouseover = () => closeX.style.transform = 'scale(1.2)';
    closeX.onmouseout = () => closeX.style.transform = 'scale(1.0)';
    closeX.onclick = () => closeOverlay();
    
    headerContainer.appendChild(title);
    headerContainer.appendChild(closeX);
    modal.appendChild(headerContainer);

    const info = document.createElement('p');
    info.style.cssText = 'font-size:13px;color:#94a3b8;margin-bottom:18px';
    info.textContent = tabConf ? tabConf.title + ' — ' + count + ' muestras' : 'Sin sensor activo';
    modal.appendChild(info);

    function mkBtn(label, color, fn) {
        const b = document.createElement('button');
        b.innerHTML = label;
        b.className = 'btn-action';
        b.style.cssText = 'width:100%;padding:13px;margin-bottom:10px;background:' + color + ';border-color:' + color + ';color:#fff;font-size:14px;border-radius:10px';
        b.onclick = fn;
        modal.appendChild(b);
    }

    if (count > 0) {
        mkBtn('📊 Exportar CSV', '#14f0c5', () => { closeOverlay(); exportSensorCSV(); });
        mkBtn('📋 Exportar JSON', '#f0b429', () => { closeOverlay(); exportJSON(); });
    }

    if (navigator.share && count > 0) {
        mkBtn('🔗 Compartir', '#0ea5e9', () => {
            const exportData = prepareExportData(sensor);
            const file = new File([JSON.stringify(exportData, null, 2)], 'physys_' + sensor + '.json', { type: 'application/json' });
            if (navigator.canShare && navigator.canShare({ files: [file] })) {
                navigator.share({ title: 'Physys Lab', files: [file] }).catch(() => {});
            }
            closeOverlay();
        });
    }

    mkBtn('💾 Exportar a Pendrive (USB)', '#6366f1', () => { closeOverlay(); exportToUsb(); });

    // ─── Sección Desmos & Hojas de Cálculo ───
    if (count > 0) {
        const desmosSep = document.createElement('div');
        desmosSep.style.cssText = 'text-align:center;color:#64748b;font-size:11px;margin:14px 0 8px;border-top:1px solid #334155;padding-top:10px;letter-spacing:0.5px;text-transform:uppercase';
        desmosSep.textContent = '📊 Desmos & Hojas de Cálculo';
        modal.appendChild(desmosSep);

        // ── Helper: formatear número sin ceros ──
        function fmtNum(val, decimals) {
            if (val === undefined || val === null) return '0';
            return parseFloat(Number(val).toFixed(decimals)).toString();
        }
        function fmtTime(t) { return fmtNum(convertTime(t), timeUnit === 'ms' ? 0 : 2); }
        function fmtVar(val, key) {
            if (key === 'dist' || key === 'angleDeg' || key === 'angleRad' || key === 'mass' || key === 'weight' || key === 'weightN') return fmtNum(val, 1);
            return fmtNum(val, 3);
        }

        // ── Helper: descargar vía servidor ESP32 (funciona en portal cautivo) ──
        function serverDownload(content, fileName, mimeType) {
            return fetch('/api/temp-export', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/octet-stream',
                    'X-Filename': fileName,
                    'X-Mime': mimeType
                },
                body: content
            }).then(r => r.json()).then(j => {
                if (j.ok) {
                    // Descargar de forma segura a través de un iframe invisible para no alterar el historial del navegador
                    let iframe = document.getElementById('download-iframe');
                    if (!iframe) {
                        iframe = document.createElement('iframe');
                        iframe.id = 'download-iframe';
                        iframe.style.display = 'none';
                        document.body.appendChild(iframe);
                    }
                    iframe.src = '/api/temp-export';
                    return true;
                }
                throw new Error('Servidor no pudo guardar');
            });
        }

        // ── Helper: construir CSV limpio ──
        function buildCSV() {
            const vars = tabConf.variables;
            const hdr = 't(' + timeUnitLabel() + '),' + vars.map(v => v.label).join(',');
            const rows = data.slice(0, 1000).map(d => {
                let cols = [fmtTime(d.t)];
                vars.forEach(v => cols.push(fmtVar(d[v.key], v.key)));
                return cols.join(',');
            }).join('\n');
            return hdr + '\n' + rows;
        }

        // ── Helper: generar estado .desmos ──
        function buildDesmosFile() {
            const vars = tabConf.variables;
            const slice = data.slice(0, 500);
            const columns = [{
                values: slice.map(d => fmtTime(d.t)),
                id: 'col_t', latex: 'x_{1}', hidden: false
            }];
            vars.forEach((v, i) => {
                columns.push({
                    values: slice.map(d => fmtVar(d[v.key], v.key)),
                    id: 'col_' + i,
                    latex: 'y_{' + (i + 1) + '}',
                    color: ['#2d70b3', '#c74440', '#388c46', '#6042a6'][i % 4],
                    hidden: false, points: true, lines: true
                });
            });
            const tV = slice.map(d => convertTime(d.t));
            const yV = slice.map(d => d[vars[0].key] || 0);
            const xPad = (Math.max(...tV) - Math.min(...tV)) * 0.1 || 1;
            const yPad = (Math.max(...yV) - Math.min(...yV)) * 0.1 || 1;
            return JSON.stringify({
                version: 11, randomSeed: 'physys',
                graph: {
                    viewport: { xmin: Math.min(...tV) - xPad, xmax: Math.max(...tV) + xPad, ymin: Math.min(...yV) - yPad, ymax: Math.max(...yV) + yPad },
                    xAxisLabel: 't (' + timeUnitLabel() + ')',
                    yAxisLabel: vars[0].label + ' (' + vars[0].unit + ')'
                },
                expressions: { list: [{ type: 'table', columns: columns, id: 'physys_table' }] }
            });
        }

        // ── Helper: generar Visor HTML de Desmos ──
        function buildDesmosHTML() {
            const stateJson = buildDesmosFile();
            return `<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Visor Desmos - Physys Lab</title>
<script src="https://www.desmos.com/api/v1.9/calculator.js?apiKey=dcb31709b452b1cf9dc26972add0fda6"></script>
<style>body,html{margin:0;padding:0;height:100%;overflow:hidden;}</style>
</head>
<body>
<div id="calculator" style="width:100vw;height:100vh;"></div>
<script>
var elt = document.getElementById('calculator');
var calc = Desmos.GraphingCalculator(elt, {
    keypad: true,
    expressions: true,
    settingsMenu: true,
    zoomButtons: true
});
calc.setState(${stateJson});
</script>
</body>
</html>`;
        }

        // ── Helper: mostrar feedback ──
        function showDesmosMsg(html, color) {
            modal.querySelectorAll('.desmos-msg').forEach(el => el.remove());
            const msg = document.createElement('div');
            msg.className = 'desmos-msg';
            msg.style.cssText = 'color:' + color + ';font-size:12px;text-align:center;margin:8px 0;padding:10px;background:rgba(0,0,0,0.3);border-radius:8px;line-height:1.6';
            msg.innerHTML = html;
            const closeBtn = modal.querySelector('[style*="border: 1px solid #ef4444"]');
            modal.insertBefore(msg, closeBtn || modal.lastChild);
        }

        // ━━━ BOTÓN 1: Descargar para Desmos ━━━
        mkBtn('📊 Descargar para Desmos', 'linear-gradient(135deg,#059669,#10b981)', () => {
            const sn = tabConf.sensor || 'datos';
            const fname = 'physys_' + sn + '.desmos';
            showDesmosMsg('⏳ Preparando archivo...', '#94a3b8');
            serverDownload(buildDesmosFile(), fname, 'application/octet-stream')
                .then(() => {
                    showDesmosMsg(
                        '📥 <b>' + fname + '</b> descargado<br>' +
                        '<small style="color:#fbbf24"><b>Pasos:</b><br>' +
                        '1. Desconéctate del WiFi <b>Physys-Lab</b><br>' +
                        '2. Abre <b>desmos.com/calculator</b><br>' +
                        '3. Menú <b>≡</b> → <b>Abrir</b> → busca el archivo</small>',
                        '#6ee7b7'
                    );
                })
                .catch(() => {
                    showDesmosMsg('❌ Error al preparar descarga. Reintenta.', '#ef4444');
                });
        });

        // ━━━ BOTÓN 2: Descargar CSV (Sheets/Excel) ━━━
        mkBtn('📄 Descargar CSV (Sheets/Excel)', 'linear-gradient(135deg,#2563eb,#3b82f6)', () => {
            const sn = tabConf.sensor || 'datos';
            const date = new Date().toISOString().slice(0,10);
            const fname = 'physys_' + sn + '_' + date + '.csv';
            showDesmosMsg('⏳ Preparando CSV...', '#94a3b8');
            serverDownload(buildCSV(), fname, 'text/csv')
                .then(() => {
                    showDesmosMsg(
                        '📥 <b>CSV descargado</b> (' + count + ' filas)<br>' +
                        '<small style="color:#fbbf24"><b>Pasos:</b><br>' +
                        '1. Desconéctate del WiFi <b>Physys-Lab</b><br>' +
                        '2. Abre con <b>Google Sheets</b> o <b>Excel</b></small>',
                        '#93c5fd'
                    );
                })
                .catch(() => {
                    showDesmosMsg('❌ Error al preparar CSV. Reintenta.', '#ef4444');
                });
        });

        // ━━━ BOTÓN 3: Descargar Visor Desmos (HTML) ━━━
        mkBtn('📱 Descargar Visor Interactivo (HTML)', 'linear-gradient(135deg,#8b5cf6,#7c3aed)', () => {
            const sn = tabConf.sensor || 'datos';
            const fname = 'physys_' + sn + '_visor.html';
            showDesmosMsg('⏳ Preparando Visor HTML...', '#94a3b8');
            serverDownload(buildDesmosHTML(), fname, 'text/html')
                .then(() => {
                    showDesmosMsg(
                        '📥 <b>Visor HTML descargado</b><br>' +
                        '<small style="color:#fbbf24"><b>Pasos:</b><br>' +
                        '1. Desconéctate del WiFi <b>Physys-Lab</b><br>' +
                        '2. Toca el archivo <b>' + fname + '</b> descargado para abrir tu gráfica interactiva en el navegador.</small>',
                        '#c4b5fd'
                    );
                })
                .catch(() => {
                    showDesmosMsg('❌ Error al preparar Visor. Reintenta.', '#ef4444');
                });
        });

        // ━━━ Instrucciones visuales ━━━
        const instrDiv = document.createElement('div');
        instrDiv.style.cssText = 'margin:10px 0 0;padding:10px;background:rgba(251,191,36,0.08);border:1px solid rgba(251,191,36,0.25);border-radius:10px;font-size:11px;color:#94a3b8;line-height:1.5;text-align:left';
        instrDiv.innerHTML =
            '<div style="color:#fbbf24;font-weight:700;margin-bottom:4px;text-align:center">⚡ Flujo rápido</div>' +
            '① Toca <b>Descargar</b> arriba<br>' +
            '② <b>Desconéctate</b> del WiFi Physys-Lab<br>' +
            '③ Abre <b>Desmos</b> o <b>Google Sheets</b><br>' +
            '④ <b>Importa</b> el archivo descargado';
        modal.appendChild(instrDiv);
    }

    if (count === 0) {
        const warn = document.createElement('p');
        warn.style.cssText = 'font-size:12px;color:#f0b429;text-align:center;margin-bottom:12px';
        warn.textContent = '⚠️ Graba datos primero con el botón Medir para exportar CSV/JSON.';
        modal.insertBefore(warn, modal.querySelector('.btn-action'));
    }

    const closeBtn = document.createElement('button');
    closeBtn.textContent = 'Cerrar';
    closeBtn.className = 'btn-action';
    closeBtn.style.cssText = 'width:100%;padding:10px;margin-top:4px;background:transparent;border:1px solid #ef4444;color:#ef4444;border-radius:10px';
    closeBtn.onclick = () => closeOverlay();
    modal.appendChild(closeBtn);

    overlay.appendChild(modal);
    overlay.addEventListener('click', (e) => { if (e.target === overlay) closeOverlay(); });
    document.body.appendChild(overlay);
}

// ─── Desmos List Modal (x₁ y y₁ separados para copiar uno a uno) ───
function showDesmosListModal(lists) {
    const overlay = document.createElement('div');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.92);display:flex;justify-content:center;align-items:center;z-index:10000;backdrop-filter:blur(6px)';

    const modal = document.createElement('div');
    modal.style.cssText = 'background:#1e293b;padding:20px;border-radius:16px;width:92%;max-width:500px;max-height:90vh;color:#fff;box-shadow:0 25px 60px rgba(0,0,0,0.7);border:1px solid rgba(255,255,255,0.1);display:flex;flex-direction:column;overflow-y:auto';

    const title = document.createElement('h3');
    title.innerHTML = '📊 Listas para Desmos';
    title.style.cssText = 'margin:0 0 8px 0;color:#10b981;font-size:16px';
    modal.appendChild(title);

    const steps = document.createElement('div');
    steps.style.cssText = 'font-size:12px;color:#94a3b8;margin-bottom:12px;line-height:1.6;padding:8px;background:rgba(0,0,0,0.2);border-radius:8px';
    steps.innerHTML =
        '<b style="color:#f0b429">Pasos en Desmos:</b><br>' +
        '1️⃣ Copia x₁ → pega en <b>línea vacía</b> (no en tabla)<br>' +
        '2️⃣ Vuelve aquí, copia y₁ → pega en la <b>siguiente línea</b><br>' +
        '3️⃣ En una nueva línea escribe: <b style="color:#6ee7b7">(x₁, y₁)</b>';
    modal.appendChild(steps);

    // Función para crear bloque de lista
    function makeListBlock(label, value, color) {
        const block = document.createElement('div');
        block.style.cssText = 'margin-bottom:10px';

        const lbl = document.createElement('div');
        lbl.style.cssText = 'font-size:11px;color:' + color + ';font-weight:700;margin-bottom:4px';
        lbl.textContent = label + ' (' + value.split(',').length + ' valores)';
        block.appendChild(lbl);

        const area = document.createElement('textarea');
        area.value = value;
        area.readOnly = true;
        area.style.cssText = 'width:100%;height:60px;background:#0f172a;color:#e2e8f0;border:1px solid ' + color + ';border-radius:8px;padding:8px;font-size:10px;font-family:monospace;resize:none;line-height:1.3';
        block.appendChild(area);

        const btnRow = document.createElement('div');
        btnRow.style.cssText = 'display:flex;gap:6px;margin-top:4px';

        const btnSel = document.createElement('button');
        btnSel.innerHTML = '🔵 Seleccionar';
        btnSel.style.cssText = 'flex:1;padding:8px;background:linear-gradient(135deg,#2563eb,#3b82f6);color:#fff;border:none;border-radius:8px;font-size:12px;font-weight:600;cursor:pointer';
        btnSel.onclick = () => {
            area.focus(); area.select();
            area.setSelectionRange(0, value.length);
            btnSel.innerHTML = '✅ ¡Seleccionado! Mantén pulsado → Copiar';
            btnSel.style.background = '#059669';
            setTimeout(() => { btnSel.innerHTML = '🔵 Seleccionar'; btnSel.style.background = 'linear-gradient(135deg,#2563eb,#3b82f6)'; }, 4000);
        };
        btnRow.appendChild(btnSel);
        block.appendChild(btnRow);

        return block;
    }

    modal.appendChild(makeListBlock('x₁ (tiempo en ' + timeUnitLabel() + ')', lists.x, '#3b82f6'));
    modal.appendChild(makeListBlock('y₁ (' + lists.label + ' en ' + lists.unit + ')', lists.y, '#10b981'));

    // Info
    const info = document.createElement('p');
    info.style.cssText = 'font-size:11px;color:#64748b;text-align:center;margin:4px 0';
    info.textContent = lists.count + ' puntos • Luego escribe (x₁, y₁) en Desmos para graficar';
    modal.appendChild(info);

    // Cerrar
    const btnClose = document.createElement('button');
    btnClose.textContent = 'Cerrar';
    btnClose.style.cssText = 'width:100%;padding:10px;margin-top:8px;background:transparent;border:1px solid #ef4444;color:#ef4444;border-radius:10px;font-size:13px;cursor:pointer';
    btnClose.onclick = () => overlay.remove();
    modal.appendChild(btnClose);

    overlay.appendChild(modal);
    overlay.addEventListener('click', (e) => { if (e.target === overlay) overlay.remove(); });
    document.body.appendChild(overlay);
}

// ─── Manual Copy Modal (Fallback para móvil cuando Clipboard API falla) ───
function showManualCopyModal(tsvText) {
    const overlay = document.createElement('div');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.92);display:flex;justify-content:center;align-items:center;z-index:10000;backdrop-filter:blur(6px)';

    const modal = document.createElement('div');
    modal.style.cssText = 'background:#1e293b;padding:20px;border-radius:16px;width:92%;max-width:500px;max-height:85vh;color:#fff;box-shadow:0 25px 60px rgba(0,0,0,0.7);border:1px solid rgba(255,255,255,0.1);display:flex;flex-direction:column;overflow:hidden';

    // Título
    const title = document.createElement('h3');
    title.innerHTML = '📝 Copiar datos manualmente';
    title.style.cssText = 'margin:0 0 4px 0;color:#14f0c5;font-size:16px';
    modal.appendChild(title);

    // Instrucciones
    const steps = document.createElement('div');
    steps.style.cssText = 'font-size:12px;color:#94a3b8;margin-bottom:12px;line-height:1.6';
    steps.innerHTML = 
        '<b style="color:#f0b429">Instrucciones para celular:</b><br>' +
        '1️⃣ Toca <b>"Seleccionar Todo"</b> abajo<br>' +
        '2️⃣ Mantén pulsado el texto → <b>Copiar</b><br>' +
        '3️⃣ Abre la app <b>Desmos</b><br>' +
        '4️⃣ Toca <b>+</b> → <b>Tabla</b> → pega en la primera celda';
    modal.appendChild(steps);

    // Textarea con datos
    const area = document.createElement('textarea');
    area.value = tsvText;
    area.readOnly = true;
    area.style.cssText = 'width:100%;flex:1;min-height:150px;max-height:40vh;background:#0f172a;color:#e2e8f0;border:2px solid #3b82f6;border-radius:10px;padding:12px;font-size:11px;font-family:monospace;resize:none;line-height:1.4';
    modal.appendChild(area);

    // Info de filas
    const info = document.createElement('p');
    const lines = tsvText.split('\n').length - 1;
    info.style.cssText = 'font-size:11px;color:#64748b;margin:6px 0;text-align:center';
    info.textContent = lines + ' filas de datos • ' + (new Blob([tsvText]).size / 1024).toFixed(1) + ' KB';
    modal.appendChild(info);

    // Botones
    const btnRow = document.createElement('div');
    btnRow.style.cssText = 'display:flex;gap:8px;margin-top:8px';

    const btnSelect = document.createElement('button');
    btnSelect.innerHTML = '🔵 Seleccionar Todo';
    btnSelect.className = 'btn-action';
    btnSelect.style.cssText = 'flex:1;padding:12px;background:linear-gradient(135deg,#2563eb,#3b82f6);color:#fff;border:none;border-radius:10px;font-size:13px;font-weight:600;cursor:pointer';
    btnSelect.onclick = () => {
        area.focus();
        area.select();
        area.setSelectionRange(0, tsvText.length);
        btnSelect.innerHTML = '✅ ¡Seleccionado! — Mantén pulsado → Copiar';
        btnSelect.style.background = 'linear-gradient(135deg,#059669,#10b981)';
        setTimeout(() => {
            btnSelect.innerHTML = '🔵 Seleccionar Todo';
            btnSelect.style.background = 'linear-gradient(135deg,#2563eb,#3b82f6)';
        }, 4000);
    };

    const btnClose = document.createElement('button');
    btnClose.textContent = '✕';
    btnClose.className = 'btn-action';
    btnClose.style.cssText = 'width:48px;padding:12px;background:transparent;border:1px solid #ef4444;color:#ef4444;border-radius:10px;font-size:16px;cursor:pointer';
    btnClose.onclick = () => overlay.remove();

    btnRow.appendChild(btnSelect);
    btnRow.appendChild(btnClose);
    modal.appendChild(btnRow);

    // Botón compartir como alternativa (si está disponible)
    if (navigator.share) {
        const btnShare = document.createElement('button');
        btnShare.innerHTML = '📤 O comparte como archivo TSV';
        btnShare.className = 'btn-action';
        btnShare.style.cssText = 'width:100%;padding:10px;margin-top:6px;background:rgba(255,255,255,0.05);color:#94a3b8;border:1px solid rgba(255,255,255,0.15);border-radius:10px;font-size:12px;cursor:pointer';
        btnShare.onclick = () => {
            const file = new File([tsvText], 'physys_datos.tsv', { type: 'text/tab-separated-values' });
            if (navigator.canShare && navigator.canShare({ files: [file] })) {
                navigator.share({ title: 'Physys Lab', files: [file] }).catch(() => {});
            } else {
                navigator.share({ title: 'Physys Lab', text: tsvText }).catch(() => {});
            }
        };
        modal.appendChild(btnShare);
    }

    overlay.appendChild(modal);
    overlay.addEventListener('click', (e) => { if (e.target === overlay) overlay.remove(); });
    document.body.appendChild(overlay);

    // Auto-seleccionar el texto
    setTimeout(() => { area.focus(); area.select(); }, 100);
}

function fetchConfig() {
    fetch('/api/status')
        .then(r => r.json())
        .then(data => {
            if (data.config) {
                if (data.config.tof_model) $('tof_model').value = data.config.tof_model;
                if (data.config.sample_rate) $('sample_rate').value = data.config.sample_rate;
                if (data.config.usb_log !== undefined && $('usb-auto-log')) $('usb-auto-log').checked = data.config.usb_log;
                if (data.config.invert_encoder !== undefined && $('invert-encoder-check')) $('invert-encoder-check').checked = data.config.invert_encoder;
                
                // HX711 Filter Sync
                if (data.config.hx_filter !== undefined) {
                    if ($('hx-filter-check')) $('hx-filter-check').checked = data.config.hx_filter;
                    if ($('config-hx-filter-check')) $('config-hx-filter-check').checked = data.config.hx_filter;
                }

                if (data.config.hx_high_stab !== undefined && $('config-hx-stability-check')) {
                    $('config-hx-stability-check').checked = data.config.hx_high_stab;
                }
                if (data.config.tube_length !== undefined) {
                    if ($('tube_length')) $('tube_length').value = data.config.tube_length;
                    if ($('auto-stop-dist')) $('auto-stop-dist').value = data.config.tube_length;
                }
                if (data.config.tof_range !== undefined && $('tof_range')) {
                    $('tof_range').value = data.config.tof_range;
                }
                updateToFUI();
            }
            const sensors = data.sensors || {};
            updateUsbUI(sensors.usb, sensors.usb ? 'Pendrive Conectado' : 'No detectado');
        });
}

function updateUsbUI(connected, label) {
    const box = $('usb-status-box');
    if (!box) return; // Prevent error if USB UI is hidden
    const dot = box.querySelector('.status-dot');
    const lbl = $('usb-label');
    if (connected) {
        dot.className = 'status-dot connected';
        lbl.textContent = label || 'Pendrive conectado';
    } else {
        dot.className = 'status-dot disconnected';
        lbl.textContent = 'Pendrive no detectado';
    }
}

function saveTofConfig() {
    const model = $('tof_model').value;
    const rate = $('sample_rate').value;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('SET_TOF:' + model);
        ws.send('SET_RATE:' + rate); // Save both when applying
        alert('Configuración guardada (ToF y Frecuencia). El ESP32 se reiniciará para aplicar los cambios.');
        setTimeout(() => {
            if (ws && ws.readyState === WebSocket.OPEN) ws.send('REBOOT');
        }, 800);
    }
}

function saveSampleRate() {
    const rate = $('sample_rate').value;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('SET_RATE:' + rate);
        alert('Frecuencia de muestreo actualizada a ' + (1000/rate) + ' Hz');
    }
}

function toggleHxFilter() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('TOGGLE_HX_FILTER');
    }
}

function toggleHxStability() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('TOGGLE_HX_STABILITY');
    }
}

// Removed misplaced code block

function toggleUsbAutoLog() {
    const active = $('usb-auto-log').checked;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('SET_USB_LOG:' + (active ? 'ON' : 'OFF'));
    }
}

function resetEncoder() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('RESET_ENC');
        chartData = [];
        drawChart();
    }
}

function exportToUSB() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('EXPORT_USB');
        alert('Iniciando exportación a USB. El LED parpadeará en verde al terminar.');
    } else {
        alert('Error: No hay conexión con el dispositivo');
    }
}

function rebootESP() {
    if (confirm('¿Estás seguro de que quieres reiniciar el ESP32? Se perderá la conexión temporalmente.')) {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('REBOOT');
        }
    }
}

// ─── Toma Automática ───
// (Definición principal en línea ~629)

function setTubeLength() {
    const len = $('tube_length') ? $('tube_length').value : ($('auto-stop-dist') ? $('auto-stop-dist').value : '0');
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('SET_TUBE:' + len);
    }
}

// ─── Python Editor Logic ───
function updateLineNumbers() {
    const editor = $('python-editor');
    const lineNums = $('line-nums');
    if (!editor || !lineNums) return;
    const lines = editor.value.split('\n').length;
    lineNums.innerHTML = Array.from({ length: lines }, (_, i) => i + 1).join('<br>');
    $('editor-info').textContent = new Blob([editor.value]).size + ' bytes';
}

function loadExampleScript(exampleKey) {
    const script = PYTHON_EXAMPLES[exampleKey];
    if (!script) return;
    $('python-editor').value = script;
    updateLineNumbers();
    setEditorStatus('Ejemplo cargado: ' + exampleKey, 'success');
}

function loadCurrentScript() {
    setEditorStatus('Cargando desde ESP32...', '');
    fetch('/api/python')
        .then(r => {
            if (!r.ok) throw new Error('No encontrado');
            return r.text();
        })
        .then(text => {
            $('python-editor').value = text;
            updateLineNumbers();
            setEditorStatus('Script cargado desde ESP32', 'success');
        })
        .catch(e => {
            setEditorStatus('Error: ' + e.message, 'error');
        });
}

function saveScript() {
    const code = $('python-editor').value;
    if (!code.trim()) {
        setEditorStatus('Error: El script está vacío', 'error');
        return;
    }
    setEditorStatus('Guardando en ESP32...', '');
    fetch('/api/python', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: code
    })
        .then(r => r.json())
        .then(data => {
            if (data.ok) {
                setEditorStatus('✓ Guardado exitóso (' + new Blob([code]).size + ' bytes)', 'success');
            } else {
                setEditorStatus('Error al guardar', 'error');
            }
        })
        .catch(e => {
            setEditorStatus('Error de conexión', 'error');
        });
}

function setEditorStatus(msg, cls) {
    const el = $('editor-status');
    el.textContent = msg;
    el.className = 'editor-status' + (cls ? ' ' + cls : '');
}

// ─── ToF UI Helpers ───
const TOF_INFO = {
    'vl53l0x': 'Alcance hasta 2.0m. Resolución 1mm. Zona muerta: <30mm. Ideal para rieles de aire y caída libre.',
    'vl53l1x': 'Alcance hasta 4.0m. Resolución 1mm. Zona muerta: <40mm. Resistente a luz ambiental intensa.',
    'vl53l1xv2': 'VL53L1X v2 optimizado. Zona muerta: <40mm. Mejor precisión a larga distancia.',
    'vl6180': 'Alcance corto (60cm). Muy alta precisión. Zona muerta: <10mm. Ideal para experimentos de mesa pequeños.',
    'vl53l5x': 'Multizona (8x8). Permite medir múltiples objetos. Zona muerta: <20mm.'
};

function copyDataToClipboard() {
    const tabConf = TAB_CONFIG[currentTab];
    const sensor = tabConf ? tabConf.sensor : null;
    if (!sensor || !sensorData[sensor] || sensorData[sensor].length === 0) {
        alert('No hay datos para copiar. Realiza una medición primero.');
        return;
    }
    
    const exportData = prepareExportData(sensor);
    const jsonStr = JSON.stringify(exportData, null, 2);
    
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(jsonStr).then(() => {
            const btn = $('btn-copy-data');
            const oldText = btn.textContent;
            btn.innerHTML = '✅ ¡Copiado!';
            setTimeout(() => btn.innerHTML = '📋 Copiar Datos', 2000);
        }).catch(() => {
            alert('Error al copiar. Usa el menú de Exportar.');
        });
    } else {
        alert('Copia no soportada. Usa el menú de Exportar.');
    }
}

function updateToFUI() {
    const modelEl = $('tof_model');
    if (!modelEl) return;
    const model = modelEl.value;
    const desc = $('tof_model_desc');
    const rangeContainer = $('tof_range_container');
    
    if (desc) desc.textContent = TOF_INFO[model] || 'Modelo estándar I2C.';
    if (rangeContainer) {
        rangeContainer.style.display = (model === 'vl53l0x') ? 'block' : 'none';
    }
}

// ─── Init ───
document.addEventListener('DOMContentLoaded', () => {
    // Sensor dropdown — use 'input' + track mousedown for reselection
    const sensorSelect = $('sensor-select');
    if (sensorSelect) {
        sensorSelect.addEventListener('change', (e) => switchTab(e.target.value));
        // Permitir retornar de paneles config/gpio tocando o clickeando el selector de sensores
        sensorSelect.addEventListener('click', () => {
            if (!['movimiento', 'rotacion', 'fuerza'].includes(currentTab)) {
                switchTab(sensorSelect.value);
            }
        });
        // Allow re-selecting the same sensor (e.g., to return from config)
        let dropdownOpened = false;
        sensorSelect.addEventListener('focus', () => { dropdownOpened = true; });
        sensorSelect.addEventListener('blur', () => {
            if (dropdownOpened && !['movimiento','rotacion','fuerza'].includes(currentTab)) {
                // User opened dropdown while in config/gpio — switch to selected sensor
                switchTab(sensorSelect.value);
            }
            dropdownOpened = false;
        });
    }

    // Desenfocar selectores tras cambio de valor para evitar secuestro de scroll
    document.addEventListener('change', (e) => {
        if (e.target.tagName === 'SELECT') {
            e.target.blur();
        }
    });

    // Nav icon buttons — toggle behavior (click again = go back)
    if ($('btn-nav-chart')) $('btn-nav-chart').addEventListener('click', () => {
        switchTab(lastSensorTab || 'movimiento');
    });
    if ($('btn-nav-config')) $('btn-nav-config').addEventListener('click', () => {
        const isConfig = currentTab === 'config';
        switchTab(isConfig ? lastSensorTab : 'config');
        if (!isConfig) setTimeout(updateToFUI, 50); // Garantizar UI actualizada al abrir
    });
    if ($('btn-nav-gpio')) $('btn-nav-gpio').addEventListener('click', () => {
        switchTab(currentTab === 'gpioview' ? lastSensorTab : 'gpioview');
    });

    // Variable selector
    $('graph-variable').addEventListener('change', (e) => {
        selectedVariable = e.target.value;
        chartData = [];
    });

    // Action buttons
    $('btn-record').addEventListener('click', toggleRecording);
    $('btn-export').addEventListener('click', openExportModal); // Actualizado para usar el modal unificado

    // Python editor events
    if ($('example-select')) $('example-select').addEventListener('change', (e) => {
        if (e.target.value) loadExampleScript(e.target.value);
    });
    if ($('btn-load-script')) $('btn-load-script').addEventListener('click', loadCurrentScript);
    if ($('btn-save-script')) $('btn-save-script').addEventListener('click', saveScript);
    if ($('python-editor')) {
        $('python-editor').addEventListener('input', updateLineNumbers);
        $('python-editor').addEventListener('scroll', () => {
            $('line-nums').scrollTop = $('python-editor').scrollTop;
        });
        // Tab support in textarea
        $('python-editor').addEventListener('keydown', (e) => {
            if (e.key === 'Tab') {
                e.preventDefault();
                const ta = e.target;
                const start = ta.selectionStart;
                ta.value = ta.value.substring(0, start) + '    ' + ta.value.substring(ta.selectionEnd);
                ta.selectionStart = ta.selectionEnd = start + 4;
                updateLineNumbers();
            }
        });
    }

    // Config buttons
    if ($('btn-save-config')) $('btn-save-config').addEventListener('click', saveAllConfig);
    if ($('usb-auto-log')) $('usb-auto-log').addEventListener('change', toggleUsbAutoLog);
    if ($('btn-usb-mount')) $('btn-usb-mount').addEventListener('click', () => {
        fetchConfig();
        alert('Re-escaneando dispositivos USB...');
    });
    if ($('btn-reboot')) $('btn-reboot').addEventListener('click', rebootESP);

    // GPIO Viewer rate change
    if ($('gv-rate')) $('gv-rate').addEventListener('change', () => {
        if (gvActive) startGpioPolling();
    });

    // Init first tab
    switchTab('movimiento');

    // Connect WebSocket
    connectWS();

    // Poll system info every 5s
    fetchSystemInfo();
    setInterval(fetchSystemInfo, 5000);

    // Load branding from config.json
    fetch('/api/config')
        .then(r => r.json())
        .then(cfg => {
            const titleEl = document.querySelector('.title-block h1');
            if (titleEl) {
                titleEl.innerHTML = (cfg.lab_name || 'Physys Lab') +
                    ' <span class="v-tag">' + (cfg.version || 'v9.0') + '</span>';
            }
            if (cfg.lab_name) {
                localStorage.setItem('physys_lab_name', cfg.lab_name);
            }
            if (cfg.institution) {
                setStatus(cfg.institution_short || cfg.institution, '#14f0c5');
                localStorage.setItem('physys_institution', cfg.institution);
            }
        })
        .catch(() => {});

    // Init Firebase Sync module (H5)
    if (typeof initFirebaseSync === 'function') {
        initFirebaseSync();
    }

    // Redraw chart on resize
    window.addEventListener('resize', drawChart);

    // KEYBOARD SUBMIT IN AUTH INPUT
    if ($('auth-input')) {
        $('auth-input').addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                e.preventDefault();
                submitAuth();
            }
        });
    }

    // GPIO Board Interactive Clicks for direct reassociation
    const gvBoard = document.querySelector('.gv-board');
    if (gvBoard) {
        gvBoard.addEventListener('click', (e) => {
            const pinEl = e.target.closest('.gv-pin');
            if (!pinEl) return;

            if (currentUserRole !== 'teacher') {
                showNotification('🔒 Solo el Docente puede configurar los pines de forma interactiva.', '#ff4d6a');
                return;
            }

            const gpioNum = parseInt(pinEl.getAttribute('data-gpio'));
            if (!isNaN(gpioNum)) {
                showPinConfigTooltip(gpioNum, pinEl);
            }
        });
    }

    // Captive Portal detection and full-screen premium guide on load
    const hostname = location.hostname;
    if (hostname && hostname !== '192.168.4.1' && hostname !== 'physyslab.local' && hostname !== 'localhost' && hostname !== '127.0.0.1') {
        showCaptivePortalOverlay();
    }

    // Initial role and hardware UI sync
    syncRoleUI();
});

// ─── MULTI-USER ROLE MANAGEMENT & HARDWARE PROFILE UTILITIES ───
const SAFE_PINS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 26, 35, 36, 37, 38, 39, 40, 41, 42, 45, 46, 47];

function syncRoleUI() {
    const isLocked = (currentUserRole === 'student');
    const writeButtons = [
        'btn-record', 'btn-sensor-rec', 'btn-sensor-tare', 'btn-sensor-invert',
        'btn-sensor-reset-enc', 'btn-trigger', 'btn-trigger-stop', 'btn-clear-esp', 
        'btn-save-config', 'btn-reboot', 'btn-save-script', 'btn-load-script',
        'btn-config-reset-enc', 'btn-config-tare'
    ];
    
    writeButtons.forEach(id => {
        const btn = $(id);
        if (!btn) return;
        if (isLocked) {
            btn.classList.add('lock-disabled');
            btn.setAttribute('disabled', 'true');
            btn.style.opacity = '0.5';
            btn.style.cursor = 'not-allowed';
        } else {
            btn.classList.remove('lock-disabled');
            btn.removeAttribute('disabled');
            btn.style.opacity = '';
            btn.style.cursor = '';
        }
    });

    // Intercept student action triggers via capturing listener if locked
    if (!window.hasRoleCapturingListener) {
        document.body.addEventListener('click', (e) => {
            const btn = e.target.closest('.lock-disabled');
            if (btn) {
                e.preventDefault();
                e.stopPropagation();
                if (isLeaderActive) {
                    showNotification(`⚠️ Control bloqueado. Mesa bajo control del Líder activo en IP ${leaderIp}.`, '#ff4d6a');
                } else {
                    showNotification(`🔒 Acción bloqueada. Solo un Líder de Mesa o el Docente pueden realizar esta acción.`, '#ff4d6a');
                }
            }
        }, true);
        window.hasRoleCapturingListener = true;
    }

    // Role badge representation
    const badge = $('role-badge');
    const roleText = $('role-text');
    if (badge && roleText) {
        badge.className = 'role-badge';
        if (currentUserRole === 'student') {
            badge.classList.add('role-student');
            roleText.innerHTML = '🔒 Estudiante (Solo Lectura)';
        } else if (currentUserRole === 'leader') {
            badge.classList.add('role-leader');
            roleText.innerHTML = '⭐ Líder de Mesa';
        } else if (currentUserRole === 'teacher') {
            badge.classList.add('role-teacher');
            roleText.innerHTML = '🎓 Docente (Admin)';
        }
    }

    // Dynamic body classes for role-based CSS rules
    document.body.classList.remove('role-student', 'role-leader', 'role-teacher');
    document.body.classList.add('role-' + currentUserRole);

    // Disable/enable pin profile selections according to the role and preset state
    const canEditPins = (currentUserRole === 'teacher');
    const profileSelect = $('pin-profile-select');
    if (profileSelect) {
        profileSelect.disabled = !canEditPins;
    }
    const selects = ['pin-tof-sda', 'pin-tof-scl', 'pin-enc-sda', 'pin-enc-scl', 'pin-hx-dt', 'pin-hx-sck'];
    selects.forEach(id => {
        const el = $(id);
        if (el) {
            const isCustom = (profileSelect && profileSelect.value === 'custom');
            el.disabled = !canEditPins || !isCustom;
        }
    });

    // Disable/enable general configuration inputs (Students can only view)
    const canEditConfig = (currentUserRole === 'teacher' || currentUserRole === 'leader');
    const configInputs = [
        'tof_model', 'tof_range', 'sample_rate', 'tube_length',
        'invert-encoder-check', 'config-hx-filter-check', 'config-hx-stability-check',
        'hx-filter-check', 'hx-stability-check', 'auto-stop-dist'
    ];
    configInputs.forEach(id => {
        const el = $(id);
        if (el) el.disabled = !canEditConfig;
    });

    // Toggle teacher settings panel (only for Docente)
    const teacherSettings = $('gv-teacher-settings');
    if (teacherSettings) {
        teacherSettings.style.display = canEditPins ? 'block' : 'none';
    }

    // Update status bar text beautifully
    updateLeaderStatusBanner();
}

function openAuthModal() {
    const modal = $('auth-modal');
    if (!modal) return;
    modal.style.display = 'flex';
    modal.classList.add('active');
    selectRole(currentUserRole === 'student' ? 'leader' : currentUserRole);
}

function closeAuthModal() {
    const modal = $('auth-modal');
    if (!modal) return;
    modal.style.display = 'none';
    modal.classList.remove('active');
}

function selectRole(role) {
    selectedAuthRole = role;
    
    // Update active visual card state
    ['student', 'leader', 'teacher'].forEach(r => {
        const card = $('rc-' + r);
        if (card) card.classList.remove('active');
    });
    
    const activeCard = $('rc-' + role);
    if (activeCard) activeCard.classList.add('active');
    
    // Show/hide PIN entry container
    const entryContainer = $('pin-entry-container');
    const label = $('pin-label');
    const input = $('auth-input');
    
    if (role === 'student') {
        if (entryContainer) entryContainer.style.display = 'none';
        // Auto validation for student mode as it is completely passwordless
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('DEAUTH');
        }
        closeAuthModal();
    } else {
        if (entryContainer) entryContainer.style.display = 'block';
        if (label) {
            label.textContent = (role === 'leader') ? 'Ingrese PIN de Mesa (1234):' : 'Ingrese Contraseña Docente:';
        }
        if (input) {
            input.value = '';
            input.placeholder = (role === 'leader') ? '••••' : 'Contraseña';
            input.type = (role === 'leader') ? 'number' : 'password';
            input.focus();
        }
    }
}

function submitAuth() {
    const role = selectedAuthRole;
    if (role === 'student') {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('DEAUTH');
        }
        closeAuthModal();
        return;
    }
    
    const input = $('auth-input');
    if (!input) return;
    const value = input.value.trim();
    if (!value) {
        showNotification('⚠️ Por favor ingrese el PIN o la clave.', '#ff4d6a');
        return;
    }
    
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send('AUTH:' + value);
    } else {
        showNotification('❌ Error: Sin conexión con la tarjeta', '#ff4d6a');
    }
}

function applyPinProfilePreset(preset) {
    const isCustom = (preset === 'custom');
    const selects = ['pin-tof-sda', 'pin-tof-scl', 'pin-enc-sda', 'pin-enc-scl', 'pin-hx-dt', 'pin-hx-sck'];
    
    const presets = {
        basic: {
            'pin-tof-sda': '4',
            'pin-tof-scl': '5',
            'pin-enc-sda': '10',
            'pin-enc-scl': '11',
            'pin-hx-dt': '6',
            'pin-hx-sck': '7'
        },
        cam: {
            'pin-tof-sda': '1',
            'pin-tof-scl': '47',
            'pin-enc-sda': '14',
            'pin-enc-scl': '21',
            'pin-hx-dt': '41',
            'pin-hx-sck': '42'
        }
    };
    
    if (preset !== 'custom') {
        const valMap = presets[preset];
        for (const [id, val] of Object.entries(valMap)) {
            const el = $(id);
            if (el) {
                // Forzar que exista la opción antes de asignar
                if (!Array.from(el.options).some(opt => opt.value == val)) {
                    const opt = document.createElement('option');
                    opt.value = val;
                    opt.text = 'GPIO ' + val;
                    el.appendChild(opt);
                }
                el.value = val;
                el.disabled = true;
            }
        }
    } else {
        selects.forEach(id => {
            const el = $(id);
            if (el) el.disabled = false;
        });
    }
}

function populatePinSelectors() {
    const isCamera = activeHardwareProfile.camera_detected;
    const cameraType = activeHardwareProfile.type;
    
    // Camera pins to flag in red
    const freenoveCamPins = [4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 39, 40, 41, 42];
    const standardCamPins = [17, 18];
    const blockedPins = isCamera ? (cameraType === "freenove_cam" ? freenoveCamPins : standardCamPins) : [];

    const selectors = ['pin-tof-sda', 'pin-tof-scl', 'pin-enc-sda', 'pin-enc-scl', 'pin-hx-dt', 'pin-hx-sck'];
    selectors.forEach(id => {
        const selectEl = $(id);
        if (!selectEl) return;
        
        const currentVal = selectEl.value;
        
        selectEl.innerHTML = SAFE_PINS.map(pin => {
            const isBlocked = blockedPins.includes(pin);
            const label = 'GPIO ' + pin + (isBlocked ? ' ⚠️ (Cámara)' : '');
            const disabledAttr = isBlocked ? ' style="color:#f87171;"' : '';
            return `<option value="${pin}"${disabledAttr}>${label}</option>`;
        }).join('');
        
        if (activeHardwareProfile.pins) {
            const pinKey = id.replace('pin-', '').replace('-', '_');
            const activePin = activeHardwareProfile.pins[pinKey];
            if (activePin !== undefined) {
                selectEl.value = activePin;
            }
        } else if (currentVal) {
            selectEl.value = currentVal;
        }
    });

    // Detectar si el pinout actual corresponde a Básico o Cámara para marcar el selector de perfiles
    if (activeHardwareProfile.pins) {
        const p = activeHardwareProfile.pins;
        const isBasic = (p.tof_sda == 4 && p.tof_scl == 5 && p.enc_sda == 10 && p.enc_scl == 11 && p.hx_dt == 6 && p.hx_sck == 7);
        const isCam = (p.tof_sda == 1 && p.tof_scl == 2 && p.enc_sda == 3 && p.enc_scl == 14 && p.hx_dt == 21 && p.hx_sck == 26);
        
        const profileSelect = $('pin-profile-select');
        if (profileSelect) {
            if (isBasic) {
                profileSelect.value = 'basic';
                applyPinProfilePreset('basic');
            } else if (isCam) {
                profileSelect.value = 'cam';
                applyPinProfilePreset('cam');
            } else {
                profileSelect.value = 'custom';
                applyPinProfilePreset('custom');
            }
        }
        
        // Dynamically synchronize the sidebar ESP32 hardware diagram with the active pins configuration
        updateSidebarDiagram();
    }
}

function applyCustomPins() {
    if (currentUserRole !== 'teacher') {
        showNotification('⚠️ Solo el Docente puede cambiar los pines de hardware.', '#ff4d6a');
        return;
    }

    const tofSda = parseInt($('pin-tof-sda').value);
    const tofScl = parseInt($('pin-tof-scl').value);
    const encSda = parseInt($('pin-enc-sda').value);
    const encScl = parseInt($('pin-enc-scl').value);
    const hxDt = parseInt($('pin-hx-dt').value);
    const hxSck = parseInt($('pin-hx-sck').value);

    // Unique values assertion (ignorando NaN si los hay)
    const values = [tofSda, tofScl, encSda, encScl, hxDt, hxSck].filter(v => !isNaN(v));
    const uniqueValues = new Set(values);
    if (uniqueValues.size !== values.length || values.length !== 6) {
        alert('❌ Error: Faltan pines por asignar o hay pines duplicados.');
        return;
    }

    // Camera overlap detection and warnings
    const isCamera = activeHardwareProfile.camera_detected;
    const cameraType = activeHardwareProfile.type;
    const freenoveCamPins = [4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 39, 40, 41, 42];
    const standardCamPins = [17, 18];
    const blockedPins = isCamera ? (cameraType === "freenove_cam" ? freenoveCamPins : standardCamPins) : [];
    
    const overlap = values.filter(v => blockedPins.includes(v));
    if (overlap.length > 0) {
        if (!confirm(`⚠️ Advertencia de Conflicto de Cámara:\n\nLos pines [${overlap.join(', ')}] están ocupados por el bus de la cámara conectada.\nSi continúas, podrías experimentar fallos en el sistema o daños de comunicación.\n\n¿Estás seguro de que deseas aplicar esta configuración de pines?`)) {
            return;
        }
    }

    const cmd = `SET_PINS:${tofSda},${tofScl},${encSda},${encScl},${hxDt},${hxSck}`;
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(cmd);
        showNotification('🔌 Enviando reasignación de pines a la tarjeta...', '#38bdf8');
    }
}

function updateHardwareStatusBanner() {
    const banner = $('gv-auto-banner');
    if (!banner) return;

    const isCamera = activeHardwareProfile.camera_detected;
    const type = activeHardwareProfile.type;

    if (isCamera) {
        banner.style.display = 'block';
        banner.className = 'gv-banner camera-active';
        if (type === 'freenove_cam') {
            banner.innerHTML = `<strong>📸 Cámara Físicamente Conectada (Perfil Freenove CAM)</strong><br>
            Los pines del bus de la cámara (GPIOs 4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 39, 40, 41, 42) están bloqueados por hardware. Los sensores se reubican por defecto en: <strong>ToF: 1/2, Encoder: 3/14, HX711: 21/26</strong> para evitar interferencias.`;
        } else {
            banner.innerHTML = `<strong>📸 Cámara Físicamente Conectada (Perfil CAM Estándar)</strong><br>
            La cámara está en uso (SDA: 17, SCL: 18). Los pines del bus de cámara se encuentran ocupados.`;
        }
    } else {
        banner.style.display = 'block';
        banner.className = 'gv-banner camera-inactive';
        banner.innerHTML = `<strong>ℹ️ Placa Base Estándar Activa (Sin Cámara Detectada)</strong><br>
        Todos los pines del microcontrolador están libres para uso general. Los sensores están asignados al estándar: <strong>ToF: 4/5, Encoder: 10/11, HX711: 6/7</strong>.`;
    }
}

function updateLeaderStatusBanner() {
    if (isLeaderActive && currentUserRole !== 'leader' && currentUserRole !== 'teacher') {
        setStatus(`🔒 Control bloqueado por Mesa en IP ${leaderIp}`, '#f0b429');
    } else if (currentUserRole === 'leader') {
        setStatus('⭐ Líder de Mesa (Modo Control Activo)', '#eab308');
    } else if (currentUserRole === 'teacher') {
        setStatus('🎓 Docente (Modo Administrador)', '#38bdf8');
    } else {
        if (ws && ws.readyState === WebSocket.OPEN) {
            setStatus('Conectado', '#14f0c5');
        } else {
            setStatus('Desconectado', '#ff4d6a');
        }
    }
}

function showNotification(message, color = '#14f0c5') {
    let container = $('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        container.style.cssText = 'position:fixed;bottom:24px;right:24px;display:flex;flex-direction:column;gap:10px;z-index:99999;pointer-events:none;max-width:320px;';
        document.body.appendChild(container);
    }
    
    const toast = document.createElement('div');
    toast.className = 'glass-card';
    toast.style.cssText = `
        background: rgba(30, 41, 59, 0.95);
        color: #fff;
        padding: 12px 18px;
        border-radius: 12px;
        border-left: 4px solid ${color};
        box-shadow: 0 10px 25px rgba(0, 0, 0, 0.4), 0 0 15px ${color}33;
        font-size: 13px;
        font-weight: 500;
        pointer-events: auto;
        opacity: 0;
        transform: translateY(20px);
        transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
        backdrop-filter: blur(8px);
    `;
    toast.textContent = message;
    container.appendChild(toast);
    
    setTimeout(() => {
        toast.style.opacity = '1';
        toast.style.transform = 'translateY(0)';
    }, 10);
    
    setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateY(-20px)';
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

// ─── DYNAMIC HARDWARE DIAGRAM, DIRECT PIN TOOLTIPS, AND CAPTIVE PORTAL ONBOARDING ───

function updateSidebarDiagram() {
    const leftCol = document.querySelector('.pins-col.pins-left');
    const rightCol = document.querySelector('.pins-col.pins-right');
    if (!leftCol || !rightCol || !activeHardwareProfile.pins) return;

    const p = activeHardwareProfile.pins;

    leftCol.innerHTML = `
        <div class="p-node i2c" data-gpio="${p.tof_sda}"><span class="pin-num">${p.tof_sda}</span> TOF-SDA</div>
        <div class="p-node i2c" data-gpio="${p.tof_scl}"><span class="pin-num">${p.tof_scl}</span> TOF-SCL</div>
        <div class="p-node serial" data-gpio="${p.hx_dt}"><span class="pin-num">${p.hx_dt}</span> HX-DT</div>
        <div class="p-node serial" data-gpio="${p.hx_sck}"><span class="pin-num">${p.hx_sck}</span> HX-SCK</div>
    `;

    rightCol.innerHTML = `
        <div class="p-node i2c" data-gpio="${p.enc_sda}"><span class="pin-num">${p.enc_sda}</span> ENC-SDA</div>
        <div class="p-node i2c" data-gpio="${p.enc_scl}"><span class="pin-num">${p.enc_scl}</span> ENC-SCL</div>
        <div class="p-node led" data-gpio="48"><span class="pin-num">48</span> WS2812</div>
        <div class="p-node boot-pin" data-gpio="0"><span class="pin-num">0</span> BOOT</div>
    `;
}

let currentGvTooltip = null;

function showPinConfigTooltip(gpioNum, targetEl) {
    if (currentGvTooltip) {
        currentGvTooltip.remove();
        currentGvTooltip = null;
    }

    const isCamera = activeHardwareProfile.camera_detected;
    const cameraType = activeHardwareProfile.type;
    const freenoveCamPins = [4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 39, 40, 41, 42];
    const standardCamPins = [17, 18];
    const blockedPins = isCamera ? (cameraType === "freenove_cam" ? freenoveCamPins : standardCamPins) : [];
    const isBlocked = blockedPins.includes(gpioNum);

    const tooltip = document.createElement('div');
    tooltip.className = 'gv-tooltip glass-card';
    
    // Position tooltip near targetEl
    const rect = targetEl.getBoundingClientRect();
    const scrollTop = window.pageYOffset || document.documentElement.scrollTop;
    const scrollLeft = window.pageXOffset || document.documentElement.scrollLeft;
    
    if (rect.left > window.innerWidth / 2) {
        tooltip.style.left = (rect.left + scrollLeft - 190) + 'px';
    } else {
        tooltip.style.left = (rect.right + scrollLeft + 10) + 'px';
    }
    tooltip.style.top = (rect.top + scrollTop) + 'px';

    const titleColor = isBlocked ? '#f87171' : '#38bdf8';
    const warningMsg = isBlocked ? `<div style="font-size:0.6rem; color:#f87171; text-align:center; margin-bottom:8px; line-height:1.2;">⚠️ Reservado por Cámara</div>` : '';

    tooltip.innerHTML = `
        <div class="gv-tooltip-header" style="color: ${titleColor};">📌 Configurar GPIO ${gpioNum}</div>
        ${warningMsg}
        <div class="gv-tooltip-options">
            <button onclick="assignPinTo(${gpioNum}, 'pin-tof-sda')">ToF SDA</button>
            <button onclick="assignPinTo(${gpioNum}, 'pin-tof-scl')">ToF SCL</button>
            <button onclick="assignPinTo(${gpioNum}, 'pin-enc-sda')">Encoder SDA</button>
            <button onclick="assignPinTo(${gpioNum}, 'pin-enc-scl')">Encoder SCL</button>
            <button onclick="assignPinTo(${gpioNum}, 'pin-hx-dt')">HX711 DT</button>
            <button onclick="assignPinTo(${gpioNum}, 'pin-hx-sck')">HX711 SCK</button>
            <button class="btn-danger" style="margin-top:4px;" onclick="assignPinTo(${gpioNum}, 'release')">Liberar Pin</button>
        </div>
    `;

    document.body.appendChild(tooltip);
    currentGvTooltip = tooltip;

    // Close tooltip on click outside
    setTimeout(() => {
        const closeHandler = (e) => {
            if (tooltip && !tooltip.contains(e.target) && e.target !== targetEl && !targetEl.contains(e.target)) {
                tooltip.remove();
                if (currentGvTooltip === tooltip) currentGvTooltip = null;
                document.removeEventListener('click', closeHandler);
            }
        };
        document.addEventListener('click', closeHandler);
    }, 10);
}

function assignPinTo(gpioNum, targetSelectId) {
    if (currentGvTooltip) {
        currentGvTooltip.remove();
        currentGvTooltip = null;
    }

    if (targetSelectId === 'release') {
        const selectors = ['pin-tof-sda', 'pin-tof-scl', 'pin-enc-sda', 'pin-enc-scl', 'pin-hx-dt', 'pin-hx-sck'];
        let freed = false;
        selectors.forEach(id => {
            const el = $(id);
            if (el && parseInt(el.value) === gpioNum) {
                const currentlyAssigned = selectors.map(s => parseInt($(s)?.value || -1));
                const freePin = SAFE_PINS.find(p => !currentlyAssigned.includes(p));
                if (freePin !== undefined) {
                    el.value = freePin;
                    freed = true;
                    showNotification(`📌 GPIO ${gpioNum} liberado. Sensor reasignado a GPIO ${freePin}.`, '#eab308');
                }
            }
        });
        if (!freed) {
            showNotification(`ℹ️ GPIO ${gpioNum} no estaba asignado a ningún sensor.`, '#94a3b8');
        }
        return;
    }

    const selectEl = $(targetSelectId);
    if (!selectEl) return;

    const isCamera = activeHardwareProfile.camera_detected;
    const cameraType = activeHardwareProfile.type;
    const freenoveCamPins = [4, 5, 6, 7, 11, 12, 13, 14, 15, 16, 17, 18, 39, 40, 41, 42];
    const standardCamPins = [17, 18];
    const blockedPins = isCamera ? (cameraType === "freenove_cam" ? freenoveCamPins : standardCamPins) : [];
    
    if (blockedPins.includes(gpioNum)) {
        if (!confirm(`⚠️ Advertencia de Conflicto de Cámara:\n\nEl GPIO ${gpioNum} está reservado para la cámara.\nSi continúas asignándolo a este sensor, podrías provocar inestabilidad.\n\n¿Estás seguro de que deseas asignarlo?`)) {
            return;
        }
    }

    const profileSelect = $('pin-profile-select');
    if (profileSelect && profileSelect.value !== 'custom') {
        profileSelect.value = 'custom';
        applyPinProfilePreset('custom');
    }

    selectEl.value = gpioNum;

    selectEl.style.transition = 'all 0.3s ease';
    selectEl.style.borderColor = '#eab308';
    selectEl.style.boxShadow = '0 0 10px rgba(234, 179, 8, 0.4)';
    setTimeout(() => {
        selectEl.style.borderColor = 'rgba(56, 189, 248, 0.4)';
        selectEl.style.boxShadow = 'none';
    }, 1500);

    const nameMap = {
        'pin-tof-sda': 'ToF SDA',
        'pin-tof-scl': 'ToF SCL',
        'pin-enc-sda': 'Encoder SDA',
        'pin-enc-scl': 'Encoder SCL',
        'pin-hx-dt': 'HX711 DT',
        'pin-hx-sck': 'HX711 SCK'
    };

    showNotification(`📌 GPIO ${gpioNum} asignado a ${nameMap[targetSelectId]}! Recuerda hacer clic en "💾 Guardar y Reiniciar ESP32" para aplicar los cambios.`, '#eab308');
}

function showCaptivePortalOverlay() {
    let overlay = $('captive-portal-overlay');
    if (overlay) return;

    overlay = document.createElement('div');
    overlay.id = 'captive-portal-overlay';
    overlay.className = 'captive-portal-overlay';
    overlay.style.cssText = `
        position: fixed;
        top: 0;
        left: 0;
        width: 100vw;
        height: 100vh;
        background: rgba(10, 14, 26, 0.95);
        backdrop-filter: blur(20px);
        -webkit-backdrop-filter: blur(20px);
        z-index: 999999;
        display: flex;
        align-items: center;
        justify-content: center;
        padding: 20px;
        box-sizing: border-box;
        font-family: inherit;
        color: #f3f4f6;
    `;

    const card = document.createElement('div');
    card.className = 'glass-card';
    card.style.cssText = `
        max-width: 480px;
        width: 100%;
        padding: 30px;
        border-radius: 20px;
        border: 1px solid rgba(56, 189, 248, 0.4);
        background: rgba(15, 23, 42, 0.6);
        box-shadow: 0 20px 40px rgba(0, 0, 0, 0.6), 0 0 20px rgba(56, 189, 248, 0.15);
        text-align: center;
    `;

    card.innerHTML = `
        <div style="font-size: 3rem; margin-bottom: 15px; animation: pulse 2s infinite;">📶</div>
        <h2 style="margin: 0 0 10px 0; color: #38bdf8; font-size: 1.5rem; font-weight: 800;">Portal Cautivo Detectado</h2>
        <p style="font-size: 0.88rem; color: #94a3b8; line-height: 1.5; margin-bottom: 24px;">
            Estás usando el navegador limitado de tu sistema operativo. Para evitar que el celular te <strong>desconecte automáticamente</strong> y poder descargar tus datos:
        </p>
        <div style="text-align: left; background: rgba(0, 0, 0, 0.2); border-radius: 12px; padding: 16px; margin-bottom: 24px; border: 1px solid rgba(255,255,255,0.05); font-size: 0.85rem; line-height: 1.6;">
            <div style="display: flex; gap: 10px; margin-bottom: 10px;">
                <span style="background: #38bdf8; color: #0a0e1a; font-weight: bold; border-radius: 50%; width: 20px; height: 20px; display: inline-flex; align-items: center; justify-content: center; flex-shrink: 0;">1</span>
                <span>Pulsa los <strong>tres puntos (⋮)</strong> arriba a la derecha (o <strong>Cancelar</strong> / <strong>Listo</strong> en iPhone).</span>
            </div>
            <div style="display: flex; gap: 10px; margin-bottom: 10px;">
                <span style="background: #38bdf8; color: #0a0e1a; font-weight: bold; border-radius: 50%; width: 20px; height: 20px; display: inline-flex; align-items: center; justify-content: center; flex-shrink: 0;">2</span>
                <span>Selecciona <strong>"Mantener conexión sin internet"</strong> o <strong>"Usar esta red tal como está"</strong>.</span>
            </div>
            <div style="display: flex; gap: 10px;">
                <span style="background: #38bdf8; color: #0a0e1a; font-weight: bold; border-radius: 50%; width: 20px; height: 20px; display: inline-flex; align-items: center; justify-content: center; flex-shrink: 0;">3</span>
                <span>Abre <strong>Chrome o Safari</strong> e ingresa a:<br><strong style="color: #eab308; font-size: 1rem; font-family: monospace; display: block; margin-top: 4px; text-align: center; background: rgba(234, 179, 8, 0.1); padding: 4px 8px; border-radius: 6px; border: 1px solid rgba(234, 179, 8, 0.2);">http://192.168.4.1</strong></span>
            </div>
        </div>
        <button id="btn-cp-continue" class="btn-action btn-gold" style="width: 100%; font-weight: bold; padding: 12px; border-radius: 10px; font-size: 0.9rem;">
            Continuar en Portal Cautivo anyway
        </button>
    `;

    overlay.appendChild(card);
    document.body.appendChild(overlay);

    $('btn-cp-continue').addEventListener('click', () => {
        overlay.style.opacity = '0';
        overlay.style.transition = 'opacity 0.3s ease-out';
        setTimeout(() => overlay.remove(), 300);
    });
}

