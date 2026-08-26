# OpenBUS CLI Shell — 最终实施方案（基于 Qt HTTP Server + JSON-RPC 2.0）

## 🎯 方案变更说明

### 原因回顾
1. ❌ 原始方案失败：MinGW + Qt6.8 AUTOMOC bug 无法修复
2. ❌ Boost.Beast 过于复杂：需要大量配置和依赖管理  
3. ✅ **Qt HTTP Server**：Qt6.6+ 内置组件，无需额外库

---

## 🔧 技术选型（最终版）

| 组件 | 选择 | 理由 |
|------|------|------|
| **TCP Server** | QTcpServer | Qt6 已集成，稳定成熟 |
| **HTTP Server** | QHttpServer | Qt6.6+ 新特性，原生支持 |
| **协议格式** | JSON-RPC 2.0 | 标准化，工具友好 |
| **JSON 库** | nlohmann/json | 已在用 |
| **依赖** | **无新增** | 利用现有 Qt Network |

---

## 📐 架构设计

```
┌─────────────────────────────────────┐
│         OpenBUS Core App            │
├─────────────────────────────────────┤
│  ┌───────────────────────────────┐  │
│  │   QHttpServer (port 5555)     │  │
│  │   - POST /rpc endpoint        │  │
│  └──────────────┬────────────────┘  │
│                 │                    │
│  ┌──────────────▼────────────────┐  │
│  │   JSON-RPC Handler            │  │
│  │   (reusing shell_command.cpp) │  │
│  └──────────────┬────────────────┘  │
│                 │                    │
│  ┌──────────────▼────────────────┐  │
│  │   Existing Command Registry   │  │
│  └───────────────────────────────┘  │
└─────────────────────────────────────┘
```

---

## 🔄 迁移路径（Day 1-2）

### Day 1: 清理 + 核心实现 (上午)

- [ ] 删除旧 shell 代码：
  ```bash
  rm -rf src/core/shell/{shell_protocol.*,cmd_trace.*,shell_server.*}
  mv src/core/shell/{rpc_handler.h,rpc_handler.cpp} src/core/shell/new/
  ```

- [ ] 创建新文件 `src/core/shell/http_rpc_server.h/.cpp`：
  ```cpp
  class HttpRpcServer {
      QHttpServer* server_;  // Qt6 原生 HTTP 服务器
      std::unordered_map<std::string, CommandHandler> handlers_;
  public:
      bool start(int port);  // 监听 5555
      void register_handler(const std::string& method, CommandHandler handler);
  };
  ```

- [ ] 重写 `shell_command.cpp` 的核心逻辑为 RPC 形式：
  ```cpp
  json handle_json_rpc(const json& request);  // parse -> execute -> response
  ```

### Day 1: 测试 + 整合 (下午)

- [ ] 单元测试：`test_http_rpc.cpp`
  ```cpp
  // test via curl
  curl -X POST http://localhost:5555/rpc \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","method":"trace.list","params":{},"id":1}'
  ```

- [ ] 整合到 main.cpp：
  ```cpp
  if (args.enable_shell) {
      HttpRpcServer rpc;
      rpc.start(5555);
  }
  ```

### Day 2: Python SDK + 文档

- [ ] `openbus_shell/client.py`:
  ```python
  import requests
  
  class ShellClient:
      def __init__(self, host='localhost', port=5555):
          self.url = f'http://{host}:{port}/rpc'
      
      def call(self, method, params=None):
          payload = {'jsonrpc':'2.0','method':method,'params':params or {},'id':1}
          resp = requests.post(self.url, json=payload)
          return resp.json()['result']
  ```

- [ ] 示例脚本：`examples/ai_agent_analyze.py`

---

## ✅ 验收标准

1. ✅ `python scripts/build.py build` 成功
2. ✅ `curl -X POST http://localhost:5555/rpc ...` 正常响应
3. ✅ Python SDK 可调用 trace.list
4. ✅ AI Agent 可通过 JSON-RPC 控制 OpenBUS

---

## 🚫 废弃内容

- ❌ `shell_protocol.cpp` (自定义二进制协议)
- ❌ `shell_server.cpp` (QTcpServer 尝试)
- ❌ cmd_trace stub

---

## 📦 最小文件结构

```
src/core/shell/
  ├── http_rpc_server.h      # HTTP RPC 服务器（Qt6 原生）
  ├── http_rpc_server.cpp
  ├── rpc_handler.h          # JSON-RPC 处理器
  ├── rpc_handler.cpp
  ├── shell_command.h        # (保留) 命令注册表接口
  ├── shell_command.cpp      # (重做) RPC 适配层
  └── cmd_trace.h            # (保留) trace 命令定义
  
backup/
  └── old-shell/             # 备份旧实现（不再使用）
```

---

## 🔗 参考资源

- Qt HTTP Server: https://doc.qt.io/qt-6/qhttpserver.html  
- JSON-RPC 2.0: https://www.jsonrpc.org/specification  
- nlohmann/json: https://github.com/nlohmann/json  

---

## ⏭️ Next Actions

1. 用户审批确认 ✓  
2. 开始 Day 1: 清理旧代码 + 实现 HttpRpcServer
3. 完成 Day 2: Python SDK + 测试

---
