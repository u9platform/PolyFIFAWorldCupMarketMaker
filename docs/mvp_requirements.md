# MVP Requirements - PolyFIFAWorldCupMarketMaker

## 目标

在 Polymarket 2026 FIFA World Cup Winner 市场上，同时对多个队伍提供双边报价，验证做市可行性。

## 范围

### IN SCOPE
- 多市场同时做市（支持 N 个队伍并行）
- Avellaneda-Stoikov 报价模型（库存感知的动态 spread）
- 单层 bid/ask 挂单（每个市场独立一对）
- 成交后自动重新报价
- Per-market PnL 记录与 portfolio 级汇总
- WebSocket 实时行情 + HTTP 轮询 fallback
- 自动重连与重新订阅

### OUT OF SCOPE
- 外部赔率数据（Pinnacle/Betfair）
- 全局概率归一化
- 风控熔断机制

---

## 功能需求

### F1: 市场数据获取

- F1.1: 通过 Polymarket CLOB API 获取指定市场的 order book（best bid/ask 及深度）
- F1.2: 计算 mid-price 作为 fair value
- F1.3: 定时轮询 order book，更新频率可配置（默认 10 秒）
- F1.4: WebSocket 实时行情（wss://ws-subscriptions-clob.polymarket.com/ws/market）
  - 单连接订阅多个 token，接收 book/price_change/last_trade_price 事件
  - 断连后自动重连并重新发送订阅
- F1.5: Per-token 最新 BestQuote 缓存，线程安全读取

### F2: 报价引擎 (Avellaneda-Stoikov)

- F2.1: 基于 AS 模型计算 reservation price 和 optimal spread
  - **Reservation price** (库存调整后的公允价):
    ```
    r = s - q * γ * σ² * (T - t)
    ```
    - `s` = 当前 mid-price (from order book)
    - `q` = 当前库存 (正=多头, 负=空头, 单位: shares)
    - `γ` = 风险厌恶系数 (可配置, 默认 0.1)
    - `σ` = 波动率 (从近期价格历史计算)
    - `T - t` = 距离到期时间 (单位: 年的小数, 世界杯决赛 2026-07-20)
  - **Optimal spread**:
    ```
    δ = γ * σ² * (T - t) + (2/γ) * ln(1 + γ/k)
    ```
    - `k` = 订单到达强度 (从近期成交频率估算)
    - `δ` 为最优总 spread, 但不得小于 `min_spread` (可配置)
  - **最终报价**:
    ```
    bid = r - δ/2
    ask = r + δ/2
    ```
- F2.2: 价格精度对齐到 tick size（0.001）
  - Rounding 策略: `bid = floor(raw_bid / tick) * tick`, `ask = ceil(raw_ask / tick) * tick`
  - 如果 ask - bid < tick，则强制 ask = bid + tick
  - bid 下限 clamp 到 tick（0.001），ask 上限 clamp 到 0.999
- F2.3: 内部价格计算统一使用整数 tick 表示，避免浮点精度问题
- F2.4: 报价数量（size）可配置，默认 100 shares
- F2.5: 当 reservation price 变化超过阈值时，撤旧单挂新单
  - 触发条件: |new_r - old_r| > requote_threshold，或 有成交，或 缺腿

### F7: Avellaneda-Stoikov 参数管理

- F7.1: 波动率计算 (σ)
  - 使用滑动窗口内的 mid-price 历史，计算已实现波动率
  - 窗口大小可配置（默认 100 个采样点）
  - 每次 tick 更新波动率估计
  - 公式: `σ = std(log(mid[i]/mid[i-1])) * sqrt(samples_per_year)`
  - 最小值 clamp: σ >= 0.01 (防止 spread 收窄到 0)

- F7.2: 订单到达强度 (k)
  - 使用滑动窗口内的成交次数估算
  - `k = num_fills_in_window / window_duration_hours`
  - 窗口大小可配置（默认 1 小时）
  - 最小值 clamp: k >= 0.1 (防止 ln(1 + γ/k) 爆炸)

- F7.3: 库存感知
  - Per-market 库存 `q` 直接从 PositionTracker 读取
  - 库存为正 → reservation price 下移 → 鼓励卖出
  - 库存为负 → reservation price 上移 → 鼓励买入
  - 效果: 自动平衡买卖，防止单边堆积

- F7.4: 库存硬上限 (安全阀)
  - 当 |q| > max_inventory 时，停止在库存方向继续挂单
  - 例如: q > max_inventory → 只挂 ask，不挂 bid
  - max_inventory 可配置（默认 1000 shares per market）

- F7.5: 到期时间衰减
  - T = 2026-07-20 (世界杯决赛日)
  - T - t 随时间自然减小
  - 效果: 越接近到期，spread 越窄，reservation price 对库存越敏感

### F3: 订单管理

- F3.1: 通过 CLOB API 下限价单（bid 和 ask）
- F3.2: 通过 CLOB API 撤销订单
- F3.3: 查询当前活跃订单状态
- F3.4: 检测订单成交（全部成交或部分成交）
- F3.5: 成交后自动重新计算并挂出新的 bid/ask

### F4: 持仓管理

- F4.1: Per-market 跟踪 YES token 持仓数量
- F4.2: 跟踪 USDC 余额
- F4.3: 记录每笔成交明细（时间、方向、价格、数量、所属市场）
- F4.4: Portfolio 级敞口计算: Σ(position_i × mid_i)

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
| `market_token_id` | string | - | 单市场模式 token (向后兼容) |
| `market_token_ids` | string[] | - | 多市场模式 token 列表 |
| `order_size` | double | 100 | 每笔挂单数量 (shares) |
| `poll_interval_ms` | int | 10000 | order book 轮询间隔 (毫秒) |
| `requote_threshold` | double | 0.001 | reservation price 变化多少触发重新报价 |
| `api_key` | string | - | Polymarket API key |
| `api_secret` | string | - | Polymarket API secret |
| `private_key` | string | - | 钱包私钥（用于签名订单） |
| `log_file` | string | `mm.log` | 日志文件路径 |
| `pnl_report_interval_s` | int | 60 | PnL 报告输出间隔 (秒) |
| **AS 模型参数** | | | |
| `gamma` | double | 0.1 | 风险厌恶系数 (越大 → spread 越宽, 库存惩罚越重) |
| `min_spread` | double | 0.001 | 最小允许 spread (不低于 1 tick) |
| `vol_window_size` | int | 100 | 波动率计算窗口 (采样点数) |
| `k_window_hours` | double | 1.0 | 订单到达强度估算窗口 (小时) |
| `max_inventory` | double | 1000 | 单市场最大持仓 (shares, 超过则停止该方向挂单) |
| `expiry_date` | string | "2026-07-20" | 到期日 (世界杯决赛日, 用于计算 T-t) |

---

## 非功能需求

### NF1: 性能
- 从检测到 mid-price 变化到新订单发出，延迟 < 500ms
- HTTP 连接复用（persistent curl handle + TCP_NODELAY + keepalive）
- WebSocket 单连接多市场，推送延迟 < 10ms（爱尔兰部署）
- 多市场顺序处理，单次 tick 遍历所有市场

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
主循环 ─────────────────────────────────────────┐
  │                                             │
  ├─ 1. 检查所有活跃订单状态（一次性）            │
  │     └─ 有成交 → 定位所属市场，更新持仓        │
  ├─ 2. 遍历每个市场:                            │
  │     ├─ 拉取 order book, 得到 mid (s)         │
  │     ├─ 更新波动率 σ (滑动窗口)               │
  │     ├─ 更新订单到达强度 k                    │
  │     ├─ 读取库存 q                            │
  │     ├─ AS 计算:                              │
  │     │   r = s - q * γ * σ² * (T-t)          │
  │     │   δ = γ * σ² * (T-t) + (2/γ)*ln(1+γ/k)│
  │     │   δ = max(δ, min_spread)               │
  │     │   bid = r - δ/2,  ask = r + δ/2       │
  │     ├─ 库存超限检查:                         │
  │     │   q > max → 只挂 ask                   │
  │     │   q < -max → 只挂 bid                  │
  │     ├─ 需要重新报价?                         │
  │     │   ├─ YES → 撤旧单，挂新单              │
  │     │   └─ NO  → 跳过                       │
  │     └─ (下一个市场)                          │
  ├─ 3. 到 PnL 报告时间? → 输出各市场报告        │
  ├─ 4. sleep(poll_interval)                    │
  └─────────────────────────────────────────────┘

退出信号 (Ctrl+C)
  │
  ├─ 撤销所有市场的活跃订单
  ├─ 输出 per-market PnL + portfolio 总敞口
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
| 库存平衡 | 买卖次数比在 0.6-1.4 之间（AS 库存调整生效） |
| 库存硬上限 | 从未触发 max_inventory 熔断 |
| Spread 动态 | σ 变化时 spread 跟随调整 |
| 稳定性 | 无需人工干预持续运行 |

与无 AS 模型的对照 (基于 28 小时 dry-run 数据):

| 指标 | 无 AS (实测) | 有 AS (预期) |
|------|-------------|-------------|
| 买/卖比 | 67/35 = 1.91 | 接近 1.0 |
| 净库存 | +3200 shares (单边偏多) | 在 ±max_inventory 内 |
| PnL | -$144.50 (库存亏损) | > $0 (spread 利润 > 库存成本) |

---

## 技术选型

- 语言: C++17
- HTTP 客户端: libcurl (persistent handle, connection reuse)
- WebSocket: ixwebsocket (v11.4.5)
- JSON 解析: nlohmann/json
- 签名: OpenSSL (ECDSA for Polymarket order signing)
- 构建: CMake + FetchContent
- 日志: spdlog
- 部署: AWS EC2 eu-west-1 (Ireland, ~8ms WS latency to Polymarket)
