#!/bin/bash
# Terminal 2 — IOS Backend (FastAPI + rclpy WebSocket bridge)
# Wait until Terminal 1 prints "All N required nodes alive" before starting.
cd ~/simulator_framework
source /opt/ros/jazzy/setup.bash && source install/setup.bash
fuser -k 8080/tcp 2>/dev/null
python3 -m uvicorn ios_backend.ios_backend_node:app --host 0.0.0.0 --port 8080
