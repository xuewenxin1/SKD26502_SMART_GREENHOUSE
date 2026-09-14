@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

rem 内部脚本：烧录本目录指定的 BIN。参数1 = bin 完整路径
set "BIN_FILE=%~1"
set "HERE=%~dp0"
set "FLASH_BASE=0x08000000"
set "PYOCD_TARGET=py32f071xb"
set "PORTABLE_PY=%HERE%tools\python-win\python.exe"

if "%BIN_FILE%"=="" (
  echo [错误] 未指定 BIN
  pause
  exit /b 1
)
if not exist "%BIN_FILE%" (
  echo [错误] 找不到：%BIN_FILE%
  pause
  exit /b 1
)

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
  echo [错误] 本文件夹还没装烧录环境。
  echo 请先双击：setup_flash_env.bat
  pause
  exit /b 1
)

goto do_flash

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

:do_flash
call :run_py -m pyocd --version >nul 2>&1
if errorlevel 1 (
  echo 正在安装 pyocd ...
  call :run_py -m pip install -U pyocd
  if errorlevel 1 call :run_py -m pip install -U pyocd -i https://pypi.tuna.tsinghua.edu.cn/simple
)

echo.
echo 烧录文件 : %BIN_FILE%
echo 芯片     : %PYOCD_TARGET%
echo 请确认 ST-Link 已插、板子已上电
echo.

call :run_py -m pyocd load -t %PYOCD_TARGET% --base-address=%FLASH_BASE% --erase=sector "%BIN_FILE%"
if errorlevel 1 (
  echo [失败] 烧录失败。可先运行 setup_flash_env.bat，检查 ST-Link。
  pause
  exit /b 1
)
call :run_py -m pyocd reset -t %PYOCD_TARGET%
echo.
echo [成功] 烧录完成。
pause
exit /b 0
