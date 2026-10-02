@echo off
rem protoST venv activate -- cmd.exe: `bin\activate.bat`
if defined _OLD_PATH set "PATH=%_OLD_PATH%"
if not defined _OLD_PATH set "_OLD_PATH=%PATH%"
set "STENV=@VENV_PATH@"
set "PATH=%STENV%\bin;%PATH%"
if defined _OLD_PROMPT set "PROMPT=%_OLD_PROMPT%"
if not defined PROMPT set "PROMPT=$P$G"
set "_OLD_PROMPT=%PROMPT%"
set "PROMPT=(protoST) %PROMPT%"
