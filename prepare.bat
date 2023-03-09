@echo off

setlocal

set ProgramFilesX86=%ProgramFiles(x86)%
if not exist "%ProgramFilesX86%" set ProgramFilesX86=%ProgramFiles%

if not defined STEAMLIBRARY (
	set STEAMLIBRARY="%ProgramFilesX86%\Steam"
)

if not exist %STEAMLIBRARY% (
	echo Steam game library not found: %STEAMLIBRARY%
	exit /b 1
)

set CODT6="%STEAMLIBRARY:"=%\steamapps\common\Call of Duty Black Ops II\t6sp.exe"
set CODT7="%STEAMLIBRARY:"=%\steamapps\common\Call of Duty Black Ops III\BlackOps3.exe"

if not exist %CODT6% (
	echo Black Ops 2 not found: %CODT6%
	exit /b 1
)

if not exist %CODT7% (
	echo Black Ops 3 not found: %CODT7%
	exit /b 1
)

for /f "tokens=2 delims=, usebackq" %%i IN (`tasklist /SVC /FO csv /NH /FI "IMAGENAME eq t6sp.exe"`) do (
	goto startcheckt7
)
start "" /b %CODT6%
set pid=0
:checkt6
for /f "tokens=2 delims=, usebackq" %%i IN (`tasklist /SVC /FO csv /NH /FI "IMAGENAME eq t6sp.exe"`) do (
	set pid=%%~i
)
if %pid% neq 0 goto startcheckt7
timeout /t 1 /nobreak >NUL
goto checkt6
:startcheckt7
for /f "tokens=2 delims=, usebackq" %%i IN (`tasklist /SVC /FO csv /NH /FI "IMAGENAME eq BlackOps3.exe"`) do (
	goto donechecks
)
start "" /b %CODT7%
set pid=0
:checkt7
for /f "tokens=2 delims=, usebackq" %%i IN (`tasklist /SVC /FO csv /NH /FI "IMAGENAME eq BlackOps3.exe"`) do (
	set pid=%%~i
)
if %pid% neq 0 goto donechecks
timeout /t 1 /nobreak >NUL
goto checkt7
:donechecks

endlocal

exit /b 0
