const socket = io();

socket.on('connect', () => {
    document.getElementById('status').textContent = 'Connected';
});

socket.on('disconnect', () => {
    document.getElementById('status').textContent = 'Disconnected';
});

socket.on('log', (data) => {
    const logs = document.getElementById('logs');
    const entry = document.createElement('div');
    entry.textContent = JSON.stringify(data);
    logs.appendChild(entry);
    logs.scrollTop = logs.scrollHeight;
});

socket.on('status', (data) => {
    document.getElementById('status').textContent = data.msg;
});
