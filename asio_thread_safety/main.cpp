#include <array>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <filesystem>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <span>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
class AsioClient
{
  public:
    AsioClient(boost::asio::io_context &io_context) : resolver_(io_context), socket_(io_context)
    {
    }

    std::future<bool> connect(const std::string &host, const std::string &port)
    {
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        resolver_.async_resolve(
            host, port,
            [this, promise](const boost::system::error_code &ec, boost::asio::ip::tcp::resolver::results_type results) {
                if (ec)
                {
                    promise->set_value(false);
                    return;
                }

                boost::asio::async_connect(socket_, results,
                                           [this, promise](const boost::system::error_code &ec,
                                                           const boost::asio::ip::tcp::endpoint &endpoint) {
                                               if (ec)
                                               {
                                                   promise->set_value(false);
                                                   return;
                                               }

                                               promise->set_value(true);
                                           });
            });
        return future;
    }

    void close()
    {
        if (socket_.is_open())
        {
            boost::system::error_code ec;
            socket_.close(ec);
        }
    }

    ~AsioClient()
    {
        close();
    }

    void write(const std::string &message)
    {
        boost::asio::write(socket_, boost::asio::buffer(message));
    }

    void read(std::function<bool(size_t)> payload_length_consumer,
              std::function<bool(const std::span<uint8_t> &)> payload_consumer)
    {
        do_read(payload_length_consumer, payload_consumer);
    }

  private:
    void do_read(std::function<bool(size_t)> payload_length_consumer,
                 std::function<bool(const std::span<uint8_t> &)> payload_consumer)
    {
        boost::asio::async_read(
            socket_, boost::asio::buffer(&payload_length_, sizeof(payload_length_)),
            [this, payload_length_consumer, payload_consumer](const boost::system::error_code &ec, std::size_t length) {
                if (ec)
                {
                    if (ec != boost::asio::error::operation_aborted)
                    {
                        std::cerr << "Read failed: " << ec.message() << std::endl;
                    }

                    return;
                }

                size_t payload_length = *reinterpret_cast<size_t *>(payload_length_);
                if (not payload_length_consumer(payload_length))
                    return;
                payload_ = std::vector<uint8_t>(payload_length, 0);
                boost::asio::async_read(socket_, boost::asio::buffer(payload_.data(), payload_.size()),
                                        [this, payload_length_consumer,
                                         payload_consumer](const boost::system::error_code &ec, std::size_t length) {
                                            if (ec)
                                            {
                                                if (ec != boost::asio::error::operation_aborted)
                                                {
                                                    std::cerr << "Read failed: " << ec.message() << std::endl;
                                                }

                                                return;
                                            }

                                            if (not payload_consumer or not payload_consumer(std::span<uint8_t>(
                                                                            payload_.data(), payload_.size())))
                                                return;
                                            do_read(payload_length_consumer, payload_consumer);
                                        });
            });
    }

    uint8_t payload_length_[8];
    std::vector<uint8_t> payload_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
};
class AsioServer
{
  public:
    AsioServer(boost::asio::io_context &io_context)
        : io_context_(io_context),
          acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 9090)), is_running_(false)
    {
    }

    ~AsioServer()
    {
        stop();
    }

    void start()
    {
        is_running_ = true;
        do_accept();
    }

    void stop()
    {
        is_running_ = false;
        boost::system::error_code ec;
        acceptor_.close(ec);
        if (active_sockets_)
        {
            if (active_sockets_->is_open())
            {
                active_sockets_->close(ec);
            }
        }

        active_sockets_.reset();
    }

    auto get_active_sockets() const
    {
        return active_sockets_;
    }

  private:
    void do_accept()
    {
        if (!is_running_)
            return;
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
        acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code &ec) {
            if (!is_running_)
                return;
            if (!ec)
            {
                active_sockets_ = socket;
                do_accept();
            }

            else if (ec != boost::asio::error::operation_aborted)
            {
                std::cerr << "Accept failed: " << ec.message() << std::endl;
            }
        });
    }

    boost::asio::io_context &io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<boost::asio::ip::tcp::socket> active_sockets_;
    bool is_running_;
};
class IoContextRunner
{
  public:
    explicit IoContextRunner(size_t thread_count = 1)
        : io_context_(), work_guard_(boost::asio::make_work_guard(io_context_)), thread_pool_(thread_count)
    {
        for (size_t i = 0; i < thread_count; ++i)
        {
            boost::asio::post(thread_pool_, [this]() { io_context_.run(); });
        }
    }

    boost::asio::io_context &get_io_context()
    {
        return io_context_;
    }

    void stop()
    {
        work_guard_.reset();
        io_context_.stop();
        thread_pool_.stop();
        thread_pool_.join();
    }

    ~IoContextRunner()
    {
        stop();
    }

  private:
    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    boost::asio::thread_pool thread_pool_;
};
std::shared_ptr<spdlog::logger> setup_logger(const std::string &test_name)
{
    std::filesystem::create_directories("logs");
    auto log_file = fmt::format("logs/{}.log", test_name);
    auto logger = spdlog::basic_logger_mt(test_name, log_file, true);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    logger->set_level(spdlog::level::debug);
    return logger;
}

void cleanup_logs(const std::string &test_name)
{
    spdlog::drop(test_name);
    auto log_file = fmt::format("logs/{}.log", test_name);
    std::filesystem::remove(log_file);
}

std::tuple<std::shared_ptr<IoContextRunner>, std::shared_ptr<AsioServer>, std::shared_ptr<AsioClient>,
           std::future<bool>>
setup_connection(const std::string &test_name = "", size_t thread_count = 1)
{
    cleanup_logs(test_name.empty() ? "default" : test_name);
    auto logger = setup_logger(test_name.empty() ? "default" : test_name);
    logger->info("Setting up connection...");
    auto runner = std::make_shared<IoContextRunner>(thread_count);
    auto &io_context = runner->get_io_context();
    logger->debug("IO context created");
    auto server = std::make_shared<AsioServer>(io_context);
    server->start();
    logger->debug("Server started");
    auto client = std::make_shared<AsioClient>(io_context);
    auto future = client->connect("localhost", "9090");
    logger->debug("Client connection initiated");
    return std::make_tuple(runner, server, client, std::move(future));
}

void cleanup_connection(std::shared_ptr<IoContextRunner> runner, std::shared_ptr<AsioServer> server,
                        std::shared_ptr<AsioClient> client, const std::string &test_name = "")
{
    auto logger = spdlog::get(test_name.empty() ? "default" : test_name);
    {
        logger->info("Cleaning up connection...");
        logger->debug("Closing client connection");
    }

    client->close();
    logger->debug("Stopping server");
    server->stop();
    logger->debug("Stopping IO context runner");
    runner->stop();
    logger->info("Cleanup complete");
    cleanup_logs(test_name.empty() ? "default" : test_name);
}

std::vector<uint8_t> create_payload(const std::string &message)
{
    size_t payload_length = message.size();
    std::vector<uint8_t> payload(sizeof(payload_length) + message.size());
    std::memcpy(payload.data(), &payload_length, sizeof(payload_length));
    std::memcpy(payload.data() + sizeof(payload_length), message.data(), message.size());
    return payload;
}

bool wait_for_connection(std::future<bool> &future, const std::string &test_name)
{
    auto logger = spdlog::get(test_name);
    auto status = future.wait_for(std::chrono::seconds(1));
    EXPECT_EQ(status, std::future_status::ready) << "Connection timed out";
    bool success = future.get();
    EXPECT_TRUE(success) << "Connection failed";
    logger->info("Connection established");
    return success;
}

auto create_message_validator(const std::string &expected_message, std::promise<void> &read_complete,
                              const std::string &test_name)
{
    auto logger = spdlog::get(test_name);
    return std::make_pair(
        [&expected_message, logger](size_t payload_length) -> bool {
            logger->debug("Received payload length: {}", payload_length);
            return payload_length == expected_message.length();
        },
        [&read_complete, &expected_message, logger](const std::span<uint8_t> &data) {
            std::string received(reinterpret_cast<const char *>(data.data()), data.size());
            logger->debug("Received message: {}", received);
            EXPECT_EQ(received, expected_message);
            read_complete.set_value();
            return true;
        });
}

auto create_bulk_validator(size_t message_length, size_t thread_count, size_t each_thread_write_count,
                           std::atomic_size_t &read_count, std::promise<bool> &read_complete,
                           const std::string &test_name, std::atomic_bool &stop)
{
    auto logger = spdlog::get(test_name);
    return std::make_pair(
        [message_length, logger](size_t payload_length) -> bool {
            logger->debug("Received payload length: {}", payload_length);
            if (payload_length == message_length)
            {
                return true;
            }

            else
            {
                logger->error("Invalid payload length: {}", payload_length);
                return false;
            }
        },
        [&read_count, &read_complete, message_length, thread_count, each_thread_write_count, logger,
         &stop](const std::span<uint8_t> &data) {
            bool all_same =
                std::all_of(data.begin(), data.end(), [first = data[0]](uint8_t value) { return value == first; });
            if (all_same)
            {
                logger->debug("Data is all the same of {}", data[0]);
            }

            else
            {
                logger->debug("Data: {}", fmt::join(data, " "));
            }

            if (not all_same)
            {
                logger->error("Data corruption detected");
                stop.store(true);
                read_complete.set_value(false);
                return false;
            }

            size_t current_count = read_count.fetch_add(data.size());
            logger->debug("Total bytes read: {}, target: {}", current_count + data.size(),
                          message_length * thread_count * each_thread_write_count);
            if (current_count + data.size() >= message_length * thread_count * each_thread_write_count)
            {
                logger->info("All data received successfully");
                read_complete.set_value(true);
            }

            return true;
        });
}

void sync_write(const std::shared_ptr<AsioServer> &server, const std::vector<uint8_t> &payload,
                const std::string &test_name)
{
    auto logger = spdlog::get(test_name);
    logger->debug("Starting sync write with payload size: {}", payload.size());
    boost::asio::write(*server->get_active_sockets(), boost::asio::buffer(payload));
    logger->info("Write operation completed");
}

void async_write(const std::shared_ptr<AsioServer> &server, const std::vector<uint8_t> &payload,
                 const std::string &test_name)
{
    auto logger = spdlog::get(test_name);
    logger->debug("Starting async write with payload size: {}", payload.size());
    auto write_future =
        boost::asio::async_write(*server->get_active_sockets(), boost::asio::buffer(payload), boost::asio::use_future);
    EXPECT_EQ(write_future.get(), payload.size());
    logger->info("Write operation completed");
}

void coroutine_write(const std::shared_ptr<IoContextRunner> &runner, const std::shared_ptr<AsioServer> &server,
                     const std::vector<uint8_t> &payload, std::promise<std::size_t> &write_size_promise,
                     const std::string &test_name)
{
    auto logger = spdlog::get(test_name);
    logger->debug("Starting coroutine write with payload size: {}", payload.size());
    boost::asio::co_spawn(
        runner->get_io_context(),
        [&, logger]() -> boost::asio::awaitable<void> {
            auto bytes_written = co_await boost::asio::async_write(
                *server->get_active_sockets(), boost::asio::buffer(payload), boost::asio::use_awaitable);
            logger->debug("Coroutine write completed, bytes written: {}", bytes_written);
            write_size_promise.set_value(bytes_written);
        },
        boost::asio::detached);
}

template <typename WriteFunc>
void multi_thread_write(const std::shared_ptr<IoContextRunner> &runner, const std::shared_ptr<AsioServer> &server,
                        size_t message_length, size_t thread_count, size_t each_thread_write_count,
                        std::atomic_bool &stop, WriteFunc write_func, const std::string &test_name)
{
    auto logger = spdlog::get(test_name);
    std::jthread threads[thread_count];
    for (size_t i = 0; i < thread_count; ++i)
    {
        threads[i] = std::jthread([&, i]() {
            logger->debug("Thread {} started", i);
            for (size_t j = 0; j < each_thread_write_count; ++j)
            {
                if (stop.load())
                {
                    logger->debug("Thread {} stopped early", i);
                    return;
                }

                std::string message(message_length, 'a' + i);
                auto payload = create_payload(message);
                write_func(i, j, payload);
            }

            logger->info("Thread {} completed all writes", i);
        });
    }
}

TEST(asio_practice, Connect)
{
    std::string test_name = "Connect";
    auto [runner, server, client, future] = setup_connection(test_name);
    wait_for_connection(future, test_name);
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, SimpleWrite)
{
    std::string test_name = "SimpleWrite";
    auto [runner, server, client, future] = setup_connection(test_name);
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::promise<void> read_complete;
    auto read_future = read_complete.get_future();
    std::string message = "Hello from Server!";
    auto [length_validator, payload_validator] = create_message_validator(message, read_complete, test_name);
    client->read(length_validator, payload_validator);
    auto payload = create_payload(message);
    sync_write(server, payload, test_name);
    read_future.get();
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, SimpleAsyncWrite)
{
    std::string test_name = "SimpleAsyncWrite";
    auto [runner, server, client, future] = setup_connection(test_name);
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::promise<void> read_complete;
    auto read_future = read_complete.get_future();
    std::string message = "Hello from Server!";
    auto [length_validator, payload_validator] = create_message_validator(message, read_complete, test_name);
    client->read(length_validator, payload_validator);
    auto payload = create_payload(message);
    async_write(server, payload, test_name);
    read_future.get();
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, SimpleCoroutineWrite)
{
    std::string test_name = "SimpleCoroutineWrite";
    auto [runner, server, client, future] = setup_connection(test_name);
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::promise<void> read_complete;
    auto read_future = read_complete.get_future();
    std::string message = "Hello from Server!";
    auto [length_validator, payload_validator] = create_message_validator(message, read_complete, test_name);
    client->read(length_validator, payload_validator);
    std::promise<std::size_t> write_size_promise;
    auto write_size_future = write_size_promise.get_future();
    auto payload = create_payload(message);
    coroutine_write(runner, server, payload, write_size_promise, test_name);
    EXPECT_EQ(write_size_future.get(), payload.size());
    read_future.get();
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, AsyncWriteBulkOnce)
{
    std::string test_name = "AsyncWriteBulkOnce";
    auto [runner, server, client, future] = setup_connection(test_name);
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t message_length = 1024 * 1024;
    std::promise<void> read_complete;
    auto read_future = read_complete.get_future();
    std::string message(message_length, 'a');
    auto [length_validator, payload_validator] = create_message_validator(message, read_complete, test_name);
    client->read(length_validator, payload_validator);
    auto payload = create_payload(message);
    async_write(server, payload, test_name);
    read_future.get();
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, AsyncWriteMultithreadWithSingleThreadIoContext)
{
    std::string test_name = "AsyncWriteMultithreadWithSingleThreadIoContext";
    auto [runner, server, client, future] = setup_connection(test_name);
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t message_length = 1024 * 1024;
    size_t thread_count = std::thread::hardware_concurrency();
    size_t each_thread_write_count = std::numeric_limits<size_t>::max();
    std::atomic_size_t read_count = 0;
    std::atomic_bool stop = false;
    std::promise<bool> read_complete;
    auto read_future = read_complete.get_future();
    auto [length_validator, payload_validator] = create_bulk_validator(
        message_length, thread_count, each_thread_write_count, read_count, read_complete, test_name, stop);
    client->read(length_validator, payload_validator);
    multi_thread_write(
        runner, server, message_length, thread_count, each_thread_write_count, stop,
        [&](size_t i, size_t j, const std::vector<uint8_t> &payload) { async_write(server, payload, test_name); },
        test_name);
    bool success = read_future.get();
    EXPECT_FALSE(success);
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, AsyncWriteMultithreadWithMultiThreadIoContext)
{
    std::string test_name = "AsyncWriteMultithreadWithMultiThreadIoContext";
    auto [runner, server, client, future] = setup_connection(test_name, std::thread::hardware_concurrency());
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t message_length = 1024 * 1024;
    size_t thread_count = std::thread::hardware_concurrency();
    size_t each_thread_write_count = std::numeric_limits<size_t>::max();
    std::atomic_size_t read_count = 0;
    std::atomic_bool stop = false;
    std::promise<bool> read_complete;
    auto read_future = read_complete.get_future();
    auto [length_validator, payload_validator] = create_bulk_validator(
        message_length, thread_count, each_thread_write_count, read_count, read_complete, test_name, stop);
    client->read(length_validator, payload_validator);
    multi_thread_write(
        runner, server, message_length, thread_count, each_thread_write_count, stop,
        [&](size_t i, size_t j, const std::vector<uint8_t> &payload) { async_write(server, payload, test_name); },
        test_name);
    bool success = read_future.get();
    EXPECT_FALSE(success);
    cleanup_connection(runner, server, client, test_name);
}

TEST(asio_practice, CoroutineWriteMultithreadWithSingleThreadIoContext)
{
    std::string test_name = "CoroutineWriteMultithreadWithSingleThreadIoContext";
    auto [runner, server, client, future] = setup_connection(test_name);
    ASSERT_TRUE(wait_for_connection(future, test_name));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t message_length = 1024 * 1024;
    size_t thread_count = std::thread::hardware_concurrency();
    size_t each_thread_write_count = 1000;
    std::atomic_size_t read_count = 0;
    std::atomic_bool stop = false;
    std::promise<bool> read_complete;
    auto read_future = read_complete.get_future();
    auto [length_validator, payload_validator] = create_bulk_validator(
        message_length, thread_count, each_thread_write_count, read_count, read_complete, test_name, stop);
    client->read(length_validator, payload_validator);
    multi_thread_write(
        runner, server, message_length, thread_count, each_thread_write_count, stop,
        [&](size_t i, size_t j, const std::vector<uint8_t> &payload) {
            std::promise<std::size_t> write_size_promise;
            auto write_size_future = write_size_promise.get_future();
            coroutine_write(runner, server, payload, write_size_promise, test_name);
            EXPECT_EQ(write_size_future.get(), payload.size());
        },
        test_name);
    bool success = read_future.get();
    EXPECT_FALSE(success);
    cleanup_connection(runner, server, client, test_name);
}
