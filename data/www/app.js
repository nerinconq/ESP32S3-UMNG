/* ═══════════════════════════════════════════════════════
   Physys Lab v6.1 — App Controller
   WebSocket Streaming + Canvas Chart + Tab Management
   ═══════════════════════════════════════════════════════ */

// ─── Tab Definitions ───
const TAB_CONFIG = {
    movimiento: {
        title: 'Cinemática Lineal',
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

// ─── State ───
let currentTab = 'movimiento';
let selectedVariable = 'dist';
let ws = null;
let isRecording = false;
let lowPowerMode = false;
let recordedData = [];
let chartData = [];
const MAX_CHART_POINTS = 200;
let lastData = {};

// ─── DOM Elements ───
const $ = id => document.getElementById(id);
const $$ = sel => document.querySelectorAll(sel);

// ─── WebSocket Connection ───
function connectWS() {
    const host = location.hostname || '192.168.4.1';
    ws = new WebSocket('ws://' + host + '/ws');

    ws.onopen = () => {
        $('sys-status').textContent = 'Conectado';
        $('sys-status').style.color = '#14f0c5';
        updateSensorBadges({ tof: true, encoder: true, loadcell: true });
    };

    ws.onclose = () => {
        $('sys-status').textContent = 'Desconectado — Reconectando...';
        $('sys-status').style.color = '#ff4d6a';
        setTimeout(connectWS, 2000);
    };

    ws.onerror = () => {
        ws.close();
    };

    ws.onmessage = (evt) => {
        try {
            const data = JSON.parse(evt.data);
            lastData = data;

            if (isRecording) {
                recordedData.push({ ...data, recorded: Date.now() });
            }

            updateDisplay(data);
            pushChartPoint(data);
            drawChart();
        } catch (e) {
            console.warn('[WS] Parse error', e);
        }
    };
}

// ─── Display Update ───
function updateDisplay(data) {
    const tabConf = TAB_CONFIG[currentTab];
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
    chartData.push({ t: data.t || Date.now(), v: val });
    if (chartData.length > MAX_CHART_POINTS) chartData.shift();
}

function drawChart() {
    const canvas = $('realtimeChart');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const w = canvas.parentElement.clientWidth;
    const h = canvas.parentElement.clientHeight;
    canvas.width = w * (window.devicePixelRatio || 1);
    canvas.height = h * (window.devicePixelRatio || 1);
    ctx.scale(window.devicePixelRatio || 1, window.devicePixelRatio || 1);

    const pts = chartData;
    if (pts.length < 2) {
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#4a5568';
        ctx.font = '13px system-ui';
        ctx.textAlign = 'center';
        ctx.fillText('Esperando datos...', w / 2, h / 2);
        return;
    }

    // Find value range
    let minV = Infinity, maxV = -Infinity;
    for (const p of pts) {
        if (p.v < minV) minV = p.v;
        if (p.v > maxV) maxV = p.v;
    }
    const range = maxV - minV || 1;
    const padY = range * 0.15;
    minV -= padY;
    maxV += padY;

    const padL = 50, padR = 10, padT = 10, padB = 28;
    const plotW = w - padL - padR;
    const plotH = h - padT - padB;

    ctx.clearRect(0, 0, w, h);

    // Grid lines & labels
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineWidth = 0.5;
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

    // Get color from current variable config
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
    const lastX = padL + plotW;
    const grad = ctx.createLinearGradient(0, padT, 0, padT + plotH);
    grad.addColorStop(0, lineColor.replace(')', ', 0.15)').replace('rgb', 'rgba').replace('#', ''));
    // Fallback: create rgba from hex
    const r = parseInt(lineColor.slice(1, 3), 16) || 0;
    const g = parseInt(lineColor.slice(3, 5), 16) || 0;
    const b = parseInt(lineColor.slice(5, 7), 16) || 0;
    const gradFill = ctx.createLinearGradient(0, padT, 0, padT + plotH);
    gradFill.addColorStop(0, `rgba(${r},${g},${b},0.18)`);
    gradFill.addColorStop(1, `rgba(${r},${g},${b},0.0)`);
    ctx.lineTo(lastX, padT + plotH);
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
        ctx.beginPath();
        ctx.arc(lx, ly, 8, 0, Math.PI * 2);
        ctx.strokeStyle = lineColor;
        ctx.lineWidth = 1;
        ctx.globalAlpha = 0.4;
        ctx.stroke();
        ctx.globalAlpha = 1;
    }

    // X Axis label
    ctx.fillStyle = '#4a5568';
    ctx.font = '10px system-ui';
    ctx.textAlign = 'center';
    ctx.fillText('Tiempo →', w / 2, h - 4);
}

// ─── Tab Switching ───
function switchTab(tabName) {
    currentTab = tabName;
    chartData = [];

    // Update active tab
    $$('.tab-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.tab === tabName);
    });

    const tabConf = TAB_CONFIG[tabName];
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

    // Update display with last data if available
    if (Object.keys(lastData).length > 0) {
        updateDisplay(lastData);
    }
}

// ─── Recording ───
function toggleRecording() {
    isRecording = !isRecording;
    const btn = $('btn-record');

    if (isRecording) {
        recordedData = [];
        btn.classList.add('recording');
        btn.innerHTML = '<span class="dot"></span> Grabando...';
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('START');
        }
    } else {
        btn.classList.remove('recording');
        btn.innerHTML = '<span class="dot"></span> Grabar';
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send('STOP');
        }
    }
}

// ─── Export JSON (bitácora-UMNG compatible) ───
function exportJSON() {
    const exportData = {
        device: 'Physys-Lab',
        version: 'v6.1',
        exported: new Date().toISOString(),
        duration_ms: recordedData.length > 1
            ? recordedData[recordedData.length - 1].t - recordedData[0].t
            : 0,
        samples: recordedData.length,
        data: recordedData.map(d => ({
            t: d.t,
            // Cinemática lineal
            dist: d.dist,
            vel: d.vel,
            acc: d.acc,
            // Cinemática angular
            angleDeg: d.angleDeg,
            angleRad: d.angleRad,
            angVel: d.angVel,
            angAcc: d.angAcc,
            // Dinámica
            weight: d.weight,
            mass: d.mass,
            weightN: d.weightN
        }))
    };

    const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'physys_' + Date.now() + '.json';
    a.click();
    URL.revokeObjectURL(url);

    // Also save to ESP32 data partition
    if (recordedData.length > 0) {
        fetch('/api/data', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(exportData)
        }).then(res => res.json())
          .then(r => console.log('[Export] Guardado en ESP32:', r.file))
          .catch(e => console.warn('[Export] No se pudo guardar en ESP32:', e));
    }

    // Save to IndexedDB for Firebase cloud sync (H5)
    if (typeof saveToIndexedDB === 'function') {
        saveToIndexedDB(exportData)
            .then(id => console.log('[Sync] Cached for cloud sync:', id))
            .catch(e => console.warn('[Sync] IndexedDB error:', e));
    }
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

function renderGpioPins(pins) {
    const leftCol = $('gv-left-pins');
    const rightCol = $('gv-right-pins');
    if (!leftCol || !rightCol) return;

    // Split: GPIO 0-21 = left, 35-48 = right
    const leftPins = pins.filter(p => p.g <= 21);
    const rightPins = pins.filter(p => p.g >= 35);

    leftCol.innerHTML = leftPins.map(p =>
        `<div class="gv-pin fn-${p.f}">
            <span class="gv-pin-state ${p.v ? 'high' : 'low'}"></span>
            <span class="gv-pin-num">${p.g}</span>
            <span class="gv-pin-label">${p.l}</span>
        </div>`
    ).join('');

    rightCol.innerHTML = rightPins.map(p =>
        `<div class="gv-pin fn-${p.f}">
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

// ─── Tab Switch Extended (Python + GPIO) ───
const origSwitchTab = switchTab;
switchTab = function(tabName) {
    const pythonPanel = $('python-panel');
    const gpioPanel = $('gpio-viewer-panel');
    const mainLayout = document.querySelector('.main-layout');

    // Hide all special panels
    mainLayout.style.display = 'none';
    pythonPanel.style.display = 'none';
    gpioPanel.style.display = 'none';
    stopGpioPolling();

    if (tabName === 'python') {
        pythonPanel.style.display = 'block';
        $$('.tab-btn').forEach(btn => btn.classList.toggle('active', btn.dataset.tab === tabName));
        updateLineNumbers();
    } else if (tabName === 'gpioview') {
        gpioPanel.style.display = 'block';
        $$('.tab-btn').forEach(btn => btn.classList.toggle('active', btn.dataset.tab === tabName));
        startGpioPolling();
    } else {
        mainLayout.style.display = '';
        origSwitchTab(tabName);
    }
};

// ─── Init ───
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

// ─── Init ───
document.addEventListener('DOMContentLoaded', () => {
    // Tab buttons
    $$('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });

    // Variable selector
    $('graph-variable').addEventListener('change', (e) => {
        selectedVariable = e.target.value;
        chartData = [];
    });

    // Action buttons
    $('btn-record').addEventListener('click', toggleRecording);
    $('btn-export').addEventListener('click', exportJSON);
    $('btn-save').addEventListener('click', toggleLowPower);

    // Python editor
    $('example-select').addEventListener('change', (e) => {
        if (e.target.value) loadExampleScript(e.target.value);
    });
    $('btn-load-script').addEventListener('click', loadCurrentScript);
    $('btn-save-script').addEventListener('click', saveScript);
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

    // GPIO Viewer rate change
    $('gv-rate').addEventListener('change', () => {
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
            document.querySelector('.title-block h1').innerHTML =
                (cfg.lab_name || 'Physys Lab') +
                ' <span class="v-tag">' + (cfg.version || 'v6.1') + '</span>';
            if (cfg.institution) {
                $('sys-status').textContent = cfg.institution_short || cfg.institution;
            }
        })
        .catch(() => {});

    // Init Firebase Sync module (H5)
    if (typeof initFirebaseSync === 'function') {
        initFirebaseSync();
    }

    // Redraw chart on resize
    window.addEventListener('resize', drawChart);
});
