@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "conf=%ARMCAVE_CONF%"
if not defined conf set "conf=armcave.conf"
set "input=%ARMCAVE_INPUT%"
set "output=%ARMCAVE_OUTPUT%"
set "plugins=%ARMCAVE_PLUGINS%"
set "wl=%ARMCAVE_PLUGIN_WHITELIST%"
set "bl=%ARMCAVE_PLUGIN_BLACKLIST%"
set "build_dir=%BUILD_DIR%"
if not defined build_dir set "build_dir=build"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--input" set "input=%~2"& shift& shift& goto parse_args
if /i "%~1"=="--output" set "output=%~2"& shift& shift& goto parse_args
if /i "%~1"=="--plugins" set "plugins=%~2"& shift& shift& goto parse_args
if /i "%~1"=="--plugin-whitelist" set "wl=%~2"& shift& shift& goto parse_args
if /i "%~1"=="--plugin-blacklist" set "bl=%~2"& shift& shift& goto parse_args
if /i "%~1"=="--build-dir" set "build_dir=%~2"& shift& shift& goto parse_args
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
echo Error: unknown option: %~1 1>&2
exit /b 1

:args_done
if exist "%conf%" call :read_conf
if not defined plugins set "plugins=plugins"
if not defined input (
    echo Error: input is not set; use --input, ARMCAVE_INPUT, or %conf%. 1>&2
    exit /b 1
)
if not defined output set "output=%input%.patched"

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
    if /i "!key!"=="input" if not defined input set "input=!value!"
    if /i "!key!"=="output" if not defined output set "output=!value!"
    if /i "!key!"=="plugins" if not defined plugins set "plugins=!value!"
    if /i "!key!"=="plugin_whitelist" if not defined wl set "wl=!value!"
    if /i "!key!"=="plugin_blacklist" if not defined bl set "bl=!value!"
)
exit /b 0

:usage
echo Usage: build.bat [--input path] [--output path] [--plugins dir]
echo                  [--plugin-whitelist names] [--plugin-blacklist names]
echo                  [--build-dir dir]
echo Options override ARMCAVE_* environment variables, then armcave.conf.
exit /b 0
