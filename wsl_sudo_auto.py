#!/usr/bin/env python3
"""
WSL sudo 自动输入密码脚本
用法: python3 wsl_sudo_auto.py <command>
例如: python3 wsl_sudo_auto.py "apt update"

或者使用内置的 sudo -S 方法:
  wsl -d Ubuntu-22.04 -- bash -c "echo '123456' | sudo -S <command>"
"""

import sys
import pexpect
import subprocess
import os

PASSWORD = "123456"

def run_sudo_command(command):
    """使用 sudo -S 方法运行命令"""
    full_command = f"sudo -S {command}"
    result = subprocess.run(
        f'wsl -d Ubuntu-22.04 -- bash -c "echo \\'{PASSWORD}\\' | {full_command}"',
        shell=True,
        capture_output=True,
        text=True
    )
    return result.returncode, result.stdout, result.stderr

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python3 wsl_sudo_auto.py <command>")
        print("例如: python3 wsl_sudo_auto.py \"apt update\"")
        print("")
        print("或者直接使用bash:")
        print("  wsl -d Ubuntu-22.04 -- bash -c \"echo '123456' | sudo -S <command>\"")
        sys.exit(1)
    
    command = " ".join(sys.argv[1:])
    print(f"执行: sudo {command}")
    
    code, stdout, stderr = run_sudo_command(command)
    print(stdout)
    if stderr:
        print(stderr, file=sys.stderr)
    sys.exit(code)
