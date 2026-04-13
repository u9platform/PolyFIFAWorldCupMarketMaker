# MVP Requirements - PolyFIFAWorldCupMarketMaker

## 目标

在 Polymarket 2026 FIFA World Cup Winner 市场上，同时对多个队伍提供双边报价，验证做市可行性。

## 范围

### IN SCOPE
- **行情服务**（独立进程，共享内存 IPC）
- 多市场同时做市（支持 N 个队伍并行）
- Avellaneda-Stoikov 报价模型（库存感知的动态 spread）
- 单层 bid/ask 挂单（每个市场独立一对）
- 成交后自动重新报价
- Per-market PnL 记录与 portfolio 级汇总
- 自动重连与重新订阅

### OUT OF SCOPE
- 外部赔率数据采集（Pinnacle/Betfair API 接入）— fair value 接口已预留，数据源待接入
- 全局概率归一化（强制 Σfv=1）— 仅做监控日志，不强制调整
- 风控熔断机制（价格跳动暂停、流动性枯竭检测）

---

## 功能需求

### F1: 行情服务 (Market Data Service)

独立进程，通过共享内存向策略进程提供实时行情。

#### F1.1: 架构

```
┌─────────────────────────────────────────────────┐
│           Market Data Service (进程 A)           │
│                                                  │
│  WS Client ───→ 解析 ───→ 写入共享内存            │
│  HTTP Poll ──→ 解析 ───→ 写入共享内存 (fallback)  │
│                                                  │
│  共享内存布局: MarketDataShm                      │
│  ┌──────────────────────────────────────┐        │
│  │ header: magic, version, num_tokens   │        │
│  │ slot[0]: { seqlock, token_hash,      │        │
│  │            best_bid, best_ask,       │        │
│  │            mid, timestamp,           │        │
│  │            last_trade_price/side/sz, │        │
│  │            bid_depth[5], ask_depth[5]}│        │
│  │ slot[1]: { ... }                     │        │
│  │ ...                                  │        │
│  │ slot[N-1]: { ... }                   │        │
│  └──────────────────────────────────────┘        │
└─────────────────────────────────────────────────┘
         │  POSIX shared memory (/dev/shm/poly_md)
         ▼
┌─────────────────────────────────────────────────┐
│           Strategy Process (进程 B)              │
│                                                  │
│  MarketDataReader ──→ 读 slot ──→ FairValue      │
│  (seqlock read, 无锁, 纳秒级)     ──→ AS Model  │
└─────────────────────────────────────────────────┘
```

#### F1.2: 共享内存数据结构

```cpp
struct alignas(64) MarketSlot {        // cache-line aligned
    std::atomic<uint64_t> sequence;    // seqlock: odd=writing, even=ready
    uint64_t token_hash;               // hash of token_id for fast lookup
    double best_bid;
    double best_ask;
    double mid;
    int64_t timestamp_ms;              // server timestamp from WS
    double last_trade_price;
    int8_t last_trade_side;            // 0=none, 1=BUY, 2=SELL
    double last_trade_size;
    // Top 5 depth levels
    double bid_prices[5];
    double bid_sizes[5];
    double ask_prices[5];
    double ask_sizes[5];
    char padding[...];                 // pad to 512 bytes
};

struct MarketDataShm {
    uint32_t magic = 0x504F4C59;       // "POLY"
    uint32_t version = 1;
    uint32_t num_slots;
    uint32_t reserved;
    MarketSlot slots[MAX_MARKETS];     // MAX_MARKETS = 64
};
```

#### F1.3: Seqlock 读写协议

**Writer (行情服务)**:
```
slot.sequence.store(seq + 1, release);   // 标记开始写 (奇数)
// 写入 best_bid, best_ask, mid, timestamp, ...
slot.sequence.store(seq + 2, release);   // 标记写完 (偶数)
```

**Reader (策略进程)**:
```
do {
    seq1 = slot.sequence.load(acquire);
    if (seq1 & 1) continue;              // 正在写，自旋等待
    // 读取 best_bid, best_ask, mid, ...
    seq2 = slot.sequence.load(acquire);
} while (seq1 != seq2);                  // 读取期间被写入，重试
```

- 无锁、无系统调用、无内核切换
- 读延迟: < 100ns
- 写延迟: < 50ns
- 适合单 writer 多 reader (SWMR)

#### F1.4: 行情服务职责

- 连接 Polymarket WebSocket，订阅所有配置的 token
- 解析 book / price_change / last_trade_price 事件
- 写入对应 slot 的共享内存
- HTTP fallback: 当 WS 断连超过 N 秒时，切换到 HTTP 轮询
- 心跳: 每秒更新 header 中的 heartbeat timestamp
- 策略进程通过 heartbeat 检测行情服务是否存活

#### F1.5: 行情服务 CLI

```bash
# 启动行情服务
poly_md --tokens <token1>,<token2>,... --shm-name /poly_md --shm-slots 64

# 行情监控 (读共享内存，打印到终端)
poly_md --monitor --shm-name /poly_md
```

#### F1.6: 策略进程读取接口

```cpp
class MarketDataReader {
public:
    MarketDataReader(const std::string& shm_name);

    // 读取指定 token 的最新行情 (seqlock, < 100ns)
    bool read(uint64_t token_hash, MarketSlot& out) const;

    // 检查行情服务是否存活
    bool isAlive() const;
};
```

### F8: Fair Value 计算

Fair value 是 AS 模型的核心输入，独立于报价引擎。

```
数据源层                  Fair Value 层                AS 模型层
┌──────────────┐
│ Polymarket   │──→ mid_price ──┐
│ Order Book   │                │     ┌───────────┐     ┌──────────┐
└──────────────┘                ├──→  │ fair_value │──→  │ AS 模型  │──→ bid/ask
┌──────────────┐                │     │ = f(mid,  │     │ r = s-qγσ²τ│
│ 外部赔率     │──→ ext_price ──┘     │   ext,    │     │ δ = ...   │
│ (Pinnacle等) │                      │   weights) │     └──────────┘
└──────────────┘                      └───────────┘
```

- F8.1: Fair value 接口
  - 输入: mid_price (必须), external_price (可选)
  - 输出: fair_value (double)
  - AS 模型中的 `s` 使用 fair_value 而非 raw mid_price

- F8.2: 默认模式 (无外部数据)
  - `fair_value = mid_price`
  - 与现有行为完全一致，无额外开销

- F8.3: 外部赔率锚定模式
  - `fair_value = w1 * mid_price + w2 * external_price`
  - 权重按数据源流动性分配（外部流动性通常 >> Polymarket）
  - 默认权重: w1 = 0.3 (Polymarket), w2 = 0.7 (外部)
  - 当外部数据不可用时，自动回退到纯 mid_price

- F8.4: 偏差保护
  - 当 |mid_price - external_price| > max_deviation 时:
    - 如果偏差持续 > N 秒，fair_value 偏向外部（Polymarket 可能错了）
    - 如果偏差是瞬时的，保持当前 fair_value（可能是延迟）
  - max_deviation 可配置（默认 0.03 = 3%）

- F8.5: 全局归一化 (预留)
  - 当做市 N 个队伍时，所有 fair_values 之和应 ≈ 1.0
  - 如果加总偏离 1.0 超过阈值，按比例缩放
  - 当前版本: 不强制归一化（OUT OF SCOPE），仅做监控日志

### F2: 报价引擎 (Avellaneda-Stoikov)

- F2.1: 基于 AS 模型计算 reservation price 和 optimal spread
  - **Reservation price** (库存调整后的公允价):
    ```
    r = s - q * γ * σ² * (T - t)
    ```
    - `s` = fair_value (from F8, 默认 = mid_price)
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
| **行情服务参数** | | | |
| `shm_name` | string | "/poly_md" | 共享内存名称 |
| `shm_slots` | int | 64 | 共享内存最大 token slot 数 |
| **策略参数** | | | |
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
| **Fair Value 参数** | | | |
| `fv_mode` | string | "mid" | fair value 模式: "mid" / "external_weighted" |
| `fv_ext_weight` | double | 0.7 | 外部赔率权重 (fv_mode="external_weighted" 时) |
| `fv_max_deviation` | double | 0.03 | mid 与外部偏差超过此值触发保护 |
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
- 行情服务 → 策略进程: 共享内存 seqlock, 读延迟 < 100ns
- 行情服务 WS 推送延迟: < 10ms（爱尔兰部署）
- 从行情变化到新订单发出: < 500ms
- HTTP 连接复用（persistent curl handle + TCP_NODELAY + keepalive）

### NF2: 可靠性
- 行情服务: WS 断连自动重连 + HTTP fallback
- 策略进程: API 请求失败自动重试（最多 3 次）
- 行情服务崩溃: 策略进程通过 heartbeat 检测，暂停报价

### NF3: 安全
- 私钥从环境变量或配置文件读取，不硬编码
- 日志中不输出敏感信息（API key、私钥）

### NF4: 可观测性
- 启动时输出当前配置参数
- 运行时可通过日志观察所有关键状态

---

## 运行流程

```
=== 进程 A: 行情服务 (poly_md) ===

启动
  ├─ 创建共享内存 /poly_md
  ├─ 连接 WebSocket, 订阅 tokens
  ▼
行情循环 ──────────────────────────────┐
  ├─ 收到 WS 消息                      │
  ├─ 解析 → 写入 slot (seqlock)        │
  ├─ 更新 heartbeat                    │
  └────────────────────────────────────┘


=== 进程 B: 策略 + 执行 (mm_bot) ===

启动
  ├─ 打开共享内存 /poly_md (只读)
  ├─ 验证 heartbeat (行情服务存活?)
  ├─ 初始化 API 连接, 验证余额
  ▼
策略循环 ────────────────────────────────────────┐
  │                                              │
  ├─ 1. 检查所有活跃订单状态                       │
  │     └─ 有成交 → 定位所属市场，更新持仓          │
  ├─ 2. 遍历每个市场:                              │
  │     ├─ 读共享内存 slot (seqlock, <100ns)       │
  │     ├─ 计算 fair_value (s)                     │
  │     ├─ 更新 σ, k                               │
  │     ├─ AS: r = s - qγσ²τ,  δ = γσ²τ + ...     │
  │     ├─ 库存超限 → 单边报价                      │
  │     ├─ 需要 requote? → 撤旧单，挂新单          │
  │     └─ (下一个市场)                             │
  ├─ 3. PnL 报告                                   │
  ├─ 4. sleep(poll_interval)                       │
  └──────────────────────────────────────────────┘
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
