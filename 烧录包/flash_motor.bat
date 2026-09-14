@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem 本目录一键烧：SKD26502_motor.bin（ST-Link）
cd /d "%~dp0"

if not exist "%~dp0SKD26502_motor.bin" (
  echo [错误] 同目录下没有 SKD26502_motor.bin
  echo 请先运行 sync_bins_from_build.bat，或把 motor 的 bin 拷进来并改成这个名字。
  pause
  exit /b 1
)

call "%~dp0_flash_one.bat" "%~dp0SKD26502_motor.bin"
