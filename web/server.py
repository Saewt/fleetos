from flask import Flask, render_template, request
from flask_socketio import SocketIO, emit
import subprocess
import threading
import json
import time
import os
import signal

app = Flask(__name__)
app.config['SECRET_KEY'] = 'fleetos-secret'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

sim = None

class SimulatorController:
    def __init__(self):
        self.proc = None
        self.paused = True
        self.speed = 1.2
        self._running = False
        self._step_thread = None
        self._sim_mode = 'rr'
        self._sim_ticks = 200
        self._sim_deadlock = False
        self._sim_crash = False

    def start(self, mode='rr', ticks=200, deadlock=False, crash=False):
        self.stop()
        self._sim_mode = mode
        self._sim_ticks = ticks
        self._sim_deadlock = deadlock
        self._sim_crash = crash
        self.paused = True
        self._running = True

        cmd = [
            os.path.join(PROJECT_ROOT, 'drone_fleet_os'),
            '--interactive',
            f'--mode={mode}',
            f'--ticks={ticks}'
        ]
        if deadlock:
            cmd.append('--deadlock')
        if crash:
            cmd.append('--crash')

        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=PROJECT_ROOT
        )
        threading.Thread(target=self._read_output, daemon=True).start()
        socketio.emit('sim_started', {
            'mode': mode, 'ticks': ticks,
            'deadlock': deadlock, 'crash': crash
        })

    def _read_output(self):
        try:
            for line in self.proc.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    data = json.loads(line)
                except json.JSONDecodeError:
                    socketio.emit('log', {'raw': line, 'module': 'RAW', 'level': 'INFO',
                                          'msg': line, 'tick': -1})
                    continue

                if 'comparison' in data:
                    socketio.emit('comparison', data['comparison'])
                elif data.get('module') == 'SNAPSHOT':
                    socketio.emit('snapshot', data)
                else:
                    socketio.emit('log', data)
        except Exception:
            pass
        finally:
            self._running = False
            socketio.emit('sim_stopped', {})

    def resume(self):
        if not self._running:
            return
        self.paused = False
        if self._step_thread is None or not self._step_thread.is_alive():
            self._step_thread = threading.Thread(target=self._auto_step, daemon=True)
            self._step_thread.start()

    def _auto_step(self):
        while not self.paused and self._running:
            time.sleep(self.speed)
            if not self.paused and self._running:
                try:
                    self.proc.stdin.write('step\n')
                    self.proc.stdin.flush()
                except Exception:
                    break

    def pause_sim(self):
        self.paused = True

    def step_sim(self):
        if not self._running:
            return
        try:
            self.proc.stdin.write('step\n')
            self.proc.stdin.flush()
        except Exception:
            pass

    def stop(self):
        self.paused = True
        if self._running and self.proc:
            try:
                self.proc.stdin.write('quit\n')
                self.proc.stdin.flush()
            except Exception:
                pass
            time.sleep(0.3)
            try:
                if self.proc.poll() is None:
                    self.proc.terminate()
            except Exception:
                pass
        self.proc = None
        self._running = False

    def set_speed(self, interval):
        self.speed = max(0.05, min(2.0, interval))

    def run_compare(self, ticks=100):
        self.stop()
        cmd = [
            os.path.join(PROJECT_ROOT, 'drone_fleet_os'),
            '--compare',
            f'--ticks={ticks}'
        ]
        self._running = True
        self.paused = False
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=PROJECT_ROOT
        )
        threading.Thread(target=self._read_output, daemon=True).start()
        socketio.emit('sim_started', {'mode': 'compare', 'ticks': ticks})


@app.route('/')
def index():
    return render_template('index.html')


@socketio.on('connect')
def handle_connect():
    emit('status', {'msg': 'Connected'})


@socketio.on('start')
def handle_start(data):
    global sim
    if sim is None:
        sim = SimulatorController()
    mode = data.get('mode', 'rr')
    ticks = int(data.get('ticks', 200))
    deadlock = bool(data.get('deadlock', False))
    crash = bool(data.get('crash', False))
    sim.start(mode=mode, ticks=ticks, deadlock=deadlock, crash=crash)


@socketio.on('resume')
def handle_resume():
    if sim:
        sim.resume()


@socketio.on('pause')
def handle_pause():
    if sim:
        sim.pause_sim()


@socketio.on('step')
def handle_step():
    if sim:
        sim.step_sim()


@socketio.on('stop')
def handle_stop():
    global sim
    if sim:
        sim.stop()
        sim = None


@socketio.on('set_speed')
def handle_set_speed(data):
    if sim:
        sim.set_speed(float(data.get('interval', 0.4)))


@socketio.on('run_compare')
def handle_compare(data):
    global sim
    if sim is None:
        sim = SimulatorController()
    ticks = int(data.get('ticks', 100))
    sim.run_compare(ticks=ticks)


if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=8080, debug=False, allow_unsafe_werkzeug=True)
