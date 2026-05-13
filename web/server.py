from flask import Flask, render_template
from flask_socketio import SocketIO, emit
import subprocess
import threading
import json
import sys

app = Flask(__name__)
app.config['SECRET_KEY'] = 'fleetos-secret'
socketio = SocketIO(app, cors_allowed_origins="*")

simulator_proc = None

@app.route('/')
def index():
    return render_template('index.html')

@socketio.on('connect')
def handle_connect():
    emit('status', {'msg': 'Connected to FleetOS Dashboard'})

@socketio.on('start')
def handle_start(data):
    global simulator_proc
    mode = data.get('mode', 'mlfq')
    simulator_proc = subprocess.Popen(
        ['./drone_fleet_os', f'--mode={mode}'],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd='..'
    )
    threading.Thread(target=read_stdout, daemon=True).start()

def read_stdout():
    if simulator_proc and simulator_proc.stdout:
        for line in simulator_proc.stdout:
            try:
                event = json.loads(line.strip())
                socketio.emit('log', event)
            except json.JSONDecodeError:
                socketio.emit('log', {'raw': line.strip()})

if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=8080, debug=True)
