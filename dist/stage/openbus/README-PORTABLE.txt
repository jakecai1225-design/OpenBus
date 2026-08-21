openbus 便携版说明
==================

解压即用：无需安装 Qt / MinGW / Python 或任何其他软件，双击 openbus.exe 即可运行。

数据位置
--------
配置、会话、日志全部保存在 %APPDATA%\openbus\，程序目录只读也能正常
运行（可放 U 盘 / 任意目录）。卸载 = 删除整个目录，用户数据不受影响。

硬件驱动（可选，不装不影响软件本身）
------------------------------------
- ZLG 设备    安装 ZCANPRO：https://www.zlg.cn（本包不带 zlgcan.dll）
- PEAK 设备   安装 PCAN 驱动：https://www.peak-system.com
- Kvaser 设备 安装 CANlib：https://www.kvaser.com
- slcan 串口设备无需驱动
未安装对应驱动时，仅该厂商设备不可用，其余功能不受影响。

替换自带组件
------------
- Qt / MinGW 运行时 DLL：可整体替换同版本文件（LGPL 要求保留此能力）
- Python 运行时：设置环境变量 SIN_PYTHON 指向自装解释器可替换自带运行时

首次运行提示
------------
未签名软件可能触发 Windows SmartScreen "已保护你的电脑"，点击
"更多信息 → 仍要运行"即可。

完整第三方组件许可清单见 THIRD_PARTY_NOTICES.md。
