@echo off
cd /d "%~dp0"
python "%~dp0FramePacingTrace.py" --seconds 120
pause
