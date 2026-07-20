@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CONF=armcave.conf"
if not exist "%CONF%" (
    echo Error: %CONF% not found.
    pause
    exit /b 1
)

for /f "tokens=1,* delims==" %%a in ('findstr /b "input" "%CONF%"') do set "input=%%b"
for /f "tokens=1,* delims==" %%a in ('findstr /b "output" "%CONF%"') do set "output=%%b"
for /f "tokens=1,* delims==" %%a in ('findstr /b "plugins" "%CONF%"') do set "plugins=%%b"
for /f "tokens=1,* delims==" %%a in ('findstr /b "plugin_whitelist" "%CONF%"') do set "wl=%%b"
for /f "tokens=1,* delims==" %%a in ('findstr /b "plugin_blacklist" "%CONF%"') do set "bl=%%b"
if "%plugins%"=="" set "plugins=plugins"

for /f "tokens=*" %%a in ("%input%") do set "input=%%a"
for /f "tokens=*" %%a in ("%output%") do set "output=%%a"
for /f "tokens=*" %%a in ("%plugins%") do set "plugins=%%a"
for /f "tokens=*" %%a in ("%wl%") do set "wl=%%a"
for /f "tokens=*" %%a in ("%bl%") do set "bl=%%a"

if "%input%"=="" (
    echo Error: input not set in %CONF%.
    pause
    exit /b 1
)
if "%output%"=="" (
    echo Error: output not set in %CONF%.
    pause
    exit /b 1
)

cmake -S . -B build
if errorlevel 1 (
    echo Error: CMake configuration failed.
    pause
    exit /b 1
)

cmake --build build --config Release
if errorlevel 1 (
    echo Error: C++ build failed.
    pause
    exit /b 1
)

set "cli=build\armcave.exe"
if not exist "%cli%" set "cli=build\Release\armcave.exe"
if not exist "%cli%" (
    echo Error: armcave executable not found after build.
    pause
    exit /b 1
)

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
pause
