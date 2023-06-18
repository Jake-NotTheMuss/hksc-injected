@echo off

setlocal EnableDelayedExpansion EnableExtensions

set HKSC="%HKSCI%" -L --file-prefix-map=%hksc_srcdir%/test/=

for %%f in (%hksc_srcdir%/test/*.lua) do (
	if /i "%%~xf"==".lua" (
		echo %%f | findstr /R "^[0-9][0-9].*$" >NUL 2>&1 && (
			rem compile with T7
			%HKSC% --game=t7 %hksc_srcdir%/test/%%~f ^
			--debugfile=%hksc_testdir%/t7/test/%%~nf.debugexpect ^
			--callstackdb=%hksc_testdir%/t7/test/%%~nf.profileexpect ^
			-o %hksc_testdir%/t7/test/%%~nf.cexpect
			rem compile with T6
			%HKSC% --game=t6 %hksc_srcdir%/test/%%~f ^
			--debugfile=%hksc_testdir%/t6/test/%%~nf.debugexpect ^
			--callstackdb=%hksc_testdir%/t6/test/%%~nf.profileexpect ^
			-o %hksc_testdir%/t6/test/%%~nf.cexpect
			rem compile with Sekiro (enables memoization and structures)
			%HKSC% --game=sekiro %hksc_srcdir%/test/%%~f ^
			--debugfile=%hksc_testdir%/s/test/%%~nf.debugexpect ^
			--callstackdb=%hksc_testdir%/s/test/%%~nf.profileexpect ^
			-o %hksc_testdir%/s/test/%%~nf.cexpect
		)
		if /i "%%~nf"=="ui64" (
			%HKSC% --game=t7 %hksc_srcdir%/test/%%~f ^
			--debugfile=%hksc_testdir%/t7/test/%%~nf.debugexpect ^
			--callstackdb=%hksc_testdir%/t7/test/%%~nf.profileexpect ^
			-o %hksc_testdir%/t7/test/%%~nf.cexpect
		)
		echo %%~xf | findstr /R "^s_.*$" >NUL 2>&1 && (
			%HKSC% --game=sekiro %hksc_srcdir%/test/%%~f ^
			--debugfile=%hksc_testdir%/s/test/%%~nf.debugexpect ^
			--callstackdb=%hksc_testdir%/s/test/%%~nf.profileexpect ^
			-o %hksc_testdir%/s/test/%%~nf.cexpect
		)
	)
)


set HKSC="%HKSCI%" --expect-error --file-prefix-map=%hksc_srcdir%/test/error/=

for %%f in (%hksc_srcdir%/test/error/*.lua) do (
	if /i "%%~xf"==".lua" (
		if /i "%%~nf"=="literaloverflow" (
			set hksc_L=-L
		) else (
			set hksc_L=
		)
		rem compile with T7
		%HKSC% --game=t7 %hksc_L% %hksc_srcdir%/test/error/%%~f ^
		-o %hksc_testdir%/t7/test/error/%%~nf.expect
		rem compile with T6
		%HKSC% --game=t6 %hksc_L% %hksc_srcdir%/test/error/%%~f ^
		-o %hksc_testdir%/t6/test/error/%%~nf.expect
		rem compile with Sekiro
		if /i not "%%~nf"=="hmake" (
		if /i not "%%~nf"=="hstructure" (
		if /i not "%%~nf"=="typedparam" (
		if /i not "%%~nf"=="typedvar" (
		%HKSC% --game=sekiro %hksc_L% %hksc_srcdir%/test/error/%%~f ^
		-o %hksc_testdir%/s/test/error/%%~nf.expect
		))))
	)
)

exit /b 0
