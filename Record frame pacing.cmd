@echo off
cd /d "%~dp0"
python -m swtfu.frame_pacing --seconds 120
pause
