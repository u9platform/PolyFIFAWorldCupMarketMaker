# MVP Requirements - PolyFIFAWorldCupMarketMaker

## 目标

在 Polymarket 2026 FIFA World Cup Winner 市场上，选择单一队伍（日本），提供双边报价，验证做市可行性。

## 范围

### IN SCOPE
- 单一市场（Japan YES token）
- 固定 spread 报价
- 单层 bid/ask 挂单
- 成交后自动重新报价
- PnL 记录与统计

### OUT OF SCOPE
- 外部赔率数据（Pinnacle/Betfair）
- 全局概率归一化
- Avellaneda-Stoikov 模型
- 库存 skew 调整
- 多市场同时做市
- 风控熔断机制

---

## 功能需求

### F1: 市场数据获取

- F1.1: 通过 Polymarket CLOB API 获取指定市场的 order book（best bid/ask 及深度）
- F1.2: 计算 mid-price 作为 fair value
- F1.3: 定时轮询 order book，更新频率可配置（默认 10 秒）

### F2: 报价引擎

- F2.1: 基于 mid-price 和配置的 spread，计算 bid_price 和 ask_price
  - `bid_price = mid - spread / 2`
  - `ask_price = mid + spread / 2`
- F2.2: 价格精度对齐到 tick size（0.001）
  - Rounding 策略: 先将 mid truncate 到 tick 精度，再加减 spread/2（取整到 tick）
  - 即: `bid = floor((mid - spread/2) / tick) * tick`, `ask = ceil((mid + spread/2) / tick) * tick`
  - 但如果 ask - bid < tick，则强制 ask = bid + tick
  - bid 下限 clamp 到 tick（0.001），ask 上限 clamp 到 0.999
- F2.5: 内部价格计算统一使用整数 tick 表示（price_ticks = round(price / 0.001)），避免浮点精度问题
- F2.3: 报价数量（size）可配置，默认 100 shares
- F2.4: 当 mid-price 变化超过阈值（可配置，默认 1 tick）时，撤旧单挂新单

### F3: 订单管理

- F3.1: 通过 CLOB API 下限价单（bid 和 ask）
- F3.2: 通过 CLOB API 撤销订单
- F3.3: 查询当前活跃订单状态
- F3.4: 检测订单成交（全部成交或部分成交）
- F3.5: 成交后自动重新计算并挂出新的 bid/ask

### F4: 持仓管理

- F4.1: 跟踪当前 YES token 持仓数量
- F4.2: 跟踪 USDC 余额
- F4.3: 记录每笔成交明细（时间、方向、价格、数量）

### F5: PnL 统计

- F5.1: 计算已实现 PnL（双边成交的 spread 利润）
- F5.2: 计算未实现 PnL（当前持仓 × (mark_price - avg_cost)）
- F5.3: 统计成交次数、成交量、平均 spread 收益
- F5.4: 定时输出 PnL 报告到日志（默认每 60 秒）

### F6: 日志

- F6.1: 记录所有下单、撤单、成交事件
- F6.2: 记录 order book 快照（每次轮询）
- F6.3: 记录异常和错误
- F6.4: 日志输出到文件，格式为结构化文本（时间戳 + 事件类型 + 详情）

---

## 配置参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `market_token_id` | string | Japan YES token ID | 目标市场 token |
| `spread` | double | 0.002 | 双边总 spread (0.2%) |
| `order_size` | double | 100 | 每笔挂单数量 (shares) |
| `poll_interval_ms` | int | 10000 | order book 轮询间隔 (毫秒) |
| `requote_threshold` | double | 0.001 | mid-price 变化多少触发重新报价 |
| `api_key` | string | - | Polymarket API key |
| `api_secret` | string | - | Polymarket API secret |
| `private_key` | string | - | 钱包私钥（用于签名订单） |
| `log_file` | string | `mm.log` | 日志文件路径 |
| `pnl_report_interval_s` | int | 60 | PnL 报告输出间隔 (秒) |

---

## 非功能需求

### NF1: 性能
- 从检测到 mid-price 变化到新订单发出，延迟 < 500ms

### NF2: 可靠性
- API 请求失败时自动重试（最多 3 次，间隔 1 秒）
- 网络断连后自动重连并恢复报价

### NF3: 安全
- 私钥从环境变量或配置文件读取，不硬编码
- 日志中不输出敏感信息（API key、私钥）

### NF4: 可观测性
- 启动时输出当前配置参数
- 运行时可通过日志观察所有关键状态

---

## 运行流程

```
启动
  │
  ├─ 读取配置
  ├─ 初始化 API 连接
  ├─ 验证余额和权限
  │
  ▼
主循环 ─────────────────────────────────────┐
  │                                         │
  ├─ 1. 拉取 order book                     │
  ├─ 2. 检查订单状态（成交/部分成交）         │
  │     └─ 有成交 → 更新持仓，记录PnL        │
  ├─ 3. 计算 mid-price                      │
  ├─ 4. 需要重新报价?（mid变化/有成交/缺腿） │
  │     ├─ YES → 撤旧单，计算新价，挂新单    │
  │     └─ NO  → 无操作                     │
  ├─ 5. 到 PnL 报告时间? → 输出报告          │
  ├─ 6. sleep(poll_interval)                │
  └─────────────────────────────────────────┘

退出信号 (Ctrl+C)
  │
  ├─ 撤销所有活跃订单
  ├─ 输出最终 PnL 报告
  └─ 退出
```

---

## 验证标准

跑 3-5 天后评估：

| 指标 | 通过条件 |
|------|---------|
| API 链路 | 下单/撤单/查询全部正常 |
| 双边成交 | bid 和 ask 都有成交记录 |
| 净 PnL | >= 0（不亏即可） |
| 单边库存 | 未被单方向打穿（库存 < 配置上限） |
| 稳定性 | 无需人工干预持续运行 |

---

## 技术选型

- 语言: C++17
- HTTP 客户端: libcurl 或 cpp-httplib
- JSON 解析: nlohmann/json
- 签名: OpenSSL (ECDSA for Polymarket order signing)
- 构建: CMake
- 日志: spdlog
