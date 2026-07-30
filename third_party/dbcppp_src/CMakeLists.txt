# ============================================================
#  dbcppp 静态库
# ============================================================
#  注意：当前本地 dbcppp_src 源码不完整（缺少 SignalType.h、
#  BitTiming.h、EnvironmentVariable.h 等头文件及其 Impl 源文件）。
#  Phase 2 将补全缺失文件并去 boost 依赖后启用此构建目标。
# ============================================================

# --- Phase 2 待补全 ---
# add_library(dbcppp STATIC
#     src/libdbcppp/NetworkImpl.cpp
#     src/libdbcppp/MessageImpl.cpp
#     src/libdbcppp/SignalImpl.cpp
#     # DBCAST2Network.cpp 排除：依赖 Boost.Spirit X3
#     # 需新增的 Impl 源文件（Phase 2 补全）：
#     # src/libdbcppp/AttributeImpl.cpp
#     # src/libdbcppp/AttributeDefinitionImpl.cpp
#     # src/libdbcppp/NodeImpl.cpp
#     # src/libdbcppp/ValueTableImpl.cpp
#     # src/libdbcppp/BitTimingImpl.cpp
#     # src/libdbcppp/EnvironmentVariableImpl.cpp
#     # src/libdbcppp/SignalTypeImpl.cpp
# )
# target_include_directories(dbcppp PUBLIC include)
# target_compile_features(dbcppp PUBLIC cxx_std_17)
# # tsl::robin_map 依赖
# FetchContent_Declare(
#     robin_map
#     GIT_REPOSITORY https://github.com/Tessil/robin-map.git
#     GIT_TAG        v1.3.0
#     GIT_SHALLOW    TRUE
# )
# FetchContent_MakeAvailable(robin_map)
# target_link_libraries(dbcppp PUBLIC tsl::robin_map)
