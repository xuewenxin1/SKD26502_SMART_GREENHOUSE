@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem 从工程 build 同步 BIN 到本烧录包（统一改名为带 motor/sensor 标识）
set "HERE=%~dp0"
set "ROOT=%HERE%.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"

set "MOTOR_DIR=%ROOT%\03.software\02.source_code\04.controller_unit_motor_freertos\build"
set "SENSOR_DIR=%ROOT%\03.software\02.source_code\04.controller_unit_sensor_freertos\build"

set "MOTOR_OK=0"
set "SENSOR_OK=0"

echo 同步 BIN 到烧录包...

if exist "%MOTOR_DIR%\SKD26502_SMART_GREENHOUSE_motor.bin" (
  copy /Y "%MOTOR_DIR%\SKD26502_SMART_GREENHOUSE_motor.bin" "%HERE%SKD26502_motor.bin" >nul
  set "MOTOR_OK=1"
) else if exist "%MOTOR_DIR%\SKD26502_SMART_GREENHOUSE.bin" (
  copy /Y "%MOTOR_DIR%\SKD26502_SMART_GREENHOUSE.bin" "%HERE%SKD26502_motor.bin" >nul
  set "MOTOR_OK=1"
)

if exist "%SENSOR_DIR%\SKD26502_SMART_GREENHOUSE_sensor.bin" (
  copy /Y "%SENSOR_DIR%\SKD26502_SMART_GREENHOUSE_sensor.bin" "%HERE%SKD26502_sensor.bin" >nul
  set "SENSOR_OK=1"
) else if exist "%SENSOR_DIR%\SKD26502_SMART_GREENHOUSE.bin" (
  copy /Y "%SENSOR_DIR%\SKD26502_SMART_GREENHOUSE.bin" "%HERE%SKD26502_sensor.bin" >nul
  set "SENSOR_OK=1"
)

if "%MOTOR_OK%"=="1" (echo [OK] SKD26502_motor.bin) else (echo [缺] motor bin)
if "%SENSOR_OK%"=="1" (echo [OK] SKD26502_sensor.bin) else (echo [缺] sensor bin)

echo.
dir /b "%HERE%SKD26502_*.bin" 2>nul
echo.
pause
