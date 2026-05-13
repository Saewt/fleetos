#!/bin/bash
set -e

echo "FleetOS Build & Run Script"

# Build C simulator
make clean && make

# Start Python dashboard in background
cd web
python3 -m venv .venv
source .venv/bin/activate
pip install -r ../requirements.txt
python3 server.py &
SERVER_PID=$!
cd ..

# Run simulator
./drone_fleet_os "$@"

# Cleanup
kill $SERVER_PID 2>/dev/null || true
