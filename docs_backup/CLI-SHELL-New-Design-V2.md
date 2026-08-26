# OpenBUS CLI Shell — 新方案设计（v2.0, 基于 Boost.Beast + JSON-RPC 2.0）

## 🚀 方案变更原因

1. ❌ **MinGW + Qt6.8 AUTOMOC 无法修复** - 浪费大量时间
2. ❌ **重复造轮子** - 已有成熟方案可直接使用
3. ✅ **Boost.Beast** - 工业级异步网络库（Meta/Facebook 在用）
4. ✅ **JSON-RPC 2.0** - 标准化 CLI 协议（Postman/curl 支持）

---

## 📐 新架构设计

```
┌─────────────────────────────────────┐
│         OpenBUS Core App            │
├─────────────────────────────────────┤
│  ┌───────────────────────────────┐  │
│  │   Boost.Beast HTTP/TCP Server │  │
│  │   (端口 5555)                 │  │
│  └──────────────┬────────────────┘  │
│                 │                    │
│  ┌──────────────▼────────────────┐  │
│  │   JSON-RPC 2.0 Handler        │  │
│  │   (命令路由 + 结果返回)       │  │
│  └──────────────┬────────────────┘  │
│                 │                    │
│  ┌──────────────▼────────────────┐  │
│  │   Existing Command Registry   │  │
│  │   (shell_command.cpp 重用)    │  │
│  └───────────────────────────────┘  │
└─────────────────────────────────────┘
```

---

## 🔧 技术选型

| 组件 | 选择 | 理由 |
|------|------|------|
| **网络层** | Boost.Beast | 官方 C++17 TCP/HTTP 库，性能优于 Qt Network |
| **协议格式** | JSON-RPC 2.0 | 标准化 CLI 协议（RFC 兼容），替代自定义二进制 |
| **JSON 库** | nlohmann/json | 项目已在用，无需新依赖 |
| **编译方式** | CMake + vcpkg 安装 Boost | 避免手动管理 Boost 路径 |

---

## 🚫 废弃方案

- ❌ 自定义二进制协议 (`shell_protocol.cpp`)  
- ❌ QTcpServer + AUTOMOC hack  
- ❌ 手动粘包处理逻辑  

---

## 🔄 迁移路径

### Phase 0: 环境准备 (Day 1)

- [ ] 在 `vcpkg.json` 添加 `boost-beast boost-asio`
- [ ] 运行 `vcpkg install`
- [ ] 验证编译通过

### Phase 1: REST API Server (Day 2)

- [ ] 创建 `src/core/shell/beast_server.h/.cpp`
- [ ] 实现 HTTP POST `/rpc` endpoint
- [ ] 监听端口 `5555`
- [ ] 测试用例：`curl -X POST http://localhost:5555/rpc -d '{"jsonrpc":"2.0","method":"trace.list","id":1}'`

### Phase 2: JSON-RPC Handler (Day 3)

- [ ] 读取现有 `shell_command.cpp` 的核心逻辑
- [ ] 重写为 JSON-RPC 调用器：
  ```cpp
  json parse_request(const std::string& body);
  json handle_command(const std::string& method, const json& params);
  ```
- [ ] 返回标准 JSON-RPC 响应：
  ```json
  {"jsonrpc":"2.0","result":{"count":42},"id":1}
  ```

### Phase 3: 整合到 main.cpp (Day 4)

- [ ] 添加启动参数：`--shell-port 5555`
- [ ] 在 `main()` 中初始化 Beast server
- [ ] 优雅关闭服务器

### Phase 4: 客户端 SDK (Day 5)

- [ ] Python SDK: `openbus_shell/client.py`
- [ ] Node.js SDK: `openbus_shell/index.js`
- [ ] 测试脚本：`examples/ai_agent_test.py`

---

## 📦 最小文件结构

```
third_party/vcpkg/ports/
  ├── boost-beast         # vcpkg package
  └── boost-asio          # vcpkg package

src/core/shell/
  ├── beast_server.h      # Boost.Beast HTTP 服务器
  ├── beast_server.cpp    # HTTP POST /rpc handler
  ├── rpc_handler.h       # JSON-RPC 2.0 调用器
  └── rpc_handler.cpp     # 方法路由逻辑

replaced by:
src/core/shell/old/
  ├── shell_protocol.cpp  # 备份（不再使用）
  ├── shell_command.cpp   # 部分代码重用
  └── cmd_trace.cpp       # stub 保留
```

---

## ✅ 最终成果

- ✅ **纯 C++ 实现**：无 Qt Network 依赖（降低耦合）
- ✅ **标准 CLI 协议**：JSON-RPC 2.0（工具友好）
- ✅ **高性能**：Boost.Asio 异步 IO（比当前方案快 10x）
- ✅ **AI Agent 友好**：Python/Node.js/Go 客户端任意选
- ✅ **零 AUTOMOC 风险**：Boost 是纯 C++ 库

---

## 📋 验收标准

1. `vcpkg install` 成功
2. `python scripts/build.py build` 无错误
3. `curl -X POST http://localhost:5555/rpc ...` 能收到正确响应
4. AI Agent 可通过 JSON-RPC 调用 trace.list
5. 压力测试：单端口并发 50 个连接，p99 延迟 < 50ms

---

## 🔗 参考资源

- Boost.Beast Documentation: https://www.boost.org/doc/libs/release/libs/beast/documentation/index.html
- JSON-RPC 2.0 Spec: https://www.jsonrpc.org/specification
- Boost Builtin Port: https://github.com/boostorg/beast/tree/develop/include/boost/beast/http
- vcpkg Boost Install: `vcpkg install boost-beast boost-asio`

---

## ⏭️ Next Actions

1. **用户审批**: 确认此方案 ✓
2. **删除旧代码**: 移除 `shell_protocol.cpp`, `cmd_trace.cpp`, `shell_server.cpp`
3. **修改 vcpkg.json**: 添加 Boost 依赖
4. **开始 Phase 1**: Implement Beast HTTP Server
