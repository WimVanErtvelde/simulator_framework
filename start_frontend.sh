#!/bin/bash
# Terminal 3 — IOS Frontend (React dev server)
# Opens at http://localhost:5173
cd ~/simulator_framework/ios/frontend
fuser -k 5173/tcp 2>/dev/null
npm run dev
