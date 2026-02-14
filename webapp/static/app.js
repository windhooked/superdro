// SuperDRO Web Companion — WebSocket client + DRO logic

// --- State (mirrors Android DroViewModel) ---
const state = {
    xPosMm: 0,
    zPosMm: 0,
    rpm: 0,
    machineState: 'idle',
    feedHold: false,
    unitMode: 'metric',       // 'metric' | 'imperial'
    xDisplayMode: 'diameter', // 'diameter' | 'radius'
    wsConnected: false,
    serialConnected: false,
    configValues: {},
    configDirty: new Set(),
};

// --- WebSocket ---
let ws = null;
let reconnectTimer = null;

function connect() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${location.host}/ws`);

    ws.onopen = () => {
        state.wsConnected = true;
        updateConnectionUI();
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
    };

    ws.onclose = () => {
        state.wsConnected = false;
        state.serialConnected = false;
        updateConnectionUI();
        ws = null;
        reconnectTimer = setTimeout(connect, 2000);
    };

    ws.onerror = () => {
        // onclose will fire after this
    };

    ws.onmessage = (event) => {
        try {
            const msg = JSON.parse(event.data);
            if (msg.pos !== undefined) {
                handleStatus(msg);
            } else if (msg.ack !== undefined) {
                handleAck(msg);
            } else if (msg.meta !== undefined) {
                handleMeta(msg);
            }
        } catch (e) {
            // Ignore malformed messages
        }
    };
}

function sendCommand(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(cmd));
    }
}

// --- Message handlers ---

function handleStatus(msg) {
    state.xPosMm = msg.pos.x;
    state.zPosMm = msg.pos.z;
    state.rpm = msg.rpm;
    state.machineState = msg.state;
    state.feedHold = msg.fh;
    updateDRO();
}

function handleAck(msg) {
    if (msg.ack === 'config_list' && msg.ok && msg.params) {
        state.configValues = msg.params;
        state.configDirty.clear();
        renderConfig();
    } else if (msg.ack === 'config_get' && msg.ok) {
        state.configValues[msg.key] = msg.value;
        renderConfig();
    }
    // Could show toast feedback for zero/preset ACKs
}

function handleMeta(msg) {
    if (msg.meta === 'connected') {
        state.serialConnected = msg.serial;
        updateConnectionUI();
    }
}

// --- Unit conversion (replicates DroViewModel.kt) ---

function displayX(rawMm) {
    const scaled = state.xDisplayMode === 'diameter' ? rawMm * 2 : rawMm;
    return state.unitMode === 'imperial' ? scaled / 25.4 : scaled;
}

function displayZ(rawMm) {
    return state.unitMode === 'imperial' ? rawMm / 25.4 : rawMm;
}

function formatValue(value) {
    const decimals = state.unitMode === 'imperial' ? 4 : 3;
    const sign = value >= 0 ? '+' : '';
    return sign + value.toFixed(decimals);
}

function unitLabel() {
    return state.unitMode === 'metric' ? 'mm' : 'in';
}

// --- DRO Update ---

function updateDRO() {
    document.getElementById('x-value').textContent = formatValue(displayX(state.xPosMm));
    document.getElementById('z-value').textContent = formatValue(displayZ(state.zPosMm));
    document.getElementById('rpm-value').textContent = Math.round(state.rpm).toString();

    // Units
    const unit = unitLabel();
    document.getElementById('x-unit').textContent = unit;
    document.getElementById('z-unit').textContent = unit;

    // X sublabel
    document.getElementById('x-sublabel').textContent =
        state.xDisplayMode === 'diameter' ? 'DIA' : 'RAD';

    // State indicator
    const stateEl = document.getElementById('state-indicator');
    stateEl.textContent = state.machineState.toUpperCase();
    if (state.machineState === 'alarm') {
        stateEl.className = 'panel-state alarm-text';
    } else {
        stateEl.className = 'panel-state';
    }

    // Alarm banner
    const alarm = document.getElementById('alarm-banner');
    if (state.machineState === 'alarm') {
        alarm.classList.remove('hidden');
    } else {
        alarm.classList.add('hidden');
    }
}

function updateConnectionUI() {
    const dot = document.getElementById('status-dot');
    const text = document.getElementById('status-text');

    if (!state.wsConnected) {
        dot.className = 'dot dot-red';
        text.textContent = 'Disconnected';
    } else if (!state.serialConnected) {
        dot.className = 'dot dot-yellow';
        text.textContent = 'No Serial';
    } else {
        dot.className = 'dot dot-green';
        text.textContent = 'Connected';
    }
}

// --- Commands ---

function zeroAxis(axis) {
    sendCommand({ cmd: 'zero', axis: axis });
}

let presetAxis = null;

function showPreset(axis) {
    presetAxis = axis;
    const label = axis.toUpperCase();
    document.getElementById('preset-title').textContent = `Set ${label} Position`;
    document.getElementById('preset-input').value = '';
    document.getElementById('preset-overlay').classList.remove('hidden');
    document.getElementById('preset-input').focus();
}

function cancelPreset() {
    presetAxis = null;
    document.getElementById('preset-overlay').classList.add('hidden');
}

function confirmPreset() {
    const input = document.getElementById('preset-input');
    const displayValue = parseFloat(input.value);
    if (isNaN(displayValue) || presetAxis === null) {
        cancelPreset();
        return;
    }

    // Convert display value back to mm (reverse of display conversion)
    let mmValue = state.unitMode === 'imperial' ? displayValue * 25.4 : displayValue;
    if (presetAxis === 'x' && state.xDisplayMode === 'diameter') {
        mmValue /= 2;
    }

    sendCommand({ cmd: 'preset', axis: presetAxis, value: mmValue });
    cancelPreset();
}

// --- Toggles ---

function toggleUnit() {
    state.unitMode = state.unitMode === 'metric' ? 'imperial' : 'metric';
    document.getElementById('unit-toggle').textContent =
        state.unitMode === 'metric' ? 'mm' : 'in';
    updateDRO();
}

function toggleXMode() {
    state.xDisplayMode = state.xDisplayMode === 'diameter' ? 'radius' : 'diameter';
    document.getElementById('xmode-toggle').textContent =
        state.xDisplayMode === 'diameter' ? 'DIA' : 'RAD';
    updateDRO();
}

// --- Tab switching ---

function switchTab(tab) {
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.tab === tab);
    });
    document.querySelectorAll('.tab-content').forEach(el => {
        el.classList.toggle('active', el.id === 'tab-' + tab);
    });

    // Auto-load config when switching to config tab
    if (tab === 'config' && Object.keys(state.configValues).length === 0) {
        loadConfig();
    }
}

// --- Config page ---

const CONFIG_PARAMS = [
    { key: 'spindle_ppr', label: 'Encoder PPR', category: 'Spindle' },
    { key: 'spindle_quadrature', label: 'Quadrature multiplier', category: 'Spindle' },
    { key: 'spindle_counts_per_rev', label: 'Counts/rev', category: 'Spindle', readOnly: true },
    { key: 'spindle_max_rpm', label: 'Max RPM', category: 'Spindle' },
    { key: 'z_scale_resolution_mm', label: 'Scale resolution (mm)', category: 'Z Axis' },
    { key: 'z_leadscrew_pitch_mm', label: 'Leadscrew pitch (mm)', category: 'Z Axis' },
    { key: 'z_steps_per_rev', label: 'Steps/rev', category: 'Z Axis' },
    { key: 'z_belt_ratio', label: 'Belt ratio', category: 'Z Axis' },
    { key: 'z_steps_per_mm', label: 'Steps/mm', category: 'Z Axis', readOnly: true },
    { key: 'z_max_speed_mm_s', label: 'Max speed (mm/s)', category: 'Z Axis' },
    { key: 'z_accel_mm_s2', label: 'Acceleration (mm/s\u00B2)', category: 'Z Axis' },
    { key: 'z_backlash_mm', label: 'Backlash (mm)', category: 'Z Axis' },
    { key: 'x_scale_resolution_mm', label: 'Scale resolution (mm)', category: 'X Axis' },
    { key: 'x_is_diameter', label: 'Diameter mode', category: 'X Axis' },
];

function loadConfig() {
    sendCommand({ cmd: 'config_list' });
}

function saveConfig() {
    // Send config_set for each dirty key, then config_save
    for (const key of state.configDirty) {
        const val = state.configValues[key];
        sendCommand({ cmd: 'config_set', key: key, value: String(val) });
    }
    state.configDirty.clear();
    sendCommand({ cmd: 'config_save' });
    renderConfig();
}

function renderConfig() {
    const container = document.getElementById('config-categories');
    container.innerHTML = '';

    // Group by category
    const categories = {};
    for (const param of CONFIG_PARAMS) {
        if (!categories[param.category]) {
            categories[param.category] = [];
        }
        categories[param.category].push(param);
    }

    for (const [cat, params] of Object.entries(categories)) {
        const section = document.createElement('div');
        section.className = 'config-section';

        const heading = document.createElement('h3');
        heading.textContent = cat;
        section.appendChild(heading);

        for (const param of params) {
            const row = document.createElement('div');
            row.className = 'config-row';

            const label = document.createElement('span');
            label.className = 'config-label';
            label.textContent = param.label;

            const input = document.createElement('input');
            input.className = 'config-input';
            if (param.readOnly) input.className += ' readonly';
            if (state.configDirty.has(param.key)) input.className += ' dirty';

            const val = state.configValues[param.key];
            input.value = val !== undefined ? val : '';
            input.readOnly = !!param.readOnly;

            if (!param.readOnly) {
                input.addEventListener('change', () => {
                    state.configValues[param.key] = input.value;
                    state.configDirty.add(param.key);
                    input.classList.add('dirty');
                });
            }

            row.appendChild(label);
            row.appendChild(input);
            section.appendChild(row);
        }

        container.appendChild(section);
    }
}

// --- Init ---
connect();
updateDRO();
updateConnectionUI();
