# Test Plan - PolyFIFAWorldCupMarketMaker MVP

测试框架: Google Test (gtest)
Mock 框架: Google Mock (gmock)

**价格表示约定**: 内部使用整数 tick 计算 (1 tick = 0.001)，避免浮点精度问题。
**Rounding 策略**: `bid = floor(raw_bid / tick) * tick`, `ask = ceil(raw_ask / tick) * tick`, 最终确保 ask - bid >= 1 tick。

---

## 测试分层

```
Unit Tests        → 纯逻辑，无外部依赖
Integration Tests → Mock API，验证模块交互
E2E Tests         → 连接 Polymarket 真实环境（手动）
```

---

## 1. Unit Tests

### 1.1 报价引擎 (QuoteEngine)

```
TEST: QuoteEngine_计算bid和ask
  GIVEN: mid_price = 0.022, spread = 0.002
  WHEN:  calculateQuotes()
  THEN:  bid = 0.021, ask = 0.023

TEST: QuoteEngine_价格对齐tick_floor_ceil
  GIVEN: mid_price = 0.02237, spread = 0.002
  WHEN:  calculateQuotes()
  THEN:  raw_bid = 0.02137 → floor → 0.021
         raw_ask = 0.02337 → ceil  → 0.024
         bid = 0.021, ask = 0.024

TEST: QuoteEngine_spread小于最小tick_强制最小spread
  GIVEN: mid_price = 0.022, spread = 0.0005
  WHEN:  calculateQuotes()
  THEN:  raw spread < 1 tick, 强制 ask = bid + tick
         bid = 0.022, ask = 0.023 (或 bid = 0.021, ask = 0.022)
         关键断言: ask - bid >= 0.001

TEST: QuoteEngine_mid为零
  GIVEN: mid_price = 0.0
  WHEN:  calculateQuotes()
  THEN:  不生成报价，返回 std::nullopt

TEST: QuoteEngine_bid下限clamp
  GIVEN: mid_price = 0.001, spread = 0.004
  WHEN:  calculateQuotes()
  THEN:  raw_bid = -0.001 → clamp → 0.001
         ask = 0.003
         bid = 0.001, ask = 0.003

TEST: QuoteEngine_ask上限clamp
  GIVEN: mid_price = 0.998, spread = 0.004
  WHEN:  calculateQuotes()
  THEN:  raw_ask = 1.000 → clamp → 0.999
         bid = 0.996, ask = 0.999

TEST: QuoteEngine_是否需要重新报价_变化超过阈值
  GIVEN: old_mid = 0.022, new_mid = 0.024, threshold = 0.001
  WHEN:  shouldRequote()
  THEN:  返回 true

TEST: QuoteEngine_是否需要重新报价_变化未超过阈值
  GIVEN: old_mid = 0.022, new_mid = 0.0225, threshold = 0.001
  WHEN:  shouldRequote()
  THEN:  返回 false

TEST: QuoteEngine_整数tick内部表示
  GIVEN: price = 0.021
  WHEN:  toTicks(price)
  THEN:  返回 21
  WHEN:  fromTicks(21)
  THEN:  返回 0.021
  关键: 验证 toTicks(0.001 * 3) == 3 (浮点不丢精度)
```

### 1.2 Order Book 解析 (OrderBook)

```
TEST: OrderBook_解析正常数据
  GIVEN: JSON = {"bids":[{"price":"0.021","size":"500"}], "asks":[{"price":"0.023","size":"300"}]}
  WHEN:  parse()
  THEN:  best_bid = 0.021, best_bid_size = 500, best_ask = 0.023, best_ask_size = 300

TEST: OrderBook_计算mid_price
  GIVEN: best_bid = 0.021, best_ask = 0.023
  WHEN:  midPrice()
  THEN:  返回 0.022

TEST: OrderBook_计算micro_price
  GIVEN: best_bid = 0.021, bid_size = 500, best_ask = 0.023, ask_size = 300
  WHEN:  microPrice()
  THEN:  返回 (0.021 * 300 + 0.023 * 500) / 800 = 0.02225

TEST: OrderBook_空order_book
  GIVEN: JSON = {"bids":[], "asks":[]}
  WHEN:  parse()
  THEN:  midPrice() 返回 0, isValid() 返回 false

TEST: OrderBook_单边缺失
  GIVEN: JSON = {"bids":[{"price":"0.021","size":"500"}], "asks":[]}
  WHEN:  parse()
  THEN:  isValid() 返回 false

TEST: OrderBook_多层深度
  GIVEN: 3层 bids + 3层 asks
  WHEN:  parse()
  THEN:  best_bid = 最高 bid, best_ask = 最低 ask, 深度正确

TEST: OrderBook_crossed_market
  GIVEN: JSON = {"bids":[{"price":"0.023","size":"500"}], "asks":[{"price":"0.021","size":"300"}]}
  WHEN:  parse()
  THEN:  isValid() 返回 false (bid >= ask 是异常状态)

TEST: OrderBook_locked_market
  GIVEN: JSON = {"bids":[{"price":"0.022","size":"500"}], "asks":[{"price":"0.022","size":"300"}]}
  WHEN:  parse()
  THEN:  isValid() 返回 false (bid == ask, 无 spread 可赚)
```

### 1.3 持仓管理 (PositionTracker)

```
TEST: Position_初始状态
  GIVEN: 新建 PositionTracker
  THEN:  yes_position = 0, avg_cost = 0, realized_pnl = 0

TEST: Position_买入记录
  GIVEN: 初始持仓 = 0
  WHEN:  onFill(BUY, price=0.021, qty=100)
  THEN:  yes_position = 100, avg_cost = 0.021

TEST: Position_卖出记录_完全平仓
  GIVEN: 持仓 = 100, avg_cost = 0.021
  WHEN:  onFill(SELL, price=0.023, qty=100)
  THEN:  yes_position = 0, realized_pnl = (0.023 - 0.021) * 100 = 0.2

TEST: Position_部分卖出
  GIVEN: 持仓 = 100, avg_cost = 0.021
  WHEN:  onFill(SELL, price=0.023, qty=50)
  THEN:  yes_position = 50, avg_cost = 0.021, realized_pnl = 0.1

TEST: Position_多次买入平均成本
  GIVEN: 持仓 = 0
  WHEN:  onFill(BUY, 0.021, 100) 然后 onFill(BUY, 0.023, 100)
  THEN:  yes_position = 200, avg_cost = 0.022

TEST: Position_未实现PnL_盈利
  GIVEN: 持仓 = 100, avg_cost = 0.021
  WHEN:  unrealizedPnl(mark_price = 0.025)
  THEN:  返回 (0.025 - 0.021) * 100 = 0.4

TEST: Position_未实现PnL_亏损
  GIVEN: 持仓 = 100, avg_cost = 0.021
  WHEN:  unrealizedPnl(mark_price = 0.019)
  THEN:  返回 (0.019 - 0.021) * 100 = -0.2

TEST: Position_空仓卖出_做空建仓
  GIVEN: 持仓 = 0
  WHEN:  onFill(SELL, price=0.023, qty=100)
  THEN:  yes_position = -100, avg_cost = 0.023

TEST: Position_做空后买回平仓
  GIVEN: 持仓 = -100, avg_cost = 0.023
  WHEN:  onFill(BUY, price=0.021, qty=100)
  THEN:  yes_position = 0, realized_pnl = (0.023 - 0.021) * 100 = 0.2

TEST: Position_做空后买回亏损
  GIVEN: 持仓 = -100, avg_cost = 0.021
  WHEN:  onFill(BUY, price=0.023, qty=100)
  THEN:  yes_position = 0, realized_pnl = (0.021 - 0.023) * 100 = -0.2

TEST: Position_混合操作_多次买卖
  GIVEN: 持仓 = 0
  WHEN:  BUY 100 @ 0.021, SELL 50 @ 0.023, BUY 50 @ 0.020, SELL 100 @ 0.022
  THEN:  yes_position = 0
         realized_pnl = 50*(0.023-0.021) + 50*(0.022-0.021) + 50*(0.022-0.020)
                       = 0.1 + 0.05 + 0.1 = 0.25
```

### 1.4 PnL 统计 (PnlReporter)

```
TEST: PnlReporter_无成交
  GIVEN: 无任何成交记录
  WHEN:  generateReport()
  THEN:  total_trades = 0, realized_pnl = 0, avg_spread_earned = 0

TEST: PnlReporter_一次完整round_trip
  GIVEN: BUY at 0.021 qty 100, SELL at 0.023 qty 100
  WHEN:  generateReport()
  THEN:  total_trades = 2, realized_pnl = 0.2, avg_spread_earned = 0.002

TEST: PnlReporter_多次成交统计
  GIVEN: 5次 BUY + 5次 SELL, 各种价格
  WHEN:  generateReport()
  THEN:  正确计算总PnL、平均spread、成交频率 (trades/hour)

TEST: PnlReporter_亏损round_trip
  GIVEN: BUY at 0.023 qty 100, SELL at 0.021 qty 100
  WHEN:  generateReport()
  THEN:  realized_pnl = -0.2

TEST: PnlReporter_不对称数量
  GIVEN: BUY 100 @ 0.021, SELL 50 @ 0.023, SELL 50 @ 0.022
  WHEN:  generateReport()
  THEN:  realized_pnl = 50*0.002 + 50*0.001 = 0.15
         total_trades = 3

TEST: PnlReporter_成交明细记录
  GIVEN: onFill(BUY, 0.021, 100, timestamp=1000)
  WHEN:  getTrades()
  THEN:  返回包含 {side=BUY, price=0.021, qty=100, ts=1000} 的列表

TEST: PnlReporter_总PnL等于已实现加未实现
  GIVEN: BUY 100 @ 0.021, 当前 mark = 0.025
  WHEN:  generateReport()
  THEN:  total_pnl = realized(0) + unrealized(0.4) = 0.4
```

### 1.5 配置管理 (Config)

```
TEST: Config_加载默认值
  GIVEN: 配置文件含必要字段 + market_token_id
  WHEN:  load()
  THEN:  spread = 0.002, order_size = 100, poll_interval_ms = 10000

TEST: Config_加载自定义值
  GIVEN: 配置文件 {"spread": 0.004, "order_size": 200}
  WHEN:  load()
  THEN:  spread = 0.004, order_size = 200, poll_interval_ms = 10000 (默认)

TEST: Config_多市场token列表
  GIVEN: 配置文件 {"market_token_ids": ["token1", "token2", "token3"]}
  WHEN:  load()
  THEN:  allTokenIds() 返回 3 个 token

TEST: Config_单市场向后兼容
  GIVEN: 配置文件 {"market_token_id": "token1"}
  WHEN:  load()
  THEN:  allTokenIds() 返回 ["token1"]

TEST: Config_无market字段
  GIVEN: 配置文件无 market_token_id 也无 market_token_ids
  WHEN:  load()
  THEN:  抛出 ConfigError

TEST: Config_无效spread_负数
  GIVEN: 配置文件 {"spread": -0.001}
  WHEN:  load()
  THEN:  抛出 ConfigError

TEST: Config_无效spread_零
  GIVEN: 配置文件 {"spread": 0}
  WHEN:  load()
  THEN:  抛出 ConfigError

TEST: Config_无效spread_过大
  GIVEN: 配置文件 {"spread": 0.5}
  WHEN:  load()
  THEN:  抛出 ConfigError (spread > 合理上限)

TEST: Config_缺少必要字段
  GIVEN: 配置文件缺少 api_key
  WHEN:  load()
  THEN:  抛出 ConfigError，提示缺少 api_key

TEST: Config_文件不存在
  GIVEN: 配置文件路径不存在
  WHEN:  load("nonexistent.json")
  THEN:  抛出 ConfigError
```

---

## 2. Integration Tests (Mock API)

### 2.1 订单管理流程 (OrderManager)

```
TEST: OrderManager_下单成功
  GIVEN: Mock API 返回 order_id = "abc123"
  WHEN:  placeOrder(BUY, 0.021, 100)
  THEN:  活跃订单列表包含 "abc123"

TEST: OrderManager_下单失败重试_最终成功
  GIVEN: Mock API 前2次返回 500，第3次成功
  WHEN:  placeOrder(BUY, 0.021, 100)
  THEN:  重试3次后成功下单

TEST: OrderManager_下单3次失败放弃
  GIVEN: Mock API 持续返回 500
  WHEN:  placeOrder(BUY, 0.021, 100)
  THEN:  3次重试后抛出 ApiError

TEST: OrderManager_撤单成功
  GIVEN: 活跃订单 "abc123"
  WHEN:  cancelOrder("abc123")
  THEN:  活跃订单列表不再包含 "abc123"

TEST: OrderManager_撤单失败
  GIVEN: 活跃订单 "abc123", Mock API cancel 返回 500
  WHEN:  cancelOrder("abc123")
  THEN:  抛出 ApiError, 订单仍在活跃列表中, 日志输出错误

TEST: OrderManager_撤所有单
  GIVEN: 3个活跃订单
  WHEN:  cancelAll()
  THEN:  活跃订单列表为空，API 被调用3次

TEST: OrderManager_撤所有单_部分失败
  GIVEN: 3个活跃订单, 第2个撤单失败
  WHEN:  cancelAll()
  THEN:  继续尝试撤第3个, 返回失败列表包含第2个, 日志输出警告

TEST: OrderManager_检测全部成交
  GIVEN: Mock API 返回 order status = FILLED
  WHEN:  checkOrders()
  THEN:  触发 onFill 回调，订单从活跃列表移除

TEST: OrderManager_检测部分成交
  GIVEN: Mock API 返回 order status = PARTIALLY_FILLED, filled_qty = 50
  WHEN:  checkOrders()
  THEN:  触发 onFill(qty=50)，订单保留在活跃列表, filled 数量更新
```

### 2.2 主循环 (MarketMaker)

```
TEST: MarketMaker_启动流程
  GIVEN: Mock API 返回有效 order book
  WHEN:  start()
  THEN:  下出1个 bid + 1个 ask

TEST: MarketMaker_启动时余额不足
  GIVEN: Mock API 返回余额 = 0
  WHEN:  start()
  THEN:  抛出异常，不下单

TEST: MarketMaker_mid变化触发重新报价
  GIVEN: 当前 bid/ask 基于 mid=0.022
  WHEN:  新 order book 的 mid=0.024 (变化 > threshold)
  THEN:  撤旧单 → 挂新单 (bid=0.023, ask=0.025)

TEST: MarketMaker_mid未变化不重新报价
  GIVEN: 当前 bid/ask 基于 mid=0.022
  WHEN:  新 order book 的 mid=0.0225 (变化 < threshold)
  THEN:  不撤单，不挂新单

TEST: MarketMaker_bid成交后重新挂单
  GIVEN: bid 成交 (买入 100 shares at 0.021)
  WHEN:  主循环检测到成交
  THEN:  更新持仓 → 重新挂 bid

TEST: MarketMaker_ask成交后重新挂单
  GIVEN: ask 成交 (卖出 100 shares at 0.023)
  WHEN:  主循环检测到成交
  THEN:  更新持仓 → 重新挂 ask

TEST: MarketMaker_双边同时成交
  GIVEN: bid 和 ask 都成交
  WHEN:  主循环检测到
  THEN:  PnL += spread * qty, 重新挂双边

TEST: MarketMaker_mid变化同时有成交
  GIVEN: bid 成交 + 新 order book 的 mid 变化 > threshold
  WHEN:  主循环
  THEN:  先处理成交更新持仓, 再基于新 mid 重新报价 (两件事都做)

TEST: MarketMaker_order_book无效时不报价
  GIVEN: Mock API 返回空 order book
  WHEN:  主循环
  THEN:  不下单，日志输出警告

TEST: MarketMaker_order_book_crossed时不报价
  GIVEN: Mock API 返回 bid >= ask 的 order book
  WHEN:  主循环
  THEN:  撤掉现有挂单，不下新单，日志输出警告

TEST: MarketMaker_下单单腿失败
  GIVEN: bid 下单成功, ask 下单失败 (API error)
  WHEN:  主循环
  THEN:  撤掉已成功的 bid, 日志输出错误, 下次循环重试双边

TEST: MarketMaker_退出时清理
  GIVEN: 2个活跃订单
  WHEN:  收到 SIGINT
  THEN:  cancelAll() 被调用，输出最终 PnL 报告

TEST: MarketMaker_退出时撤单失败
  GIVEN: 2个活跃订单, cancelAll 部分失败
  WHEN:  收到 SIGINT
  THEN:  日志输出未撤掉的订单ID, 供人工处理
```

### 2.3 多市场 (MarketMaker Multi-Market)

```
TEST: MarketMaker_多市场初始化
  GIVEN: Config 含 3 个 token_id
  WHEN:  构造 MarketMaker
  THEN:  marketCount() == 3

TEST: MarketMaker_多市场各自独立报价
  GIVEN: 3 个市场, Mock API 为每个返回不同 order book
  WHEN:  tick()
  THEN:  每个市场各下 1 bid + 1 ask, 共 6 个订单, 各自 mid 不同

TEST: MarketMaker_多市场独立fill
  GIVEN: 3 个市场都有活跃订单, 市场A的bid成交
  WHEN:  tick()
  THEN:  只有市场A更新持仓和重新挂单, 市场B/C不受影响

TEST: MarketMaker_portfolio敞口计算
  GIVEN: 市场A持仓100@0.02, 市场B持仓-50@0.15
  WHEN:  portfolioExposure()
  THEN:  返回 100*0.02 + (-50)*0.15 = 2.0 - 7.5 = -5.5

TEST: MarketMaker_多市场优雅退出
  GIVEN: 3 个市场各有 2 个活跃订单
  WHEN:  stop()
  THEN:  6 个订单全部撤销, 输出 3 条 per-market PnL 报告
```

---

## 3. E2E Tests (手动)

在真实环境下手动验证，记录结果。

```
E2E-1: 启动连接
  步骤: 配置真实 API key，启动程序
  预期: 成功连接，输出 order book 数据
  验证: 日志中有 order book 快照

E2E-2: 首次挂单
  步骤: 观察启动后是否自动挂出 bid + ask
  预期: Polymarket UI 上可见自己的挂单
  验证: 日志中有下单成功记录，UI 确认

E2E-3: 成交触发
  步骤: 在 Polymarket UI 上手动吃掉自己的 bid 或 ask
  预期: 程序检测到成交，更新持仓，重新挂单
  验证: 日志中有成交记录 + 重新挂单记录

E2E-4: 长时间运行
  步骤: 运行 24 小时
  预期: 无崩溃，日志持续输出，PnL 报告正常
  验证: 日志无异常中断，PnL 报告连续

E2E-5: 优雅退出
  步骤: Ctrl+C
  预期: 所有挂单被撤销，输出最终报告
  验证: Polymarket UI 上无残留挂单

E2E-6: 网络断开恢复
  步骤: 运行中断开网络 30 秒后恢复
  预期: 程序自动重连，恢复报价
  验证: 日志中有断连 + 重连记录
```

---

## 测试文件结构

```
tests/
├── unit/
│   ├── test_quote_engine.cpp      # 9 tests
│   ├── test_order_book.cpp        # 8 tests
│   ├── test_position_tracker.cpp  # 11 tests
│   ├── test_pnl_reporter.cpp      # 7 tests
│   └── test_config.cpp            # 10 tests (+3 multi-market)
├── integration/
│   ├── test_order_manager.cpp     # 9 tests
│   └── test_market_maker.cpp      # 18 tests (+5 multi-market)
├── mocks/
│   └── mock_api_client.h
└── CMakeLists.txt

Total: 72 tests
```
