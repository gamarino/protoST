@echo off
rem protoST venv deactivate -- cmd.exe: `bin\deactivate.bat`
if defined _OLD_PATH set "PATH=%_OLD_PATH%"
set _OLD_PATH=
if defined _OLD_PROMPT set "PROMPT=%_OLD_PROMPT%"
set _OLD_PROMPT=
set STENV=
