@echo off
setlocal
cd /d "%~dp0"
echo Open http://127.0.0.1:5173/ in Chrome or Edge.
echo Keep this window open while using the configurator.
call npm.cmd run dev -- --host 127.0.0.1 --port 5173 --strictPort
pause
