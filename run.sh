#!/bin/bash
set -e
echo "=== FleetOS Dashboard ==="

cd "$(dirname "$0")"

make clean && make

echo "Starting dashboard on http://localhost:8080"
cd web
python3 -m venv .venv 2>/dev/null || true
source .venv/bin/activate 2>/dev/null || true
pip install -q flask flask-socketio 2>/dev/null || true
python3 server.py
