# AUTOSAR 面试题

---

### 题1｜易｜分层与 RTE

**问：** Classic AUTOSAR 主要分层（ASW/RTE/BSW/MCAL）？ASW 为何不能直接调 CAN Driver？RTE 作用？你验证的「RTE↔ASW 收发」验的是哪段？

**答：** 上→下：ASW → RTE → BSW（Com/PduR/CanIf/Dcm…）→ MCAL。禁直调驱动是为可移植与接口标准化。RTE 给 SW-C 提供标准读写/调用，解耦应用与 BSW。验证点：应用端口 ↔ RTE ↔ Com（及下游）通路是否通、方向与更新是否对。

---

### 题2｜易～中｜信号上总线路径

**问：** 发 `EngineSpeed` 的模块路径？Signal / I-PDU / L-PDU 各在哪处理？「I-PDU 打包」验什么？PDU 路由 vs 信号路由？

**答：** Tx：ASW→RTE→**Com**→**PduR**→**CanIf**→Can/MCAL→总线（Rx 反向）。Signal 在 Com 映射进 I-PDU；I-PDU 由 Com 组解包、PduR 路由；L-PDU 近 CanIf/CAN ID。打包验：位序/端序/长度、周期或触发是否符合矩阵。信号路由≈Com；PDU 路由≈PduR。

---

### 题3｜中｜应用通信 vs 诊断通路（新）

**问：** 普通 CAN 周期信号与 UDS 诊断报文在 AUTOSAR 里路径有何不同？为何多帧诊断常涉及 CanTp，而普通 Com 信号通常不需要？功能寻址与物理寻址对测试意味着什么？

**答：** 应用信号：Com↔PduR↔CanIf。诊断：Dcm↔（常经）**CanTp**↔PduR↔CanIf——诊断可超单帧，需分段/流控/重组。功能寻址多节点可响应，干扰大；物理寻址对单 ECU，联调更干净。二者都依赖正确 PDU 路由，但上层模块不同（Com vs Dcm）。

---

### 题4｜中｜UDS $10 / $22 / $27

**问：** 三服务用途？为何有的 $22 须先扩展会话甚至 $27？$27 流程与连续错密钥？`$7F 22 33` 含义？

**答：** $10 切会话；$22 按 DID 读数据；$27 安全解锁。DID/服务在 DCM 配了会话与安全等级。$27：requestSeed→算密钥→sendKey；错多次常锁延时（如 NRC $36/$37）。`7F 22 33`＝否定响应，$22，安全访问拒绝（未解锁或等级不够）。正响应 SID=+0x40（$10→$50）。

---

### 题5｜难｜帧对但应用 Signal 不对（新）

**问：** TSMaster 抓包：CAN ID/DLC/载荷符合矩阵，但 ASW 读到的 Signal 不更新或数值错。请分层给出排查顺序，并说明如何区分「Com 映射错」「RTE 接错」「Rx 超时用了默认值」。

**答：** ①确认帧确进本节点（ID 过滤/CanIf/PduR 是否接到本 Com I-PDU）。②对载荷按矩阵手算目标 Signal，与 `Rte_Read`/调试值比——一致则应用侧问题，不一致则 Com 起始位/长度/端序或 Signal↔I-PDU 映射错。③值一直为 init/替代值且从不变→重点查 Rx 超时、I-PDU 未指示到 Com、或 RTE 读了错误 DataElement。④偶发旧值→查 Com_MainFunctionRx/任务周期与更新时机。

---

| # | 难度 | 考察 | 简历锚点 |
|---|------|------|----------|
| 1 | 易 | 分层 | 架构、RTE↔ASW |
| 2 | 易～中 | Com 路径 | PDU/信号、打包 |
| 3 | 中 | Com vs 诊断栈 | UDS、单节点测试 |
| 4 | 中 | UDS 语义 | $10/$22/$27 |
| 5 | 难 | Com/RTE 排障 | 抓包验证信号 |
