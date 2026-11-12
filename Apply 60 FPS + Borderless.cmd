@echo off
cd /d "%~dp0"
python "%~dp0BorderlessFullscreen.py" --apply --fps60 --watch-hangs
pause
