@echo off
cd /d "%~dp0"
python "%~dp0SWTFU60.py" --apply
python "%~dp0ControllerFix.py"
pause
