@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not "%~1"=="" (
    echo Error: build.bat does not accept arguments; edit armcave.conf. 1>&2
    exit /b 1
)

set "conf=armcave.conf"
set "input="
set "output="
set "plugins="
set "wl="
set "bl="
set "build_dir="

if not exist "%conf%" (
    echo Error: %conf% was not found. 1>&2
    exit /b 1
)
call :read_conf
if not defined input (
    echo Error: input is not set in %conf%. 1>&2
    exit /b 1
)
if not defined output (echo Error: output is not set in %conf%. 1>&2& exit /b 1)
if not defined plugins (echo Error: plugins is not set in %conf%. 1>&2& exit /b 1)
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

if defined wl if defined bl goto run_both
if defined wl goto run_whitelist
if defined bl goto run_blacklist
"%cli%" "%input%" -o "%output%" --plugins "%plugins%"
exit /b %errorlevel%

:run_both
"%cli%" "%input%" -o "%output%" --plugins "%plugins%" --plugin-whitelist "%wl%" --plugin-blacklist "%bl%"
exit /b %errorlevel%

:run_whitelist
"%cli%" "%input%" -o "%output%" --plugins "%plugins%" --plugin-whitelist "%wl%"
exit /b %errorlevel%

:run_blacklist
"%cli%" "%input%" -o "%output%" --plugins "%plugins%" --plugin-blacklist "%bl%"
exit /b %errorlevel%

:read_conf
for /f "usebackq tokens=1,* delims==" %%a in ("%conf%") do (
    for /f "tokens=*" %%k in ("%%a") do set "key=%%k"
    for /f "tokens=*" %%v in ("%%b") do set "value=%%v"
    if /i "!key!"=="input" set "input=!value!"
    if /i "!key!"=="output" set "output=!value!"
    if /i "!key!"=="plugins" set "plugins=!value!"
    if /i "!key!"=="plugin_whitelist" set "wl=!value!"
    if /i "!key!"=="plugin_blacklist" set "bl=!value!"
    if /i "!key!"=="build_dir" set "build_dir=!value!"
)
exit /b 0
