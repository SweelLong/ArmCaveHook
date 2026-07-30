@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not "%~1"=="" (
    echo Error: build.bat does not accept arguments; edit armcave.conf. 1>&2
    exit /b 1
)

set "conf=armcave.conf"
set "profiles="
set "ran_profile="

if not exist "%conf%" (
    echo Error: %conf% was not found. 1>&2
    exit /b 1
)

:: Ensure ArmCaveHook-Arcplugins submodule is present
if not exist "ArmCaveHook-Arcplugins\.git" (
    git submodule update --init --depth 1 2>nul
    if errorlevel 1 (
        echo Error: ArmCaveHook-Arcplugins submodule not found. Run: git submodule update --init 1>&2
        exit /b 1
    )
)

call :read_conf
call set "build_dir=%%cfg.build_dir%%"
if not defined build_dir (echo Error: build_dir is not set in %conf%. 1>&2& exit /b 1)

where cmake >nul 2>nul || (echo Error: cmake was not found in PATH. 1>&2& exit /b 1)
where clang >nul 2>nul || (echo Error: clang was not found in PATH; install LLVM. 1>&2& exit /b 1)
where clang++ >nul 2>nul || (echo Error: clang++ was not found in PATH; install LLVM. 1>&2& exit /b 1)

cmake -S . -B "%build_dir%"
if errorlevel 1 exit /b 1
cmake --build "%build_dir%" --config Release
if errorlevel 1 exit /b 1

set "cli=%build_dir%\armcave.exe"
if not exist "%cli%" set "cli=%build_dir%\Release\armcave.exe"
if not exist "%cli%" (echo Error: built CLI not found under %build_dir%. 1>&2& exit /b 1)

for %%P in (%profiles%) do (
    if /i "!cfg.%%P.enable!"=="true" (
        call :run_profile "%%P"
        if errorlevel 1 exit /b 1
    )
)
if not defined ran_profile (
    echo Error: No enabled profiles found in %conf% ^(set enable = true in a [section]^) 1>&2
    exit /b 1
)
echo All profiles completed.
exit /b 0

:read_conf
set "current_profile="
for /f "usebackq tokens=1,* delims==" %%A in ("%conf%") do (
    set "key=%%A"
    set "value=%%B"
    call :trim key
    call :trim value
    if defined key if not "!key:~0,1!"=="#" (
        if "!key:~0,1!"=="[" (
            set "current_profile=!key:~1,-1!"
            set "profiles=!profiles! !current_profile!"
        ) else if defined current_profile (
            set "cfg.!current_profile!.!key!=!value!"
        ) else (
            set "cfg.!key!=!value!"
        )
    )
)
exit /b 0

:trim
set "trim_value=!%~1!"
for /f "tokens=* delims= " %%T in ("!trim_value!") do set "trim_value=%%T"
:trim_right
if "!trim_value:~-1!"==" " (
    set "trim_value=!trim_value:~0,-1!"
    goto trim_right
)
set "%~1=!trim_value!"
exit /b 0

:run_profile
set "profile=%~1"
set "input="
set "output="
set "plugins="
set "wl="
set "bl="
call set "input=%%cfg.%profile%.input%%"
call set "output=%%cfg.%profile%.output%%"
call set "plugins=%%cfg.%profile%.plugins%%"
call set "wl=%%cfg.%profile%.plugin_whitelist%%"
call set "bl=%%cfg.%profile%.plugin_blacklist%%"

if not defined input (echo Error: input is not set in [%profile%] section of %conf%. 1>&2& exit /b 1)
if not defined output (echo Error: output is not set in [%profile%] section of %conf%. 1>&2& exit /b 1)
if not defined plugins (echo Error: plugins is not set in [%profile%] section of %conf%. 1>&2& exit /b 1)

echo === ArmCaveHook: [%profile%] ===
echo   input:   %input%
echo   output:  %output%
echo   plugins: %plugins%

if defined wl (
    if defined bl (
        "%cli%" "%input%" -o "%output%" --plugins "%plugins%" --plugin-whitelist "%wl%" --plugin-blacklist "%bl%"
    ) else (
        "%cli%" "%input%" -o "%output%" --plugins "%plugins%" --plugin-whitelist "%wl%"
    )
) else (
    if defined bl (
        "%cli%" "%input%" -o "%output%" --plugins "%plugins%" --plugin-blacklist "%bl%"
    ) else (
        "%cli%" "%input%" -o "%output%" --plugins "%plugins%"
    )
)
if errorlevel 1 exit /b 1
set "ran_profile=1"
echo.
exit /b 0
