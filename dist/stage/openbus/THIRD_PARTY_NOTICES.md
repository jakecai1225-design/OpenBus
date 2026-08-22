# THIRD-PARTY NOTICES — openbus

本文件列出 openbus 发行包内随附的第三方组件及其许可证（打包方案 §9）。
openbus 自身以 MIT 许可发布（见 LICENSE.txt）。

## Qt 6（Qt6Core/Gui/Widgets/Network/Svg/PrintSupport + platforms/ 等插件）

- 版本：6.8.3（MinGW 64-bit 动态链接）
- 许可：GNU LGPL v3（开源版）
- 主页：https://www.qt.io
- 合规：以动态链接 DLL 分发；用户可整体替换本目录内 Qt DLL（便携版天然满足
  LGPL 的可替换性要求）。完整许可文本：
  https://www.gnu.org/licenses/lgpl-3.0.html
  对应源码获取：https://download.qt.io/archive/qt/

## MinGW 运行时（libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll）

- 来源：GCC 13.1.0（windeployqt --compiler-runtime 部署）
- 许可：GPL + GCC Runtime Library Exception
- 声明：https://www.gnu.org/licenses/gcc-exception-3.1-faq.html

## QCustomPlot（编译内嵌于 openbus.exe / openbus_graphic.dll）

- 版本：2.x（third_party/qcustomplot，源码内联静态链接）
- 许可：GNU GPL v3（或商业许可）
- 版权：Copyright (C) 2011-2022 Emanuel Eichhammer
- 主页：https://www.qcustomplot.com

## vector_blf（编译内嵌于 openbus_data.dll）

- 许可：GPL-3.0-or-later
- 主页：https://github.com/TobiasErbsland/vector_blf

## spdlog（编译内嵌）

- 许可：MIT（Copyright (c) 2016 Gabi Melman）
- 主页：https://github.com/gabime/spdlog

## nlohmann_json（编译内嵌）

- 许可：MIT（Copyright (c) 2013-2022 Niels Lohmann）
- 主页：https://github.com/nlohmann/json

## pugixml（编译内嵌）

- 许可：MIT（Copyright (c) 2006-2023 Arseny Kapoulkine）
- 主页：https://github.com/zeux/pugixml

## dbcppp（编译内嵌）

- 许可：MIT（Copyright (c) 2020 xR3b0rn）
- 主页：https://github.com/xR3b0rn/dbcppp

## moodycamel ConcurrentQueue（编译内嵌）

- 许可：Simplified BSD（亦可依 Boost Software License / zlib 许可选其一）
- 主页：https://github.com/cameron314/concurrentqueue

## Python 运行时（runtime/python/）

- 版本：3.14.x（python.org 官方发行版精简拷贝，含 PSF 许可文本
  runtime/python/LICENSE.txt）
- 许可：Python Software Foundation License
- 主页：https://www.python.org
- 声明：仅侧行内插件宿主使用，不写系统 PATH / 不写注册表，与用户自装
  Python 互不影响（可用环境变量 SIN_PYTHON 替换）。

## PyQt6（runtime/python/Lib/site-packages/PyQt6/）

- 版本：见 runtime/python/Lib/site-packages/requirements-lock.txt
- 许可：GNU GPL v3（或 Riverbank 商业许可）
- 版权：Copyright (c) 2025 Riverbank Computing Limited
- 主页：https://www.riverbankcomputing.com/software/pyqt
- 声明：仅供 uds-diagnostic 插件使用。随包分发 GPL 副本本身合法；
  若基于 PyQt6 开发自有插件并对外分发，请按 Riverbank 授权条款自行
  评估 GPL 或购买商业授权。

## ZLG 组件（不随包声明）

- 版权：广州致远电子股份有限公司（保留所有权利）。
- 声明：zlgcan.dll 及其配套（kerneldlls/ 设备属性配置、ZPS 协议栈等）
  均不随本包分发——授权随硬件/官方驱动包，请安装 ZCANPRO 获取
  （README-PORTABLE.txt 有安装引导）；未安装时仅 ZLG 设备不可用。
