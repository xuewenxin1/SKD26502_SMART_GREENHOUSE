@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem  烧录包专用：环境装在本文件夹 tools\ 里，可整个文件夹拷走使用
rem ============================================================

set "HERE=%~dp0"
cd /d "%HERE%"

set "TOOLS=%HERE%tools"
set "PYDIR=%TOOLS%\python-win"
set "PYEXE=%PYDIR%\python.exe"
set "GETPIP=%TOOLS%\get-pip.py"
set "PYZIP=%TOOLS%\python-embed.zip"
set "PY_VER=3.12.8"
set "PY_URL=https://www.python.org/ftp/python/%PY_VER%/python-%PY_VER%-embed-amd64.zip"
set "GETPIP_URL=https://bootstrap.pypa.io/get-pip.py"

echo.
echo ========== 烧录包环境安装 ==========
echo 安装位置: %TOOLS%
echo.

where curl >nul 2>&1
if errorlevel 1 (
  echo [错误] 需要 Windows 10/11 自带的 curl。
  pause
  exit /b 1
)

if not exist "%TOOLS%" mkdir "%TOOLS%"

if exist "%PYEXE%" (
  echo [跳过] 已有便携 Python
) else (
  echo [下载] Python %PY_VER% ...
  curl -L --retry 3 -o "%PYZIP%" "%PY_URL%"
  if errorlevel 1 (
    echo [错误] 下载失败，请检查网络。
    pause
    exit /b 1
  )
  if exist "%PYDIR%" rmdir /s /q "%PYDIR%"
  mkdir "%PYDIR%"
  powershell -NoProfile -Command "Expand-Archive -LiteralPath '%PYZIP%' -DestinationPath '%PYDIR%' -Force"
  del /f /q "%PYZIP%" >nul 2>&1
  for %%F in ("%PYDIR%\python*._pth") do (
    echo import site>> "%%~fF"
  )
)

"%PYEXE%" --version
if errorlevel 1 (
  echo [错误] Python 无法运行。
  pause
  exit /b 1
)

"%PYEXE%" -m pip --version >nul 2>&1
if errorlevel 1 (
  echo [下载] get-pip.py ...
  curl -L --retry 3 -o "%GETPIP%" "%GETPIP_URL%"
  "%PYEXE%" "%GETPIP%"
)

echo [安装] pyocd ...
"%PYEXE%" -m pip install -U pyocd
if errorlevel 1 "%PYEXE%" -m pip install -U pyocd -i https://pypi.tuna.tsinghua.edu.cn/simple
"%PYEXE%" -m pyocd --version
if errorlevel 1 (
  echo [错误] pyocd 失败。
  pause
  exit /b 1
)

where winget >nul 2>&1
if not errorlevel 1 (
  echo 尝试安装 ST-Link 驱动（STM32CubeProgrammer）...
  winget install -e --id STMicroelectronics.STM32CubeProgrammer --accept-package-agreements --accept-source-agreements
) else (
  echo 若 ST-Link 识别不了，请安装 STM32CubeProgrammer 驱动。
)

echo.
echo [完成] 以后直接双击：
echo   flash_motor.bat   = 烧放风机 SKD26502_motor.bin
echo   flash_sensor.bat  = 烧温湿度计 SKD26502_sensor.bin
echo.
pause
exit /b 0
