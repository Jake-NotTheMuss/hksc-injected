@echo off

setlocal

cd "%~dp0"

set top_srcdir=%cd%

set nmakeflags=/C /f "%top_srcdir%\src\Makefile.msc" "srcdir=%top_srcdir%\src"

rem  Search for Visual C++ tools on the system
set ProgramFilesX86=%ProgramFiles(x86)%
if not exist "%ProgramFilesX86%" set ProgramFilesX86=%ProgramFiles%

set vs_where=%ProgramFilesX86%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%vs_where%" goto FAIL

for /f "usebackq tokens=1* delims=: " %%i in (`"%vs_where%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64`) do (
	if /i "%%i"=="installationPath" set VS_InstallDir=%%j
)

if "%VS_InstallDir%"=="" (
	echo.
	echo Visual Studio is detected but no suitable installation was found.
	echo.
	exit /b 1
)

set VCVARS=%VS_InstallDir%\VC\Auxiliary\Build\vcvarsall.bat
if not exist "%VCVARS%" (
	echo "%VCVARS%" not found
	exit /b 1
)

if not exist bin mkdir bin
cd bin

rem Build 64-bit binaries
setlocal
call "%VCVARS%" x64 > NUL

if "%1" == "clean" (
	nmake %nmakeflags% %*
	goto DONE
)

nmake %nmakeflags% hksc.exe compiler_t7.dll %*

endlocal

if %errorlevel% neq 0 goto DONE

rem Build 32-bit binaries
setlocal
call "%VCVARS%" x86 > NUL

nmake %nmakeflags% compiler_t6.dll %*

endlocal

:DONE
exit /b %errorlevel%
