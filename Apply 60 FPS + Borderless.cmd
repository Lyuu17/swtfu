@echo off
cd /d "%~dp0"
python -m swtfu.borderless --apply --fps60 --watch-hangs
pause
