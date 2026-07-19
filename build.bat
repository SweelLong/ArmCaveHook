@echo off
setlocal enabledelayedexpansion

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

set "extra="
if defined wl set "extra=--plugin-whitelist %wl%"
if defined bl set "extra=%extra% --plugin-blacklist %bl%"

python armcave.py "%input%" -o "%output%" --plugins "%plugins%" %extra%
pause
