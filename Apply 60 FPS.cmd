@echo off
cd /d "%~dp0"
python -m swtfu.fps --apply
python -m swtfu.controller
pause
