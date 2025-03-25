#include <benchmark/benchmark.h>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

enum class OrderStatus
{
    PENDING,
    PROCESSING,
    COMPLETED,
    CANCELLED
};

struct SymbolConfig
{
    double min_price;
    double max_price;
    int64_t min_volume;
    int64_t max_volume;
};

struct GroupConfig
{
    std::unordered_map<std::string, SymbolConfig> symbol_configs;
};

struct OptimizedOrder
{
    int64_t order_id;
    int64_t amount;
    OrderStatus status;
    int64_t user_id;
    std::string symbol;
    std::string group;
    double price;
    int64_t volume;
    char padding[24];
    std::string product_name;
    std::string address;
};

struct UnoptimizedOrder
{
    int64_t order_id;
    char padding[56];
    int64_t amount;
    char padding2[56];
    OrderStatus status;
    char padding3[56];
    int64_t user_id;
    char padding4[56];
    std::string symbol;
    std::string group;
    double price;
    int64_t volume;
    char padding5[56];
    std::string product_name;
    std::string address;
};

// 配置数据
const std::unordered_map<std::string, GroupConfig> GROUP_CONFIGS = {
    {"GROUP_A",
     {{{"BTC", {50000.0, 100000.0, 1, 10}}, {"ETH", {2000.0, 5000.0, 5, 50}}, {"SOL", {50.0, 200.0, 100, 1000}}}}},
    {"GROUP_B",
     {{{"AAPL", {150.0, 200.0, 10, 100}}, {"GOOGL", {2500.0, 3000.0, 1, 10}}, {"MSFT", {300.0, 400.0, 5, 50}}}}}};

std::vector<OptimizedOrder> generateOptimizedOrders(size_t count)
{
    std::vector<OptimizedOrder> orders;
    orders.reserve(count);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int64_t> id_dist(1, 1000000);
    std::uniform_int_distribution<int64_t> amount_dist(100, 10000);

    std::vector<std::string> groups = {"GROUP_A", "GROUP_B"};
    std::vector<std::string> symbols = {"BTC", "ETH", "SOL", "AAPL", "GOOGL", "MSFT"};

    std::uniform_int_distribution<size_t> group_dist(0, groups.size() - 1);
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols.size() - 1);

    for (size_t i = 0; i < count; ++i)
    {
        OptimizedOrder order;
        order.order_id = id_dist(gen);
        order.amount = amount_dist(gen);
        order.status = static_cast<OrderStatus>(i % 4);
        order.user_id = id_dist(gen);
        order.group = groups[group_dist(gen)];
        order.symbol = symbols[symbol_dist(gen)];
        order.price = (order.group == "GROUP_A") ? (order.symbol == "BTC"   ? 75000.0
                                                    : order.symbol == "ETH" ? 3500.0
                                                                            : 150.0)
                                                 : (order.symbol == "AAPL"    ? 175.0
                                                    : order.symbol == "GOOGL" ? 2750.0
                                                                              : 350.0);
        order.volume = (order.group == "GROUP_A") ? (order.symbol == "BTC"   ? 5
                                                     : order.symbol == "ETH" ? 25
                                                                             : 500)
                                                  : (order.symbol == "AAPL"    ? 50
                                                     : order.symbol == "GOOGL" ? 5
                                                                               : 25);
        order.product_name = "Product_" + std::to_string(i);
        order.address = "Address_" + std::to_string(i);
        orders.push_back(order);
    }
    return orders;
}

std::vector<UnoptimizedOrder> generateUnoptimizedOrders(size_t count)
{
    std::vector<UnoptimizedOrder> orders;
    orders.reserve(count);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int64_t> id_dist(1, 1000000);
    std::uniform_int_distribution<int64_t> amount_dist(100, 10000);

    std::vector<std::string> groups = {"GROUP_A", "GROUP_B"};
    std::vector<std::string> symbols = {"BTC", "ETH", "SOL", "AAPL", "GOOGL", "MSFT"};

    std::uniform_int_distribution<size_t> group_dist(0, groups.size() - 1);
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols.size() - 1);

    for (size_t i = 0; i < count; ++i)
    {
        UnoptimizedOrder order;
        order.order_id = id_dist(gen);
        order.amount = amount_dist(gen);
        order.status = static_cast<OrderStatus>(i % 4);
        order.user_id = id_dist(gen);
        order.group = groups[group_dist(gen)];
        order.symbol = symbols[symbol_dist(gen)];
        order.price = (order.group == "GROUP_A") ? (order.symbol == "BTC"   ? 75000.0
                                                    : order.symbol == "ETH" ? 3500.0
                                                                            : 150.0)
                                                 : (order.symbol == "AAPL"    ? 175.0
                                                    : order.symbol == "GOOGL" ? 2750.0
                                                                              : 350.0);
        order.volume = (order.group == "GROUP_A") ? (order.symbol == "BTC"   ? 5
                                                     : order.symbol == "ETH" ? 25
                                                                             : 500)
                                                  : (order.symbol == "AAPL"    ? 50
                                                     : order.symbol == "GOOGL" ? 5
                                                                               : 25);
        order.product_name = "Product_" + std::to_string(i);
        order.address = "Address_" + std::to_string(i);
        orders.push_back(order);
    }
    return orders;
}

static void BM_OptimizedOrderProcessing(benchmark::State &state)
{
    auto orders = generateOptimizedOrders(1000);
    int64_t total_amount = 0;
    for (auto _ : state)
    {
        for (const auto &order : orders)
        {
            if (order.status == OrderStatus::PENDING)
            {
                auto group_it = GROUP_CONFIGS.find(order.group);
                if (group_it != GROUP_CONFIGS.end())
                {
                    auto symbol_it = group_it->second.symbol_configs.find(order.symbol);
                    if (symbol_it != group_it->second.symbol_configs.end())
                    {
                        const auto &config = symbol_it->second;
                        if (order.price >= config.min_price && order.price <= config.max_price &&
                            order.volume >= config.min_volume && order.volume <= config.max_volume)
                        {
                            total_amount += order.amount;
                            benchmark::DoNotOptimize(total_amount);
                        }
                    }
                }
            }
        }
    }
}

static void BM_UnoptimizedOrderProcessing(benchmark::State &state)
{
    auto orders = generateUnoptimizedOrders(1000);
    int64_t total_amount = 0;
    for (auto _ : state)
    {
        for (const auto &order : orders)
        {
            if (order.status == OrderStatus::PENDING)
            {
                auto group_it = GROUP_CONFIGS.find(order.group);
                if (group_it != GROUP_CONFIGS.end())
                {
                    auto symbol_it = group_it->second.symbol_configs.find(order.symbol);
                    if (symbol_it != group_it->second.symbol_configs.end())
                    {
                        const auto &config = symbol_it->second;
                        if (order.price >= config.min_price && order.price <= config.max_price &&
                            order.volume >= config.min_volume && order.volume <= config.max_volume)
                        {
                            total_amount += order.amount;
                            benchmark::DoNotOptimize(total_amount);
                        }
                    }
                }
            }
        }
    }
}

static void BM_OptimizedOrderStatusUpdate(benchmark::State &state)
{
    auto orders = generateOptimizedOrders(1000);
    for (auto _ : state)
    {
        for (auto &order : orders)
        {
            if (order.status == OrderStatus::PENDING)
            {
                auto group_it = GROUP_CONFIGS.find(order.group);
                if (group_it != GROUP_CONFIGS.end())
                {
                    auto symbol_it = group_it->second.symbol_configs.find(order.symbol);
                    if (symbol_it != group_it->second.symbol_configs.end())
                    {
                        const auto &config = symbol_it->second;
                        if (order.price >= config.min_price && order.price <= config.max_price &&
                            order.volume >= config.min_volume && order.volume <= config.max_volume)
                        {
                            order.status = OrderStatus::PROCESSING;
                            benchmark::DoNotOptimize(order.status);
                        }
                    }
                }
            }
        }
    }
}

static void BM_UnoptimizedOrderStatusUpdate(benchmark::State &state)
{
    auto orders = generateUnoptimizedOrders(1000);
    for (auto _ : state)
    {
        for (auto &order : orders)
        {
            if (order.status == OrderStatus::PENDING)
            {
                auto group_it = GROUP_CONFIGS.find(order.group);
                if (group_it != GROUP_CONFIGS.end())
                {
                    auto symbol_it = group_it->second.symbol_configs.find(order.symbol);
                    if (symbol_it != group_it->second.symbol_configs.end())
                    {
                        const auto &config = symbol_it->second;
                        if (order.price >= config.min_price && order.price <= config.max_price &&
                            order.volume >= config.min_volume && order.volume <= config.max_volume)
                        {
                            order.status = OrderStatus::PROCESSING;
                            benchmark::DoNotOptimize(order.status);
                        }
                    }
                }
            }
        }
    }
}

BENCHMARK(BM_OptimizedOrderProcessing);
BENCHMARK(BM_UnoptimizedOrderProcessing);
BENCHMARK(BM_OptimizedOrderStatusUpdate);
BENCHMARK(BM_UnoptimizedOrderStatusUpdate);
BENCHMARK_MAIN();
