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
    simMode: false,
    serialPort: '',
    configValues: {},
    configDirty: new Set(),
    savedPositions: [],       // [{label, x, z, unit, xMode}]
    toolTable: [],            // [{number, label, xOffset, zOffset}]
    activeTool: null,         // tool number or null
    activeAxis: null,         // 'x' | 'z' | null (for dynamic zoom)
    prevXPosMm: 0,
    prevZPosMm: 0,
    // ELS state
    elsThreadType: 'metric',    // 'metric' | 'imperial'
    elsGrade: 'coarse',         // 'coarse' | 'fine'
    elsPitch: null,             // mm (always stored as mm internally)
    elsDir: 'rh',               // 'rh' | 'lh'
    elsRetract: 'rapid',        // 'rapid' | 'spring'
    elsEngaged: false,
    elsPass: 0,
    elsState: 'DISENGAGED',     // 'DISENGAGED' | 'ENGAGED' | 'HOLDING' | 'ALARM'
};

let activeAxisTimer = null;
const MOVE_THRESHOLD = 0.001;  // mm — minimum delta to detect movement
const ZOOM_TIMEOUT = 500;      // ms — clear active axis after movement stops

// --- Thread tables (client-side, per PRD Phase 2) ---
const THREAD_TABLES = {
    metric: {
        coarse: [
            { label: 'M3',  pitch: 0.5 },
            { label: 'M4',  pitch: 0.7 },
            { label: 'M5',  pitch: 0.8 },
            { label: 'M6',  pitch: 1.0 },
            { label: 'M8',  pitch: 1.25 },
            { label: 'M10', pitch: 1.5 },
            { label: 'M12', pitch: 1.75 },
            { label: 'M14', pitch: 2.0 },
            { label: 'M16', pitch: 2.0 },
            { label: 'M18', pitch: 2.5 },
            { label: 'M20', pitch: 2.5 },
            { label: 'M24', pitch: 3.0 },
            { label: 'M30', pitch: 3.5 },
            { label: 'M36', pitch: 4.0 },
        ],
        fine: [
            { label: 'M8',  pitch: 1.0 },
            { label: 'M10', pitch: 1.25 },
            { label: 'M12', pitch: 1.25 },
            { label: 'M12', pitch: 1.5 },
            { label: 'M14', pitch: 1.5 },
            { label: 'M16', pitch: 1.5 },
            { label: 'M20', pitch: 1.5 },
            { label: 'M24', pitch: 2.0 },
            { label: 'M30', pitch: 2.0 },
        ],
    },
    imperial: {
        coarse: [
            { label: '1/4-20',  tpi: 20 },
            { label: '5/16-18', tpi: 18 },
            { label: '3/8-16',  tpi: 16 },
            { label: '7/16-14', tpi: 14 },
            { label: '1/2-13',  tpi: 13 },
            { label: '9/16-12', tpi: 12 },
            { label: '5/8-11',  tpi: 11 },
            { label: '3/4-10',  tpi: 10 },
            { label: '7/8-9',   tpi: 9 },
            { label: '1-8',     tpi: 8 },
        ],
        fine: [
            { label: '1/4-28',  tpi: 28 },
            { label: '5/16-24', tpi: 24 },
            { label: '3/8-24',  tpi: 24 },
            { label: '7/16-20', tpi: 20 },
            { label: '1/2-20',  tpi: 20 },
            { label: '9/16-18', tpi: 18 },
            { label: '5/8-18',  tpi: 18 },
            { label: '3/4-16',  tpi: 16 },
            { label: '7/8-14',  tpi: 14 },
            { label: '1-12',    tpi: 12 },
        ],
    },
};

function tpiToMmPitch(tpi) {
    return 25.4 / tpi;
}

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

    // ELS fields (present when state === 'threading')
    if (msg.pitch !== undefined) {
        state.elsPitch = msg.pitch;
        state.elsDir = msg.dir || 'rh';
        state.elsPass = msg.pass || 0;
        state.elsRetract = msg.retract || 'rapid';
    }

    // Derive ELS state from machine state
    if (state.machineState === 'threading') {
        state.elsEngaged = true;
        state.elsState = state.feedHold ? 'HOLDING' : 'ENGAGED';
    } else if (state.machineState === 'alarm') {
        state.elsState = 'ALARM';
        state.elsEngaged = false;
    } else {
        state.elsEngaged = false;
        state.elsState = 'DISENGAGED';
    }

    // Dynamic zoom: detect which axis is moving
    const xDelta = Math.abs(state.xPosMm - state.prevXPosMm);
    const zDelta = Math.abs(state.zPosMm - state.prevZPosMm);

    if (xDelta > MOVE_THRESHOLD || zDelta > MOVE_THRESHOLD) {
        const newActive = xDelta >= zDelta ? 'x' : 'z';
        setActiveAxis(newActive);
    }

    state.prevXPosMm = state.xPosMm;
    state.prevZPosMm = state.zPosMm;

    updateDRO();
    updateELSDisplay();
}

function setActiveAxis(axis) {
    if (state.activeAxis !== axis) {
        state.activeAxis = axis;
        document.querySelectorAll('.axis-row').forEach(row => {
            row.classList.toggle('active', row.dataset.axis === axis);
        });
    }
    // Reset timeout — clear active state after movement stops
    if (activeAxisTimer) clearTimeout(activeAxisTimer);
    activeAxisTimer = setTimeout(() => {
        state.activeAxis = null;
        document.querySelectorAll('.axis-row').forEach(row => {
            row.classList.remove('active');
        });
        activeAxisTimer = null;
    }, ZOOM_TIMEOUT);
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
}

function handleMeta(msg) {
    if (msg.meta === 'connected') {
        state.serialConnected = msg.serial;
        state.simMode = !!msg.sim;
        state.serialPort = msg.port || '';
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
        stateEl.className = 'state-badge alarm-state';
    } else {
        stateEl.className = 'state-badge';
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
    const badge = document.getElementById('mode-badge');

    if (!state.wsConnected) {
        dot.className = 'dot dot-red';
        text.textContent = 'Disconnected';
    } else if (!state.serialConnected) {
        dot.className = 'dot dot-yellow';
        text.textContent = 'No Serial';
    } else if (state.simMode) {
        dot.className = 'dot dot-green';
        text.textContent = 'Simulated';
    } else {
        dot.className = 'dot dot-green';
        text.textContent = state.serialPort || 'Connected';
    }

    // Mode badge
    if (badge) {
        if (!state.wsConnected || !state.serialConnected) {
            badge.className = 'mode-badge hidden';
        } else if (state.simMode) {
            badge.className = 'mode-badge sim';
            badge.textContent = 'SIM';
        } else {
            badge.className = 'mode-badge live';
            badge.textContent = 'LIVE';
        }
    }
}

// --- Commands ---

function zeroAxis(axis) {
    sendCommand({ cmd: 'zero', axis: axis });
}

function zeroAll() {
    sendCommand({ cmd: 'zero', axis: 'x' });
    sendCommand({ cmd: 'zero', axis: 'z' });
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
    // Update left toolbar buttons
    document.querySelectorAll('.tb-btn[data-tab]').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.tab === tab);
    });
    // Update tab content
    document.querySelectorAll('.tab-content').forEach(el => {
        el.classList.toggle('active', el.id === 'tab-' + tab);
    });

    // Auto-load config when switching to config tab
    if (tab === 'config' && Object.keys(state.configValues).length === 0) {
        loadConfig();
    }
    // Render tool table when switching to tool tab
    if (tab === 'tool') {
        renderToolTable();
    }
    // Render ELS tab on switch
    if (tab === 'els') {
        renderElsTab();
    }
}

// --- Config page ---

const CONFIG_PARAMS = [
    // Spindle
    { key: 'spindle_ppr', label: 'Encoder PPR', category: 'Spindle', type: 'number', min: 1, max: 100000 },
    { key: 'spindle_quadrature', label: 'Quadrature multiplier', category: 'Spindle', type: 'number', min: 1, max: 4 },
    { key: 'spindle_counts_per_rev', label: 'Counts/rev', category: 'Spindle', readOnly: true, type: 'number' },
    { key: 'spindle_max_rpm', label: 'Max RPM', category: 'Spindle', type: 'number', min: 1, max: 50000 },
    // Z Axis DRO
    { key: 'z_scale_resolution_mm', label: 'Scale resolution (mm)', category: 'Z Axis DRO', type: 'number', min: 0.0001, max: 1.0 },
    { key: 'z_travel_min_mm', label: 'Travel min (mm)', category: 'Z Axis DRO', type: 'number' },
    { key: 'z_travel_max_mm', label: 'Travel max (mm)', category: 'Z Axis DRO', type: 'number' },
    // Z Axis ELS
    { key: 'z_leadscrew_pitch_mm', label: 'Leadscrew pitch (mm)', category: 'Z Axis ELS', type: 'number', min: 0.1, max: 100 },
    { key: 'z_steps_per_rev', label: 'Steps/rev', category: 'Z Axis ELS', type: 'number', min: 1, max: 100000 },
    { key: 'z_belt_ratio', label: 'Belt ratio', category: 'Z Axis ELS', type: 'number', min: 0.01, max: 100 },
    { key: 'z_steps_per_mm', label: 'Steps/mm', category: 'Z Axis ELS', readOnly: true, type: 'number' },
    { key: 'z_max_speed_mm_s', label: 'Max speed (mm/s)', category: 'Z Axis ELS', type: 'number', min: 0.1, max: 1000 },
    { key: 'z_accel_mm_s2', label: 'Acceleration (mm/s\u00B2)', category: 'Z Axis ELS', type: 'number', min: 1, max: 10000 },
    { key: 'z_backlash_mm', label: 'Backlash (mm)', category: 'Z Axis ELS', type: 'number', min: 0, max: 10 },
    // X Axis DRO
    { key: 'x_scale_resolution_mm', label: 'Scale resolution (mm)', category: 'X Axis DRO', type: 'number', min: 0.0001, max: 1.0 },
    { key: 'x_is_diameter', label: 'Diameter mode', category: 'X Axis DRO', type: 'boolean' },
    { key: 'x_travel_min_mm', label: 'Travel min (mm)', category: 'X Axis DRO', type: 'number' },
    { key: 'x_travel_max_mm', label: 'Travel max (mm)', category: 'X Axis DRO', type: 'number' },
    // X Axis ELS
    { key: 'x_steps_per_rev', label: 'Steps/rev', category: 'X Axis ELS', type: 'number', min: 1, max: 100000 },
    { key: 'x_leadscrew_pitch_mm', label: 'Leadscrew pitch (mm)', category: 'X Axis ELS', type: 'number', min: 0.1, max: 100 },
    { key: 'x_belt_ratio', label: 'Belt ratio', category: 'X Axis ELS', type: 'number', min: 0.01, max: 100 },
    { key: 'x_steps_per_mm', label: 'Steps/mm', category: 'X Axis ELS', readOnly: true, type: 'number' },
    // Threading
    { key: 'thread_retract_mode', label: 'Retract mode', category: 'Threading', type: 'number', min: 0, max: 1 },
    { key: 'thread_retract_x_mm', label: 'Retract X (mm)', category: 'Threading', type: 'number', min: 0, max: 50 },
    { key: 'thread_compound_angle', label: 'Compound angle (\u00B0)', category: 'Threading', type: 'number', min: 0, max: 90 },
];

function loadConfig() {
    sendCommand({ cmd: 'config_list' });
}

function validateConfigValue(param, rawValue) {
    if (param.type === 'boolean') {
        const s = String(rawValue).toLowerCase().trim();
        return s === 'true' || s === 'false' || s === '1' || s === '0';
    }
    if (param.type === 'number') {
        const num = Number(rawValue);
        if (isNaN(num)) return false;
        if (param.min !== undefined && num < param.min) return false;
        if (param.max !== undefined && num > param.max) return false;
        return true;
    }
    return true;
}

function getValidationMessage(param) {
    if (param.type === 'number') {
        if (param.min !== undefined && param.max !== undefined) {
            return `${param.min} \u2013 ${param.max}`;
        }
        if (param.min !== undefined) return `\u2265 ${param.min}`;
        return 'Must be a number';
    }
    return '';
}

function saveConfig() {
    for (const key of state.configDirty) {
        const param = CONFIG_PARAMS.find(p => p.key === key);
        const val = state.configValues[key];
        if (param && !validateConfigValue(param, val)) continue;
        sendCommand({ cmd: 'config_set', key: key, value: String(val) });
    }
    state.configDirty.clear();
    sendCommand({ cmd: 'config_save' });
    renderConfig();
}

function renderConfig() {
    const container = document.getElementById('config-categories');
    container.innerHTML = '';

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

            const val = state.configValues[param.key];

            if (param.type === 'boolean' && !param.readOnly) {
                const toggle = document.createElement('button');
                const isOn = val === true || val === 'true' || val === 1 || val === '1';
                toggle.className = 'config-toggle' + (isOn ? ' on' : '');
                if (state.configDirty.has(param.key)) toggle.className += ' dirty';
                toggle.textContent = isOn ? 'ON' : 'OFF';
                toggle.onclick = () => {
                    state.configValues[param.key] = !isOn;
                    state.configDirty.add(param.key);
                    renderConfig();
                };
                row.appendChild(label);
                row.appendChild(toggle);
            } else {
                const wrapper = document.createElement('div');
                wrapper.className = 'config-input-wrapper';

                const input = document.createElement('input');
                input.className = 'config-input';
                if (param.readOnly) input.className += ' readonly';
                if (state.configDirty.has(param.key)) input.className += ' dirty';

                input.value = val !== undefined ? val : '';
                input.readOnly = !!param.readOnly;

                if (param.type === 'number' && !param.readOnly) {
                    input.type = 'number';
                    input.step = 'any';
                    if (param.min !== undefined) input.min = param.min;
                    if (param.max !== undefined) input.max = param.max;
                }

                if (!param.readOnly) {
                    input.addEventListener('change', () => {
                        if (!validateConfigValue(param, input.value)) {
                            input.classList.add('invalid');
                            input.classList.remove('dirty');
                            let tip = wrapper.querySelector('.config-tooltip');
                            if (!tip) {
                                tip = document.createElement('div');
                                tip.className = 'config-tooltip';
                                wrapper.appendChild(tip);
                            }
                            tip.textContent = getValidationMessage(param);
                            tip.classList.add('visible');
                            return;
                        }
                        input.classList.remove('invalid');
                        const tip = wrapper.querySelector('.config-tooltip');
                        if (tip) tip.classList.remove('visible');
                        state.configValues[param.key] = param.type === 'number'
                            ? Number(input.value) : input.value;
                        state.configDirty.add(param.key);
                        input.classList.add('dirty');
                    });
                    input.addEventListener('focus', () => {
                        const tip = wrapper.querySelector('.config-tooltip');
                        if (tip) tip.classList.remove('visible');
                    });
                }

                wrapper.appendChild(input);
                row.appendChild(label);
                row.appendChild(wrapper);
            }

            section.appendChild(row);
        }

        container.appendChild(section);
    }
}

// --- Saved positions (right status bar, localStorage) ---

function loadSavedPositions() {
    try {
        const data = localStorage.getItem('superdro_positions');
        if (data) {
            state.savedPositions = JSON.parse(data);
        }
    } catch (e) { /* ignore corrupt data */ }
}

function saveSavedPositions() {
    localStorage.setItem('superdro_positions', JSON.stringify(state.savedPositions));
}

function saveCurrentPosition() {
    const label = `P${state.savedPositions.length + 1}`;
    state.savedPositions.push({
        label: label,
        x: state.xPosMm,
        z: state.zPosMm,
        unit: state.unitMode,
        xMode: state.xDisplayMode,
    });
    saveSavedPositions();
    renderSavedPositions();
}

function clearAllPositions() {
    state.savedPositions = [];
    saveSavedPositions();
    renderSavedPositions();
}

function deletePosition(index) {
    state.savedPositions.splice(index, 1);
    saveSavedPositions();
    renderSavedPositions();
}

function recallPosition(index) {
    const pos = state.savedPositions[index];
    if (!pos) return;
    sendCommand({ cmd: 'preset', axis: 'x', value: pos.x });
    sendCommand({ cmd: 'preset', axis: 'z', value: pos.z });
}

function renderSavedPositions() {
    const container = document.getElementById('saved-positions');
    container.innerHTML = '';

    for (let i = 0; i < state.savedPositions.length; i++) {
        const pos = state.savedPositions[i];
        const xDisplay = formatValue(displayX(pos.x));
        const zDisplay = formatValue(displayZ(pos.z));
        const unit = unitLabel();

        const item = document.createElement('div');
        item.className = 'saved-item';

        const labelEl = document.createElement('div');
        labelEl.className = 'saved-item-label';
        labelEl.textContent = pos.label;

        const coordsEl = document.createElement('div');
        coordsEl.className = 'saved-item-coords';
        coordsEl.textContent = `X ${xDisplay}  Z ${zDisplay} ${unit}`;

        const actionsEl = document.createElement('div');
        actionsEl.className = 'saved-item-actions';

        const recallBtn = document.createElement('button');
        recallBtn.className = 'delete-btn';
        recallBtn.textContent = 'RECALL';
        recallBtn.onclick = () => recallPosition(i);

        const deleteBtn = document.createElement('button');
        deleteBtn.className = 'delete-btn';
        deleteBtn.textContent = '\u00D7';
        deleteBtn.onclick = (e) => { e.stopPropagation(); deletePosition(i); };

        actionsEl.appendChild(recallBtn);
        actionsEl.appendChild(deleteBtn);
        item.appendChild(labelEl);
        item.appendChild(coordsEl);
        item.appendChild(actionsEl);
        container.appendChild(item);
    }
}

// --- Tool offset table (localStorage) ---

function loadToolTable() {
    try {
        const data = localStorage.getItem('superdro_tools');
        if (data) {
            state.toolTable = JSON.parse(data);
        }
        const active = localStorage.getItem('superdro_active_tool');
        if (active) {
            state.activeTool = parseInt(active, 10);
        }
    } catch (e) { /* ignore corrupt data */ }
}

function saveToolTable() {
    localStorage.setItem('superdro_tools', JSON.stringify(state.toolTable));
    if (state.activeTool !== null) {
        localStorage.setItem('superdro_active_tool', String(state.activeTool));
    } else {
        localStorage.removeItem('superdro_active_tool');
    }
}

function addTool() {
    const maxNum = state.toolTable.reduce((m, t) => Math.max(m, t.number), 0);
    state.toolTable.push({ number: maxNum + 1, label: '', xOffset: 0, zOffset: 0 });
    saveToolTable();
    renderToolTable();
}

function deleteTool(index) {
    const tool = state.toolTable[index];
    if (tool && state.activeTool === tool.number) {
        state.activeTool = null;
    }
    state.toolTable.splice(index, 1);
    saveToolTable();
    renderToolTable();
    updateActiveToolDisplay();
}

function activateTool(num) {
    const newTool = state.toolTable.find(t => t.number === num);
    if (!newTool) return;

    const oldTool = state.toolTable.find(t => t.number === state.activeTool);
    const oldX = oldTool ? oldTool.xOffset : 0;
    const oldZ = oldTool ? oldTool.zOffset : 0;
    const deltaX = newTool.xOffset - oldX;
    const deltaZ = newTool.zOffset - oldZ;

    state.activeTool = num;
    saveToolTable();

    // Shift DRO by sending preset commands with offset delta
    if (deltaX !== 0) {
        sendCommand({ cmd: 'preset', axis: 'x', value: state.xPosMm - deltaX });
    }
    if (deltaZ !== 0) {
        sendCommand({ cmd: 'preset', axis: 'z', value: state.zPosMm - deltaZ });
    }

    renderToolTable();
    updateActiveToolDisplay();
}

function touchOffTool(index) {
    const tool = state.toolTable[index];
    if (!tool) return;
    tool.xOffset = state.xPosMm;
    tool.zOffset = state.zPosMm;
    saveToolTable();
    renderToolTable();
}

function updateToolLabel(index, label) {
    if (state.toolTable[index]) {
        state.toolTable[index].label = label;
        saveToolTable();
    }
}

function renderToolTable() {
    const container = document.getElementById('tool-table-body');
    if (!container) return;
    container.innerHTML = '';

    for (let i = 0; i < state.toolTable.length; i++) {
        const tool = state.toolTable[i];
        const isActive = state.activeTool === tool.number;
        const row = document.createElement('tr');
        row.className = isActive ? 'tool-row active' : 'tool-row';

        row.innerHTML = `
            <td class="tool-num">T${tool.number}</td>
            <td><input class="tool-label-input" type="text" value="${tool.label}"
                 placeholder="---" onchange="updateToolLabel(${i}, this.value)"></td>
            <td class="tool-offset">${tool.xOffset.toFixed(3)}</td>
            <td class="tool-offset">${tool.zOffset.toFixed(3)}</td>
            <td class="tool-actions">
                <button class="btn btn-small" onclick="touchOffTool(${i})">TOUCH</button>
                <button class="btn btn-small${isActive ? ' btn-accent' : ''}"
                        onclick="activateTool(${tool.number})">${isActive ? 'ACTIVE' : 'SELECT'}</button>
                <button class="btn btn-small btn-dim" onclick="deleteTool(${i})">\u00D7</button>
            </td>`;
        container.appendChild(row);
    }
}

function updateActiveToolDisplay() {
    const el = document.getElementById('active-tool-display');
    if (!el) return;
    if (state.activeTool !== null) {
        el.textContent = 'T' + state.activeTool;
        el.className = 'active-tool active';
    } else {
        el.textContent = '---';
        el.className = 'active-tool';
    }
}

// --- ELS / Threading tab ---

function switchElsType(type) {
    state.elsThreadType = type;
    state.elsGrade = 'coarse';
    // Update custom pitch unit label
    const unitEl = document.getElementById('els-custom-unit');
    if (unitEl) unitEl.textContent = type === 'imperial' ? 'TPI' : 'mm';
    renderElsTab();
}

function switchElsGrade(grade) {
    state.elsGrade = grade;
    renderElsTab();
}

function selectThreadPitch(pitchMm) {
    state.elsPitch = pitchMm;
    sendCommand({ cmd: 'set_pitch', pitch: pitchMm, dir: state.elsDir });
    renderElsTab();
}

function setCustomPitch() {
    const input = document.getElementById('els-custom-pitch');
    let val = parseFloat(input.value);
    if (isNaN(val) || val <= 0) return;
    if (state.elsThreadType === 'imperial') {
        val = tpiToMmPitch(val);
    }
    state.elsPitch = val;
    sendCommand({ cmd: 'set_pitch', pitch: val, dir: state.elsDir });
    renderElsTab();
}

function setElsDir(dir) {
    state.elsDir = dir;
    if (state.elsPitch !== null) {
        sendCommand({ cmd: 'set_pitch', pitch: state.elsPitch, dir: dir });
    }
    renderElsTab();
}

function setElsRetract(mode) {
    state.elsRetract = mode;
    sendCommand({ cmd: 'set_retract', mode: mode });
    renderElsTab();
}

function elsEngage() {
    if (state.elsPitch === null || state.elsPitch <= 0) return;
    sendCommand({ cmd: 'engage' });
}

function elsDisengage() {
    sendCommand({ cmd: 'disengage' });
}

function elsFeedHold() {
    sendCommand({ cmd: 'feed_hold' });
}

function renderElsTab() {
    const container = document.getElementById('els-pitch-grid');
    if (!container) return;
    container.innerHTML = '';

    const threads = THREAD_TABLES[state.elsThreadType]?.[state.elsGrade] || [];

    for (const thread of threads) {
        const btn = document.createElement('button');
        const pitchMm = thread.pitch !== undefined ? thread.pitch : tpiToMmPitch(thread.tpi);
        const isSelected = state.elsPitch !== null &&
            Math.abs(state.elsPitch - pitchMm) < 0.001;

        btn.className = 'els-pitch-btn' + (isSelected ? ' selected' : '');

        if (state.elsThreadType === 'metric') {
            btn.innerHTML = `<span class="pitch-label">${thread.label}</span>` +
                            `<span class="pitch-value">${thread.pitch} mm</span>`;
        } else {
            btn.innerHTML = `<span class="pitch-label">${thread.label}</span>` +
                            `<span class="pitch-value">${thread.tpi} TPI</span>`;
        }

        btn.onclick = () => selectThreadPitch(pitchMm);
        container.appendChild(btn);
    }

    // Update type tabs
    document.querySelectorAll('.els-type-btn').forEach(b => {
        b.classList.toggle('active', b.dataset.type === state.elsThreadType);
    });

    // Update grade tabs
    document.querySelectorAll('.els-grade-btn').forEach(b => {
        b.classList.toggle('active', b.dataset.grade === state.elsGrade);
    });

    // Update direction buttons
    document.querySelectorAll('.els-dir-btn').forEach(b => {
        b.classList.toggle('active', b.dataset.dir === state.elsDir);
    });

    // Update retract buttons
    document.querySelectorAll('.els-retract-btn').forEach(b => {
        b.classList.toggle('active', b.dataset.mode === state.elsRetract);
    });

    // Update engage/disengage button
    const engageBtn = document.getElementById('els-engage-btn');
    if (engageBtn) {
        if (state.elsEngaged) {
            engageBtn.textContent = 'DISENGAGE';
            engageBtn.className = 'btn els-engage-btn engaged';
            engageBtn.onclick = elsDisengage;
            engageBtn.disabled = false;
        } else {
            engageBtn.textContent = 'ENGAGE';
            engageBtn.className = 'btn els-engage-btn';
            engageBtn.onclick = elsEngage;
            engageBtn.disabled = state.elsPitch === null;
        }
    }
}

function updateELSDisplay() {
    const stateEl = document.getElementById('els-state-indicator');
    if (stateEl) {
        stateEl.textContent = state.elsState;
        stateEl.className = 'els-state-badge els-state-' + state.elsState.toLowerCase();
    }

    const pitchEl = document.getElementById('els-live-pitch');
    if (pitchEl) {
        if (state.elsPitch !== null) {
            if (state.elsThreadType === 'imperial') {
                pitchEl.textContent = (25.4 / state.elsPitch).toFixed(1) + ' TPI';
            } else {
                pitchEl.textContent = state.elsPitch.toFixed(2) + ' mm';
            }
        } else {
            pitchEl.textContent = '---';
        }
    }

    const dirEl = document.getElementById('els-live-dir');
    if (dirEl) dirEl.textContent = state.elsDir.toUpperCase();

    const passEl = document.getElementById('els-live-pass');
    if (passEl) passEl.textContent = state.elsPass > 0 ? String(state.elsPass) : '---';

    const rpmEl = document.getElementById('els-live-rpm');
    if (rpmEl) rpmEl.textContent = Math.round(state.rpm);

    const zEl = document.getElementById('els-live-z');
    if (zEl) zEl.textContent = formatValue(displayZ(state.zPosMm));
}

// --- Reconnect ---

function reconnectWs() {
    if (ws) {
        ws.close();
    }
}

// --- Init ---
loadSavedPositions();
loadToolTable();
connect();
updateDRO();
updateConnectionUI();
updateActiveToolDisplay();
renderSavedPositions();
