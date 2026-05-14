const socket = io();

const SPECIAL = {
    Base:         {x: 0, y: 0,  icon: '&#8962;', name: 'Hangar'},
    LandingPad:   {x: 10, y: 2,  icon: '&#9992;', name: 'Landing Pad'},
    ChargeStation:{x: 10, y: 9,  icon: '&#9889;', name: 'Charge Station'},
    CommChannel:  {x: 5,  y: 6,  icon: '&#9776;', name: 'Comm Channel'},
    SensorBuffer: {x: 2,  y: 10, icon: '&#9632;', name: 'Sensor Buffer'},
    LogStation:   {x: 1,  y: 6,  icon: '&#9776;', name: 'Log Station'},
    MemoryRelay:  {x: 6,  y: 1,  icon: '&#9670;', name: 'Memory Relay'}
};

const MINES = new Set([41, 72, 84, 35, 95, 18, 49, 108, 116]);

const DRONE_COLORS = ['flight', 'battery', 'mapping', 'log'];
const DRONE_LABELS = ['F', 'B', 'M', 'L'];

const LOG_MODULES = ['KERNEL', 'SCHED', 'MEM', 'FS', 'SYNC', 'DEADLOCK', 'FAULT', 'THREAD'];
const CONCEPT_BY_MODULE = {
    SCHED: 'scheduling',
    MEM: 'memory',
    FS: 'io',
    SYNC: 'sync',
    DEADLOCK: 'failure',
    FAULT: 'failure'
};
const RESOURCE_NAMES = ['LandingPad', 'ChargeStation', 'CommChannel'];
const TOUR_STEPS = [
    {
        title: 'Drone Fleet OS Simulator',
        target: null,
        body: 'Bu ekran mini OS simülatörünü savunmak için tasarlandı. Drone haritası canlı dünya metaforu, sağ paneller ise kernel alt sistemlerinin durumudur.',
        points: [
            'Drone = process/thread',
            'Grid resource = OS kaynağı',
            'Her log ve snapshot gözlemlenebilir tasarım kararıdır'
        ]
    },
    {
        title: '1. World Grid',
        target: '#grid-area',
        body: 'Sol grid canlı simülasyon haritasıdır. Drone rengi ve parlaması process durumunu gösterir; blocked drone hangi OS olayını bekliyorsa ilgili kaynağa gider.',
        points: [
            'RUNNING drone aktif CPU kullanımını gösterir',
            'BLOCKED drone page fault, I/O, mutex veya resource bekler',
            'Legend sunum sırasında durum renklerini hızlı açıklar'
        ]
    },
    {
        title: '2. Scheduler',
        target: '#scheduler-panel',
        body: 'Scheduler paneli CPU seçimini görünür yapar. RR modunda tek ready queue, MLFQ modunda Q0/Q1/Q2 ve priority boost gösterilir.',
        points: [
            'Current thread ve quantum takip edilir',
            'I/O veya page fault sonrası scheduler başka thread seçer',
            'MLFQ, RR baseline tasarımına karşı enhanced versiyondur'
        ]
    },
    {
        title: '3. Memory + File I/O',
        target: '#memory-panel, #fs-panel',
        body: 'Memory ve filesystem panelleri cross-component interaction kanıtıdır. Page fault veya file write süreci BLOCKED yapar ve scheduler devreye girer.',
        points: [
            'Memory frames LRU/paging durumunu gösterir',
            'Filesystem paneli yazılan log dosyalarını gösterir',
            'Concept strip son OS olayını highlight eder'
        ]
    },
    {
        title: '4. Sync + Resources',
        target: '#buffer-panel, #resource-panel',
        body: 'Ring buffer, mutex owner ve resource ownership concurrency kısmını anlatır. Buffer dolu/boş veya kaynak dolu olduğunda thread beklemeye geçer.',
        points: [
            'SYNC logları lock/unlock davranışını gösterir',
            'Resource panel deadlock için owner/waiter ilişkisini açıklar',
            'Bu bölüm thread, lock ve condition bekleme mantığını savunmak için kullanılır'
        ]
    },
    {
        title: '5. Failure + Compare',
        target: '#btn-deadlock, #btn-crash, #btn-compare',
        body: 'Deadlock ve crash kontrollü failure senaryolarıdır. Compare butonu RR ve MLFQ metriklerini yan yana verir.',
        points: [
            'Deadlock: cycle detection, victim seçimi, cleanup',
            'Crash: process termination ve kaynak temizliği',
            'Compare: baseline vs enhanced scheduler savunması'
        ]
    }
];

let state = {
    tick: 0,
    procs: [],
    memory: null,
    scheduler: null,
    filesystem: null,
    buffer: null,
    logs: [],
    filters: new Set(LOG_MODULES),
    scenario: 'normal',
    mode: 'rr',
    running: false,
    paused: true
};
let tourIndex = 0;

function cellKey(x, y) { return y * 12 + x; }
function cellPos(key) { return {x: key % 12, y: Math.floor(key / 12)}; }
function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function formatPid(pid) {
    return Number.isInteger(pid) && pid >= 0 ? `P${pid}` : 'free';
}

function getDroneTarget(proc) {
    const reason = proc.blocked_reason;
    const pid = proc.pid;
    const st = proc.state;

    if (st === 'TERMINATED') return null;

    if (st === 'BLOCKED') {
        if (reason === 'PAGE_FAULT') return {...SPECIAL.MemoryRelay};
        if (reason === 'IO_BLOCK') return {...SPECIAL.LogStation};
        if (reason === 'MUTEX_WAIT' || reason === 'COND_WAIT') return {x: SPECIAL.SensorBuffer.x, y: Math.min(11, SPECIAL.SensorBuffer.y + pid - 1)};
        if (reason === 'RESOURCE_WAIT') {
            if (pid === 0) return {...SPECIAL.LandingPad};
            if (pid === 1) return {...SPECIAL.ChargeStation};
            return {...SPECIAL.CommChannel};
        }
        return {x: 1, y: pid + 1};
    }

    if (st === 'RUNNING') {
        const phase = (state.tick + pid * 3) % 24;
        let wx, wy;
        if (phase < 6) { wx = 8; wy = 3; }
        else if (phase < 12) { wx = 6; wy = 8; }
        else if (phase < 18) { wx = 3; wy = 6; }
        else { wx = 5; wy = 3; }
        return {x: Math.min(11, wx + (pid % 3) - 1), y: Math.min(11, wy + (pid % 2))};
    }

    return {x: Math.min(2, (state.tick + pid) % 3), y: Math.min(11, pid + 1)};
}

function computeDronePositions() {
    const positions = {};
    state.procs.forEach(proc => {
        if (proc.state === 'TERMINATED') return;
        const target = getDroneTarget(proc);
        if (!target) return;
        positions[proc.pid] = target;
    });
    return positions;
}

function renderGrid() {
    const grid = document.getElementById('drone-grid');
    const positions = computeDronePositions();

    let html = '';
    for (let y = 0; y < 12; y++) {
        for (let x = 0; x < 12; x++) {
            let classes = ['cell'];
            let label = '';
            let droneHtml = '';

            const key = cellKey(x, y);

            if (MINES.has(key)) {
                classes.push('cell-mine');
                label = '&#9762;';
            } else if (key === cellKey(SPECIAL.Base.x, SPECIAL.Base.y)) {
                classes.push('cell-base');
                label = SPECIAL.Base.icon;
            } else {
                let isSpecial = false;
                for (const [name, spec] of Object.entries(SPECIAL)) {
                    if (name === 'Base') continue;
                    if (key === cellKey(spec.x, spec.y)) {
                        classes.push('cell-resource');
                        label = spec.icon;
                        isSpecial = true;
                        break;
                    }
                }
                if (!isSpecial) {
                    classes.push('cell-empty');
                }
            }

            for (const proc of state.procs) {
                if (proc.state === 'TERMINATED') continue;
                const pos = positions[proc.pid];
                if (pos && pos.x === x && pos.y === y) {
                    const droneClass = 'drone-' + (DRONE_COLORS[proc.pid] || 'flight');
                    const stateClass = 'drone-' + proc.state.toLowerCase();
                    droneHtml = `<div class="drone-marker ${droneClass} ${stateClass}"
                        data-pid="${proc.pid}">${DRONE_LABELS[proc.pid] || '?'}</div>`;
                    break;
                }
            }

            html += `<div class="${classes.join(' ')}">${label}${droneHtml}</div>`;
        }
    }
    grid.innerHTML = html;
}

function showTooltip(e, pid) {
    const proc = state.procs.find(p => p.pid === pid);
    if (!proc) return;
    const tip = document.getElementById('cell-tooltip');
    const threads = proc.threads || [];
    const runningCount = threads.filter(t => t.state === 'T_RUNNING').length;
    const blockedCount = threads.filter(t => t.state === 'T_BLOCKED').length;

    tip.innerHTML = `<b>${escapeHtml(proc.name)}</b> [PID:${proc.pid}]<br>
        State: <span style="color:${stateColor(proc.state)}">${proc.state}</span>
        | Priority: ${proc.priority}<br>
        Wait: ${proc.wait_time} | Burst: ${proc.burst_remaining}<br>
        Block: ${escapeHtml(proc.blocked_reason || 'NONE')}<br>
        Threads: ${threads.length} (run:${runningCount} block:${blockedCount})`;

    tip.classList.add('show');
    tip.style.left = (e.clientX + 14) + 'px';
    tip.style.top = (e.clientY + 14) + 'px';
}

function hideTooltip() {
    document.getElementById('cell-tooltip').classList.remove('show');
}

function stateColor(s) {
    return {'NEW':'var(--color-new)','READY':'var(--color-ready)','RUNNING':'var(--color-running)',
            'BLOCKED':'var(--color-blocked)','TERMINATED':'var(--color-terminated)'}[s] || '#888';
}

function renderProcessPanel() {
    const el = document.getElementById('process-cards');
    if (!state.procs.length) { el.innerHTML = '<div class="empty-note">Waiting for simulation...</div>'; return; }

    el.innerHTML = state.procs.map(proc => {
        const threads = proc.threads || [];
        const barFill = Math.max(0, proc.burst_remaining);
        const maxBurst = Math.max(barFill, 20);
        const runT = threads.filter(t => t.state === 'T_RUNNING').length;
        const blockT = threads.filter(t => t.state === 'T_BLOCKED').length;
        const readyT = threads.filter(t => t.state === 'T_READY').length;

        return `<div class="proc-card">
            <span class="proc-indicator indicator-${proc.state.toLowerCase()}"></span>
            <span class="proc-name">${escapeHtml(proc.name)}</span>
            <span class="proc-state" style="color:${stateColor(proc.state)}">${proc.state}</span>
            <span class="proc-meta">P:${proc.priority}</span>
            <span class="proc-meta">W:${proc.wait_time}</span>
            <span class="proc-bar"><div class="proc-bar-fill" style="width:${(barFill/maxBurst*100)}%;background:${stateColor(proc.state)}"></div></span>
            <span class="proc-meta">${barFill}</span>
            <span class="proc-threads">T:${threads.length} [R:${runT} B:${blockT} D:${readyT}]</span>
        </div>`;
    }).join('');
}

function renderSchedulerPanel() {
    const el = document.getElementById('scheduler-content');
    const sched = state.scheduler;
    if (!sched) { el.innerHTML = '<div class="empty-note">No scheduler data</div>'; return; }

    let html = '';
    html += `<div class="queue-row"><span class="queue-label">Run</span>
        <span class="queue-tid queue-running">T${sched.current_tid !== undefined ? sched.current_tid : '?'}</span>
        <span class="queue-meta">quantum:${sched.quantum_remaining || '?'}</span></div>`;

    if (sched.mode === 'RR' && sched.ready) {
        html += `<div class="queue-row"><span class="queue-label">Ready</span>
            <span class="queue-tids">${sched.ready.map(t => `<span class="queue-tid">T${t}</span>`).join('') || '<span class="empty-note">empty</span>'}</span></div>`;
    } else if (sched.mode === 'MLFQ') {
        for (let q = 0; q < 3; q++) {
            const qdata = sched['q' + q] || [];
            html += `<div class="queue-row"><span class="queue-label">Q${q}</span>
                <span class="queue-tids">${qdata.map(t => `<span class="queue-tid">T${t}</span>`).join('') || '<span class="empty-note">empty</span>'}</span></div>`;
        }
        html += `<div class="queue-row queue-meta">Boosts: ${sched.priority_boosts || 0}</div>`;
    }

    el.innerHTML = html;
}

function pidColor(pid) {
    const colors = ['#3b82f6', '#ef4444', '#22c55e', '#f59e0b', '#8b5cf6', '#06b6d4', '#ec4899', '#84cc16'];
    return colors[pid % colors.length];
}

function renderMemoryPanel() {
    const el = document.getElementById('memory-content');
    const mem = state.memory;
    if (!mem || !mem.framemap) { el.innerHTML = '<div class="empty-note">No memory data</div>'; return; }

    let html = `<div class="panel-meta">Free: ${mem.free_frames} / 16</div>`;
    html += '<div class="mem-grid">';
    mem.framemap.forEach(fm => {
        const cls = fm.pid >= 0 ? '' : 'mem-free';
        const bg = fm.pid >= 0 ? pidColor(fm.pid) : 'transparent';
        const label = fm.pid >= 0 ? 'F' + fm.frame : '·';
        html += `<div class="mem-cell ${cls}" style="background:${bg};color:${fm.pid >= 0 ? '#fff' : '#3a3f50'}">${label}</div>`;
    });
    html += '</div>';
    el.innerHTML = html;
}

function renderBufferPanel() {
    const el = document.getElementById('buffer-content');
    const buf = state.buffer;
    if (!buf) { el.innerHTML = '<div class="empty-note">No buffer data</div>'; return; }

    let html = `<div class="buf-info">
        <span>Items: <b>${buf.count}</b>/8</span>
        <span>In: ${buf.in} Out: ${buf.out}</span>
        <span>Mutex Owner: <b>${buf.mutex_owner >= 0 ? 'P' + buf.mutex_owner : 'free'}</b></span>
    </div>`;
    if (buf.items && buf.items.length) {
        html += '<div class="buf-items">';
        buf.items.forEach(item => {
            html += `<span class="buf-item">${escapeHtml(item)}</span>`;
        });
        html += '</div>';
    }
    el.innerHTML = html;
}

function renderFilesystemPanel() {
    const el = document.getElementById('fs-content');
    const fs = state.filesystem;
    if (!fs || !fs.files) { el.innerHTML = '<div class="empty-note">No filesystem data</div>'; return; }

    let html = `<div class="panel-meta">${fs.file_count} files</div>`;
    fs.files.forEach(f => {
        html += `<div class="fs-file"><span class="fs-path">${escapeHtml(f.path)}</span><span class="fs-size">${f.size}B</span></div>`;
    });
    el.innerHTML = html;
}

function renderResourcePanel() {
    const el = document.getElementById('resource-content');
    if (!el) return;
    if (!state.procs.length) {
        el.innerHTML = '<div class="empty-note">No resource data</div>';
        return;
    }

    const resources = RESOURCE_NAMES.map(name => ({name, owners: [], waiters: []}));
    state.procs.forEach(proc => {
        const held = proc.held_resources || [];
        held.forEach((value, index) => {
            if (value && resources[index]) resources[index].owners.push(proc.pid);
        });
        if (proc.state === 'BLOCKED' && proc.blocked_reason === 'RESOURCE_WAIT') {
            const requested = Number.isInteger(proc.requested_resource) ? proc.requested_resource : -1;
            const index = requested >= 0 && requested < resources.length ? requested : Math.min(proc.pid, resources.length - 1);
            if (resources[index]) resources[index].waiters.push(proc.pid);
        }
    });

    el.innerHTML = resources.map(resource => `
        <div class="resource-row">
            <span class="resource-name">${resource.name}</span>
            <span>owner: <b>${resource.owners.length ? resource.owners.map(formatPid).join(', ') : 'free'}</b></span>
            <span>wait: <b>${resource.waiters.length ? resource.waiters.map(formatPid).join(', ') : '-'}</b></span>
        </div>
    `).join('');
}

function conceptFromEvent(data) {
    const module = data.module || 'RAW';
    const msg = String(data.msg || data.raw || '');
    if (CONCEPT_BY_MODULE[module]) return CONCEPT_BY_MODULE[module];
    if (msg.includes('Page fault')) return 'memory';
    if (msg.includes('I/O') || msg.includes('write')) return 'io';
    if (msg.includes('Mutex') || msg.includes('Data produced') || msg.includes('Data consumed')) return 'sync';
    if (msg.includes('blocked') || msg.includes('Context switch')) return 'scheduling';
    if (msg.includes('Deadlock') || msg.includes('Crash')) return 'failure';
    return null;
}

function highlightConcept(concept) {
    document.querySelectorAll('.concept-chip').forEach(chip => {
        chip.classList.toggle('active', Boolean(concept) && chip.dataset.concept === concept);
    });
}

function updateLastEvent(data) {
    const el = document.getElementById('last-event');
    if (!el) return;
    const tick = data.tick !== undefined ? data.tick : '?';
    const module = data.module || 'RAW';
    const msg = data.msg || data.raw || JSON.stringify(data.data || '');
    el.textContent = `Tick ${tick}: ${module} - ${msg}`;
}

function explainEventForPresentation(data) {
    const module = data.module || 'RAW';
    const msg = String(data.msg || data.raw || '');
    const concept = conceptFromEvent(data) || 'idle';

    if (msg.includes('Context switch')) {
        return {concept: 'scheduling', text: 'Scheduler CPU kontrolünü başka thread’e verdi. Ready queue’dan yeni çalışan seçildi.'};
    }
    if (msg.includes('Process blocked') || msg.includes('Thread blocked')) {
        return {concept: 'scheduling', text: 'Bir process/thread beklemeye geçti. Çalışmaya devam edemediği için scheduler başka işi seçebilir.'};
    }
    if (msg.includes('Process unblocked') || msg.includes('Thread unblocked')) {
        return {concept: 'scheduling', text: 'Bekleyen process/thread tekrar READY oldu. Artık scheduler kuyruğuna dönebilir.'};
    }
    if (msg.includes('Page fault resolved')) {
        return {concept: 'memory', text: 'Page fault çözüldü. Sayfa belleğe yüklendi ve process tekrar çalışabilir hale geldi.'};
    }
    if (msg.includes('Page fault')) {
        return {concept: 'memory', text: 'Page fault oluştu. İstenen sayfa bellekte yok; process geçici olarak BLOCKED oldu.'};
    }
    if (msg.includes('I/O write started')) {
        return {concept: 'io', text: 'File I/O başladı. Process IO_BLOCK durumunda beklerken CPU başka thread’e verilebilir.'};
    }
    if (msg.includes('I/O completed')) {
        return {concept: 'io', text: 'File I/O tamamlandı. Bekleyen process tekrar READY durumuna dönebilir.'};
    }
    if (msg.includes('Mutex lock acquired')) {
        return {concept: 'sync', text: 'Mutex alındı. Thread shared buffer’a güvenli şekilde erişiyor.'};
    }
    if (msg.includes('Mutex lock wait')) {
        return {concept: 'sync', text: 'Mutex dolu. Thread synchronization nedeniyle beklemeye geçti.'};
    }
    if (msg.includes('Mutex unlock')) {
        return {concept: 'sync', text: 'Mutex bırakıldı. Bekleyen thread varsa uyandırılabilir.'};
    }
    if (msg.includes('Data produced')) {
        return {concept: 'sync', text: 'Producer buffer’a veri ekledi. Consumer thread’ler için yeni veri hazır.'};
    }
    if (msg.includes('Data consumed')) {
        return {concept: 'sync', text: 'Consumer buffer’dan veri aldı. Buffer’da yer açıldığı için producer devam edebilir.'};
    }
    if (msg.includes('Resource requested')) {
        return {concept: 'failure', text: 'Process bir kaynak istedi. Kaynak doluysa RESOURCE_WAIT durumu deadlock zincirine katkı verebilir.'};
    }
    if (msg.includes('Resource acquired') || msg.includes('Resource allocated')) {
        return {concept: 'failure', text: 'Kaynak process’e verildi. Resource Ownership panelinden owner ilişkisini takip edin.'};
    }
    if (msg.includes('Deadlock detected')) {
        return {concept: 'failure', text: 'Deadlock yakalandı. Circular wait oluştu; sistem victim seçip cleanup yapacak.'};
    }
    if (msg.includes('Deadlock victim terminated')) {
        return {concept: 'failure', text: 'Deadlock victim process sonlandırıldı. Kaynakları temizlenip bekleyen process’ler devam ettiriliyor.'};
    }
    if (msg.includes('Crash injected')) {
        return {concept: 'failure', text: 'Controlled fault injection çalıştı. Process çökertildi ve OS cleanup prosedürü başladı.'};
    }
    if (module === 'SNAPSHOT') {
        return null;
    }
    if (concept !== 'idle') {
        return {concept, text: `${module} olayı gerçekleşti: ${msg}`};
    }
    return null;
}

function updatePresentationNote(data) {
    const explanation = explainEventForPresentation(data);
    if (!explanation) return;

    const panel = document.getElementById('presentation-note');
    const textEl = document.getElementById('presentation-note-text');
    const metaEl = document.getElementById('presentation-note-meta');
    if (!panel || !textEl || !metaEl) return;

    const tick = data.tick !== undefined ? data.tick : '?';
    const module = data.module || 'RAW';
    const level = data.level || 'INFO';

    panel.className = `note-${explanation.concept}`;
    textEl.textContent = explanation.text;
    metaEl.textContent = `Tick ${tick} · ${module} · ${level}`;
}

function resetPresentationNote(text) {
    const panel = document.getElementById('presentation-note');
    const textEl = document.getElementById('presentation-note-text');
    const metaEl = document.getElementById('presentation-note-meta');
    if (!panel || !textEl || !metaEl) return;
    panel.className = 'note-idle';
    textEl.textContent = text;
    metaEl.textContent = 'Hazır · Sunum modu';
}

let logCount = 0;
function addLog(data) {
    state.logs.push(data);
    if (state.logs.length > 300) state.logs.shift();
    highlightConcept(conceptFromEvent(data));
    updateLastEvent(data);
    updatePresentationNote(data);
    const mod = data.module || 'RAW';
    if (!state.filters.has(mod) && mod !== 'RAW') return;
    renderLogEntry(data);
}

function renderLogEntry(data) {
    const el = document.getElementById('log-entries');
    const tick = data.tick !== undefined ? data.tick : '?';
    const module = data.module || 'RAW';
    const level = (data.level || 'INFO').toLowerCase();
    const msg = data.msg || JSON.stringify(data.data || data.raw || '');
    const tickStr = String(tick).padStart(3, ' ');

    const div = document.createElement('div');
    div.className = 'log-entry';
    div.setAttribute('data-module', module);
    div.innerHTML = `<span class="log-tick">[${tickStr}]</span>
        <span class="log-level level-${level}">${escapeHtml(data.level || '')}</span>
        <span class="log-msg">${escapeHtml(module)} ${escapeHtml(msg)}</span>`;
    el.appendChild(div);
    el.scrollTop = el.scrollHeight;

    if (el.children.length > 300) el.removeChild(el.firstChild);
}

function filterLogs() {
    const el = document.getElementById('log-entries');
    el.innerHTML = '';
    state.logs.forEach(data => {
        const mod = data.module || 'RAW';
        if (state.filters.has(mod) || mod === 'RAW') renderLogEntry(data);
    });
}

function renderLogFilters() {
    const el = document.getElementById('log-filters');
    el.innerHTML = LOG_MODULES.map(m =>
        `<button type="button" class="log-filter ${state.filters.has(m) ? 'on' : ''}"
            aria-pressed="${state.filters.has(m)}" data-module="${m}">${m}</button>`
    ).join('');
}

function toggleFilter(mod) {
    if (state.filters.has(mod)) state.filters.delete(mod);
    else state.filters.add(mod);
    renderLogFilters();
    filterLogs();
}

function renderAll() {
    renderGrid();
    renderProcessPanel();
    renderSchedulerPanel();
    renderMemoryPanel();
    renderBufferPanel();
    renderFilesystemPanel();
    renderResourcePanel();
}

function setScenario(s) {
    state.scenario = s;
    ['normal', 'deadlock', 'crash'].forEach(name => {
        const btn = document.getElementById(`btn-${name}`);
        const active = s === name;
        btn.classList.toggle('active', active);
        btn.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
}

function startSim() {
    const mode = document.getElementById('mode-select').value;
    const ticks = parseInt(document.getElementById('ticks-input').value) || 200;

    state.mode = mode;
    state.procs = [];
    state.logs = [];
    state.tick = 0;
    state.running = true;
    state.paused = true;
    document.getElementById('last-event').textContent = 'Simulation ready. Use Step for one kernel tick or Resume to run slowly.';
    resetPresentationNote('Simülasyon hazır. Step ile tek tick ilerleyin veya Resume ile yavaş akıtın.');
    highlightConcept(null);

    document.getElementById('log-entries').innerHTML = '';
    document.getElementById('tick-display').textContent = '0';
    setPauseButtonPaused();
    renderAll();
    renderLogFilters();

    socket.emit('start', {
        mode: mode,
        ticks: ticks,
        deadlock: state.scenario === 'deadlock',
        crash: state.scenario === 'crash'
    });

    startAutoResume = false;
}

var startAutoResume = false;

function setPauseButtonPaused() {
    const btn = document.getElementById('btn-pause');
    btn.innerHTML = '&#9654; Resume';
    btn.classList.add('active');
}

function setPauseButtonRunning() {
    const btn = document.getElementById('btn-pause');
    btn.innerHTML = '&#9646;&#9646; Pause';
    btn.classList.remove('active');
}

function togglePause() {
    if (state.paused) {
        state.paused = false;
        socket.emit('resume');
        setPauseButtonRunning();
    } else {
        state.paused = true;
        socket.emit('pause');
        setPauseButtonPaused();
    }
}

function stepSim() {
    state.paused = true;
    setPauseButtonPaused();
    socket.emit('step');
}

function stopSim() {
    state.running = false;
    state.paused = true;
    socket.emit('stop');
    state.procs = [];
    state.tick = 0;
    document.getElementById('tick-display').textContent = '0';
    setPauseButtonPaused();
    resetPresentationNote('Simülasyon durdu. Yeni senaryo seçip tekrar Start verebilirsiniz.');
    renderAll();
}

function setSpeed(val) {
    document.getElementById('speed-label').textContent = parseFloat(val).toFixed(2) + 's';
    socket.emit('set_speed', { interval: parseFloat(val) });
}

function runCompare() {
    stopSim();
    state.mode = 'compare';
    const ticks = parseInt(document.getElementById('ticks-input').value) || 100;
    socket.emit('run_compare', { ticks: ticks });
}

function closeComparison() {
    document.getElementById('comparison-panel').classList.remove('show');
}

function renderComparison(data) {
    const panel = document.getElementById('comparison-panel');
    const el = document.getElementById('comparison-content');

    const rr = data.rr || {};
    const mlfq = data.mlfq || {};

    const fields = [
        ['Ticks', 'ticks'],
        ['Context Switches', 'context_switches'],
        ['Priority Boosts', 'prio_boosts'],
        ['Avg Wait', 'avg_wait'],
        ['Max Wait', 'max_wait'],
        ['Avg Turnaround', 'avg_turnaround'],
        ['Max Turnaround', 'max_turnaround'],
        ['Terminated', 'terminated']
    ];

    let html = '<table><thead><tr><th>Metric</th><th>RR</th><th>MLFQ</th></tr></thead><tbody>';
    fields.forEach(([label, key]) => {
        const rv = rr[key] !== undefined ? rr[key] : '-';
        const mv = mlfq[key] !== undefined ? mlfq[key] : '-';
        let rrClass = '', mqClass = '';
        if (typeof rv === 'number' && typeof mv === 'number') {
            if (label.includes('Switch') || label.includes('Boost')) {
                rrClass = rv <= mv ? 'winner' : '';
                mqClass = mv <= rv ? 'winner' : '';
            } else if (label.includes('Avg') || label.includes('Max')) {
                rrClass = rv <= mv ? 'winner' : '';
                mqClass = mv <= rv ? 'winner' : '';
            } else if (label === 'Terminated') {
                rrClass = rv >= mv ? 'winner' : '';
                mqClass = mv >= rv ? 'winner' : '';
            }
        }
        const rvStr = typeof rv === 'number' ? (Number.isInteger(rv) ? rv : rv.toFixed(2)) : rv;
        const mvStr = typeof mv === 'number' ? (Number.isInteger(mv) ? mv : mv.toFixed(2)) : mv;
        html += `<tr><td>${label}</td><td class="${rrClass}">${rvStr}</td><td class="${mqClass}">${mvStr}</td></tr>`;
    });
    html += '</tbody></table>';
    el.innerHTML = html;
    panel.classList.add('show');
}

function setTourHighlight(target) {
    document.querySelectorAll('.tour-highlight').forEach(el => el.classList.remove('tour-highlight'));
    if (!target) return;
    document.querySelectorAll(target).forEach(el => el.classList.add('tour-highlight'));
}

function renderIntroStep() {
    const step = TOUR_STEPS[tourIndex];
    document.getElementById('intro-title').textContent = step.title;
    document.getElementById('intro-body').textContent = step.body;
    document.getElementById('intro-points').innerHTML = step.points
        .map(point => `<div class="intro-point">${escapeHtml(point)}</div>`)
        .join('');
    document.getElementById('intro-progress').textContent = `${tourIndex + 1} / ${TOUR_STEPS.length}`;
    document.getElementById('intro-back').disabled = tourIndex === 0;
    document.getElementById('intro-next').textContent = tourIndex === TOUR_STEPS.length - 1 ? 'Finish' : 'Next';
    setTourHighlight(step.target);
    if (step.target) {
        const firstTarget = document.querySelector(step.target);
        if (firstTarget) firstTarget.scrollIntoView({block: 'nearest', inline: 'nearest'});
    }
}

function openIntro(index = 0) {
    tourIndex = Math.max(0, Math.min(TOUR_STEPS.length - 1, index));
    document.getElementById('intro-overlay').classList.add('show');
    renderIntroStep();
}

function closeIntro() {
    document.getElementById('intro-overlay').classList.remove('show');
    setTourHighlight(null);
}

function nextIntroStep() {
    if (tourIndex >= TOUR_STEPS.length - 1) {
        closeIntro();
        return;
    }
    tourIndex++;
    renderIntroStep();
}

function previousIntroStep() {
    if (tourIndex === 0) return;
    tourIndex--;
    renderIntroStep();
}

function startGuidedDemo() {
    document.getElementById('mode-select').value = 'rr';
    document.getElementById('ticks-input').value = '200';
    setScenario('normal');
    document.getElementById('speed-slider').value = '1.2';
    setSpeed('1.2');
    document.getElementById('last-event').textContent = 'Guided demo is ready: RR + Normal scenario. Press Start, then use Step or Resume.';
    resetPresentationNote('Guided demo hazır: RR + Normal. Start sonrası Step ile anlatıma başlayın.');
    closeIntro();
    document.getElementById('btn-start').focus();
}

socket.on('connect', () => {
    document.getElementById('btn-start').style.borderColor = 'var(--accent)';
});

socket.on('disconnect', () => {
    document.getElementById('btn-start').style.borderColor = '';
});

socket.on('snapshot', (data) => {
    const d = data.data || {};
    state.procs = d.procs || [];
    state.memory = d.memory || null;
    state.scheduler = d.scheduler || null;
    state.filesystem = d.filesystem || null;
    state.buffer = d.buffer || null;
    state.tick = data.tick || 0;

    document.getElementById('tick-display').textContent = state.tick;

    renderAll();
});

socket.on('log', (data) => {
    addLog(data);
});

socket.on('comparison', (data) => {
    renderComparison(data);
});

socket.on('sim_stopped', () => {
    state.running = false;
    state.paused = true;
    setPauseButtonPaused();
});

socket.on('sim_started', (data) => {
    state.running = true;
    state.paused = true;
    state.mode = data.mode || 'rr';
    if (startAutoResume) {
        startAutoResume = false;
        setTimeout(() => {
            if (state.running) {
                state.paused = false;
                socket.emit('resume');
                setPauseButtonRunning();
            }
        }, 300);
    }
});

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('btn-start').addEventListener('click', startSim);
    document.getElementById('btn-pause').addEventListener('click', togglePause);
    document.getElementById('btn-step').addEventListener('click', stepSim);
    document.getElementById('btn-stop').addEventListener('click', stopSim);
    document.getElementById('btn-compare').addEventListener('click', runCompare);
    document.getElementById('btn-help').addEventListener('click', () => openIntro(0));
    document.getElementById('cmp-close').addEventListener('click', closeComparison);
    document.getElementById('intro-close').addEventListener('click', closeIntro);
    document.getElementById('intro-skip').addEventListener('click', closeIntro);
    document.getElementById('intro-next').addEventListener('click', nextIntroStep);
    document.getElementById('intro-back').addEventListener('click', previousIntroStep);
    document.getElementById('intro-start').addEventListener('click', startGuidedDemo);
    document.getElementById('intro-overlay').addEventListener('click', event => {
        if (event.target.id === 'intro-overlay') closeIntro();
    });
    document.addEventListener('keydown', event => {
        const introOpen = document.getElementById('intro-overlay').classList.contains('show');
        if (event.key === 'Escape') {
            closeIntro();
            closeComparison();
        } else if (introOpen && event.key === 'ArrowRight') {
            nextIntroStep();
        } else if (introOpen && event.key === 'ArrowLeft') {
            previousIntroStep();
        }
    });
    document.getElementById('speed-slider').addEventListener('input', event => setSpeed(event.target.value));
    document.querySelectorAll('[data-scenario]').forEach(btn => {
        btn.addEventListener('click', () => setScenario(btn.dataset.scenario));
    });
    document.getElementById('log-filters').addEventListener('click', event => {
        const btn = event.target.closest('[data-module]');
        if (btn) toggleFilter(btn.dataset.module);
    });
    document.getElementById('drone-grid').addEventListener('mousemove', event => {
        const marker = event.target.closest('.drone-marker');
        if (!marker) {
            hideTooltip();
            return;
        }
        showTooltip(event, Number(marker.dataset.pid));
    });
    document.getElementById('drone-grid').addEventListener('mouseleave', hideTooltip);

    renderGrid();
    renderProcessPanel();
    renderSchedulerPanel();
    renderMemoryPanel();
    renderBufferPanel();
    renderFilesystemPanel();
    renderResourcePanel();
    renderLogFilters();
    setSpeed(document.getElementById('speed-slider').value);
    setPauseButtonPaused();
    openIntro(0);
});
