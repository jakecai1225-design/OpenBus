# sin

#### 介绍
报文解析，分析，回放，录制，trace，graphic 桌面软件。灵感来源于wireshark，canoe，ozone等优秀软件。

#### 软件架构

基于 **Qt6 + CMake + C++17** 构建的工业级桌面应用程序框架。

```
sin/
├── CMakeLists.txt          # 顶层 CMake 构建配置
├── src/                    # 源代码
│   ├── CMakeLists.txt      # 子模块构建配置
│   ├── main.cpp            # 程序入口
│   └── ui/                 # 界面层
│       ├── mainwindow.h    # 主窗口头文件
│       ├── mainwindow.cpp  # 主窗口实现
│       └── mainwindow.ui   # 主窗口 UI (Qt Designer)
├── resources/              # 资源文件
│   ├── resources.qrc       # Qt 资源集合
│   └── styles/
│       └── default.qss     # 全局样式表
└── build/                  # 构建输出 (gitignore)
```

后续可扩展模块：`src/core`（核心逻辑）、`src/models`（数据模型）、`src/services`（业务服务）、`src/utils`（工具类）。

#### 安装教程

**1. 安装依赖**

- [Qt 6](https://www.qt.io/download-open-source) （安装时勾选 MinGW 或 MSVC 组件）
- [CMake 3.21+](https://cmake.org/download/)

**2. 配置构建**

```bash
# 在项目根目录执行，指定 Qt6 的 CMake 路径
cmake -B build -S . -DCMAKE_PREFIX_PATH="你的Qt安装路径/6.x.x/mingw_64"
```

**3. 编译**

```bash
cmake --build build
```

**4. 运行**

```bash
./build/bin/sin.exe
```

#### 使用说明

1.  启动程序后显示主窗口
2.  点击「点击我」按钮，文本区域将显示 `Hello World!`
3.  后续在此框架基础上扩展报文解析、分析、回放等功能

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request


#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
