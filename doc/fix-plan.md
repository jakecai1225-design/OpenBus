# ==============================================
#  QML MenuBar 集成 - 完整修复方案
# ==============================================

【编译问题解决】
------------------------
1. root/CMakeLists.txt:find_package 增加 QuickWidgets
2. src/CMakeLists.txt:openbus_data 增加 QuickWidgets
3. src/CMakeLists.txt:openbus_qml_menu 增加 QuickWidgets
4. src/CMakeLists.txt:openbus_ui 保留 QuickWidgets 依赖

【代码质量问题修复】
------------------------
5. mainwindow_qmlmenu.cpp:修正 qmlRegisterType 调用
6. mainwindow_qmlmenu.cpp:连接信号槽
7. mainwindow_qmlmenu.cpp:QML 布局集成到 QMainWindow
8. mainwindow_qmlmenu.cpp:remove 私有 slot
9. Main.qml:移除重复实例化
10. build.py deploy:补充 QML 模块部署

【优先级排序】
------------------------
高 (Blocking): #1-#4 → 编译成功
中 (Quality):  #5-#8 → 功能正常
低 (Deploy):  #9-#10 → 运行时正确
