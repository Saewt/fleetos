# 🚁 FleetOS: Drone Fleet OS Simulator

**FleetOS** is a comprehensive mini Operating System simulator developed as a Drone Fleet Controller. It demonstrates core OS concepts through a robust C-based backend simulation, paired with a modern, real-time glassmorphic web dashboard built with Python (Flask) and WebSockets.

## 🚀 Features

### Core OS Capabilities (C Backend)
* **Process Scheduling:** Implements Round Robin (RR) and Multi-Level Feedback Queue (MLFQ) scheduling algorithms.
* **Memory Management:** Simulates a paging system utilizing 256-byte pages and 16 frames.
* **Concurrency & Synchronization:** Demonstrates Producer-Consumer problem solving with a bounded buffer.
* **File System:** Custom FAT-based file system implementation (64 blocks × 64B).
* **Deadlock Detection:** Utilizes Resource Allocation Graphs to detect and manage system deadlocks.
* **Error Handling:** Simulates real-world fault tolerance, including drone crash scenarios.
* **State Serialization:** Exports memory, filesystem, and scheduler states for real-time monitoring.

### Web Dashboard (Python/Flask + Vanilla JS)
* **Real-Time Telemetry:** WebSocket event routing for live updates on drone fleet metrics.
* **Modern UI:** Advanced 7-panel glassmorphic user interface.
* **State Visualization:** Dynamic, real-time visualization of Memory allocation, File System blocks, Scheduler queues, System Logs, and Resource states.

## 🛠️ Technology Stack
* **Backend Simulator:** C (GCC, Make)
* **API & Web Server:** Python 3, Flask, Flask-SocketIO
* **Frontend:** HTML5, Vanilla CSS (Glassmorphism design), JavaScript (WebSockets)

## 📦 Getting Started

### Prerequisites
* GCC (GNU Compiler Collection) & Make
* Python 3.8+ & `pip`

### 1. Build and Run the Simulator (Backend)
```bash
# Compile the C source files
make

# Run the OS simulator (Available modes: rr, mlfq)
./drone_fleet_os --mode mlfq
```

### 2. Start the Web Dashboard
Open a new terminal window and run:
```bash
# Install the required Python dependencies
pip install -r requirements.txt

# Start the Flask web server
python3 web/server.py
```
*Once running, navigate to `http://localhost:5000` in your web browser to view the dashboard.*

## 🌿 Branch Strategy
* `main`: Stable release code.
* `dev`: Main integration branch for active development.
* `feature/*`: Feature-specific module development branches (e.g., `feature/web-dashboard`).

## 📁 Project Structure
```text
fleetos/
├── src/               # Core OS C source files (kernel, memory, scheduler, filesystem, etc.)
├── web/               # Python Flask server and Dashboard (HTML/CSS/JS)
│   ├── server.py      # Flask application entry point
│   ├── static/        # CSS styles and JavaScript files
│   └── templates/     # HTML templates
├── docs/              # Documentation and architectural diagrams
├── Makefile           # Build scripts for the C simulator
└── requirements.txt   # Python dependencies for the web server
```
