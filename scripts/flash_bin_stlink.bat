@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem  Windows + ST-Link 烧 BIN（优先用 tools\ 便携环境）
rem ============================================================

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
for %%I in ("%REPO_ROOT%") do set "REPO_ROOT=%%~fI"

set "FLASH_BASE=0x08000000"
set "PYOCD_TARGET=py32f071xb"
set "PORTABLE_PY=%REPO_ROOT%\tools\python-win\python.exe"

set "MOTOR_BIN=%REPO_ROOT%\03.software\02.source_code\04.controller_unit_motor_freertos\build\SKD26502_SMART_GREENHOUSE.bin"
set "SENSOR_BIN=%REPO_ROOT%\03.software\02.source_code\04.controller_unit_sensor_freertos\build\SKD26502_SMART_GREENHOUSE.bin"

set "USE_PORTABLE=0"
if exist "%PORTABLE_PY%" set "USE_PORTABLE=1"

if "%USE_PORTABLE%"=="0" (
  where py >nul 2>&1
  if not errorlevel 1 (
    py -3 -c "import sys" >nul 2>&1
    if not errorlevel 1 set "USE_PORTABLE=2"
  )
)
if "%USE_PORTABLE%"=="0" (
  where python >nul 2>&1
  if not errorlevel 1 set "USE_PORTABLE=3"
)

if "%USE_PORTABLE%"=="0" (
  echo.
  echo [错误] 烧录环境未安装。
  echo 电脑什么都没装时，请先双击仓库根目录：
  echo   setup_flash_env.bat
  echo （需联网，自动下载便携 Python + pyocd）
  echo.
  pause
  exit /b 1
)

goto check_pyocd

:run_py
if "%USE_PORTABLE%"=="1" (
  "%PORTABLE_PY%" %*
  exit /b !errorlevel!
)
if "%USE_PORTABLE%"=="2" (
  py -3 %*
  exit /b !errorlevel!
)
python %*
exit /b %errorlevel%

:check_pyocd
call :run_py -m pyocd --version >nul 2>&1
if errorlevel 1 (
  echo 正在自动安装 pyocd ...
  call :run_py -m pip install -U pyocd
  if errorlevel 1 call :run_py -m pip install -U pyocd -i https://pypi.tuna.tsinghua.edu.cn/simple
  call :run_py -m pyocd --version >nul 2>&1
  if errorlevel 1 (
    echo [错误] pyocd 不可用。请重新运行 setup_flash_env.bat
    pause
    exit /b 1
  )
)

set "BIN_FILE="
set "ARG=%~1"
if /I "%ARG%"=="motor"  set "BIN_FILE=%MOTOR_BIN%"
if /I "%ARG%"=="sensor" set "BIN_FILE=%SENSOR_BIN%"
if /I "%ARG%"=="m"      set "BIN_FILE=%MOTOR_BIN%"
if /I "%ARG%"=="s"      set "BIN_FILE=%SENSOR_BIN%"
if not defined BIN_FILE if not "%~1"=="" if exist "%~1" set "BIN_FILE=%~f1"

if not defined BIN_FILE (
  echo.
  echo  ========== Windows ST-Link 烧录 ==========
  echo   [1] 放风机 motor
  echo   [2] 温湿度计 sensor
  echo   [3] 手动输入 BIN 路径
  echo   [Q] 退出
  echo  ========================================
  echo.
  set /p "CHOICE=请选择: "
  if /I "!CHOICE!"=="1" set "BIN_FILE=%MOTOR_BIN%"
  if /I "!CHOICE!"=="2" set "BIN_FILE=%SENSOR_BIN%"
  if /I "!CHOICE!"=="3" set /p "BIN_FILE=BIN 完整路径: "
  if /I "!CHOICE!"=="Q" exit /b 0
  if not defined BIN_FILE (
    echo 无效选择。
    pause
    exit /b 1
  )
)

set "BIN_FILE=%BIN_FILE:"=%"
if not exist "%BIN_FILE%" (
  echo.
  echo [错误] 找不到 BIN：
  echo   %BIN_FILE%
  echo.
  echo 空电脑只能烧「现成的 bin」。请把 .bin 拷到上面路径，
  echo 或： scripts\flash_bin_stlink.bat "你的文件.bin"
  echo.
  pause
  exit /b 1
)

echo.
echo 芯片   : %PYOCD_TARGET%
echo 地址   : %FLASH_BASE%
echo BIN    : %BIN_FILE%
echo 调试器 : ST-Link
echo.
echo 确认 ST-Link 已插、板子已上电后继续...
echo.

call :run_py -m pyocd load -t %PYOCD_TARGET% --base-address=%FLASH_BASE% --erase=sector "%BIN_FILE%"
if errorlevel 1 (
  echo.
  echo [失败] 常见原因：
  echo   1. 没跑过 setup_flash_env.bat
  echo   2. ST-Link 驱动没装（设备管理器有感叹号）
  echo   3. 板子没电 / SWD 线接错
  echo.
  pause
  exit /b 1
)

call :run_py -m pyocd reset -t %PYOCD_TARGET%
if errorlevel 1 (
  echo [警告] 复位失败，可按板上复位键。
) else (
  echo 复位完成。
)

echo.
echo [成功] 烧录完成。
pause
exit /b 0
