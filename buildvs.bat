@echo off

setlocal

set COMPILECMD=cl /nologo /MD /O2 /W3 /c /D_CRT_SECURE_NO_DEPRECATE /DWITH_LOGGING
set LINKCMD=link /nologo

set EXITSTATUS=0

set prefix=%~dp0
set bindir=%prefix%bin
set srcdir=%prefix%src
set includedir=%prefix%include

rem create installation directories

if not exist "%bindir%\x86" mkdir "%bindir%\x86"
if not exist "%bindir%\x64" mkdir "%bindir%\x64"

echo installing binaries to `%bindir%'

set ProgramFilesX86=%ProgramFiles(x86)%
if not exist "%ProgramFilesX86%" set ProgramFilesX86=%ProgramFiles%

set vs_where=%ProgramFilesX86%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%vs_where%" goto FAIL

for /f "usebackq tokens=1* delims=: " %%i in (`"%vs_where%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) do (
	if /i "%%i"=="installationPath" set VS_InstallDir=%%j
)

echo VS_InstallDir="%VS_InstallDir%"

if "%VS_InstallDir%"=="" (
	echo.
	echo Visual Studio is detected but no suitable installation was found.
	echo.
	goto FAIL
)

set VCVARS=%VS_InstallDir%\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" (
	echo "%VCVARS%" not found
	goto FAIL
)

pushd "%srcdir%"

echo.

rem build 64-bit DLL
setlocal
echo calling "%VCVARS%" x64
call "%VCVARS%" x64 > NUL

REM try the c++ compiler
cl 2> NUL 1>&2
if not errorlevel 0 (
	echo Visual Studio C/C++ Compiler not found
	goto FAIL
)


REM build the DLL

%COMPILECMD% /DLUA_CODT7 compiler.c
if %errorlevel% equ 0 (
%LINKCMD% /DLL /out:"%bindir%\x64\LuaCompiler.dll" compiler.obj
)
if %errorlevel% neq 0 goto FAIL

endlocal

echo.

rem build 64-bit DLL
setlocal
echo calling "%VCVARS%" x86
call "%VCVARS%" x86 > NUL

REM try the c++ compiler
cl 2> NUL 1>&2
if not errorlevel 0 (
	echo Visual Studio C/C++ Compiler not found
	goto FAIL
)

REM build the DLL

%COMPILECMD% /DLUA_CODT6 compiler.c
if %errorlevel% equ 0 (
%LINKCMD% /DLL /out:"%bindir%\x86\LuaCompiler.dll" compiler.obj
)
if %errorlevel% neq 0 goto FAIL

endlocal

del compiler.obj 2>NUL

echo.

echo calling "%VCVARS%" any
call "%VCVARS%" {empty}

REM try the c++ compiler
cl 2> NUL 1>&2
if not errorlevel 0 (
	echo Visual Studio C/C++ Compiler not found
	goto FAIL
)

rem build the injector program

%COMPILECMD% hksc.c
if %errorlevel% equ 0 (
%LINKCMD% /out:"%bindir%\hksc.exe" hksc.obj
)

del *.obj

popd

exit /b %EXITSTATUS%

:FAIL
exit /b 1
