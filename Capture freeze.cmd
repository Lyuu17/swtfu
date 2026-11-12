@echo off
cd /d "%~dp0"
python -m swtfu.hang_diagnostics --capture
pause
