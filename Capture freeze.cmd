@echo off
cd /d "%~dp0"
python "%~dp0HangDiagnostics.py" --capture
pause
