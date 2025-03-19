#include <array>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>

class AsioClient {
public:
  AsioClient(boost::asio::io_context &io_context)
      : resolver_(io_context), socket_(io_context) {}

  std::future<bool> connect(const std::string &host, const std::string &port) {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    resolver_.async_resolve(
        host, port,
        [this, promise](const boost::system::error_code &ec,
                        boost::asio::ip::tcp::resolver::results_type results) {
          if (ec) {
            promise->set_value(false);
            return;
          }
          boost::asio::async_connect(
              socket_, results,
              [this, promise](const boost::system::error_code &ec,
                              const boost::asio::ip::tcp::endpoint &endpoint) {
                if (ec) {
                  promise->set_value(false);
                  return;
                }
                promise->set_value(true);
              });
        });
    return future;
  }

  void close() {
    if (socket_.is_open()) {
      boost::system::error_code ec;
      socket_.close(ec);
    }
  }

  ~AsioClient() { close(); }

  void write(const std::string &message) {
    boost::asio::write(socket_, boost::asio::buffer(message));
  }

  void read(std::function<void(const std::span<uint8_t> &)> callback) {
    boost::asio::async_read(
        socket_, boost::asio::buffer(buffer_),
        [this, callback](const boost::system::error_code &ec,
                         std::size_t length) {
          if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
              std::cerr << "Read failed: " << ec.message() << std::endl;
            }
            return;
          }
          callback(std::span<uint8_t>(buffer_.data(), length));
        });
  }

private:
  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ip::tcp::socket socket_;
  std::array<uint8_t, 1024> buffer_;
};

class AsioServer {
public:
  AsioServer(boost::asio::io_context &io_context)
      : io_context_(io_context),
        acceptor_(io_context, boost::asio::ip::tcp::endpoint(
                                  boost::asio::ip::tcp::v4(), 9090)),
        is_running_(false) {}

  ~AsioServer() { stop(); }

  void start() {
    is_running_ = true;
    do_accept();
  }

  void stop() {
    is_running_ = false;
    boost::system::error_code ec;
    acceptor_.close(ec);
    if (active_sockets_) {
      if (active_sockets_->is_open()) {
        active_sockets_->close(ec);
      }
    }
    active_sockets_.reset();
  }

  auto get_active_sockets() const { return active_sockets_; }

private:
  void do_accept() {
    if (!is_running_)
      return;

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(
        *socket, [this, socket](const boost::system::error_code &ec) {
          if (!is_running_)
            return;

          if (!ec) {
            // std::cout << "Accepted connection" << std::endl;
            active_sockets_ = socket;

            do_accept();
          } else if (ec != boost::asio::error::operation_aborted) {

            std::cerr << "Accept failed: " << ec.message() << std::endl;
          }
        });
  }

  boost::asio::io_context &io_context_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::shared_ptr<boost::asio::ip::tcp::socket> active_sockets_;
  bool is_running_;
};

class IoContextRunner {
public:
  explicit IoContextRunner(size_t thread_count = 1)
      : io_context_(), work_guard_(boost::asio::make_work_guard(io_context_)),
        thread_pool_(thread_count) {
    for (size_t i = 0; i < thread_count; ++i) {
      boost::asio::post(thread_pool_, [this]() { io_context_.run(); });
    }
  }

  boost::asio::io_context &get_io_context() { return io_context_; }

  void stop() {
    work_guard_.reset();
    io_context_.stop();
    thread_pool_.stop();
    thread_pool_.join();
  }

  ~IoContextRunner() { stop(); }

private:
  boost::asio::io_context io_context_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard_;
  boost::asio::thread_pool thread_pool_;
};

std::tuple<std::shared_ptr<IoContextRunner>, std::shared_ptr<AsioServer>,
           std::shared_ptr<AsioClient>, std::future<bool>>
setup_connection() {
  auto runner = std::make_shared<IoContextRunner>();
  auto &io_context = runner->get_io_context();

  auto server = std::make_shared<AsioServer>(io_context);
  server->start();

  auto client = std::make_shared<AsioClient>(io_context);
  auto future = client->connect("localhost", "9090");

  return std::make_tuple(runner, server, client, std::move(future));
}

void cleanup_connection(std::shared_ptr<IoContextRunner> runner,
                        std::shared_ptr<AsioServer> server,
                        std::shared_ptr<AsioClient> client) {
  client->close();
  server->stop();
  runner->stop();
}

TEST(asio_practice, TestConnect) {
  auto [runner, server, client, future] = setup_connection();

  auto status = future.wait_for(std::chrono::seconds(1));
  ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
  EXPECT_TRUE(future.get()) << "Connection failed";

  cleanup_connection(runner, server, client);
}

TEST(asio_practice, TestSimpleWrite) {
  auto [runner, server, client, future] = setup_connection();

  auto status = future.wait_for(std::chrono::seconds(1));
  ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
  EXPECT_TRUE(future.get()) << "Connection failed";

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  client->read([](const std::span<uint8_t> &data) {
    std::string received(reinterpret_cast<const char *>(data.data()),
                         data.size());
    EXPECT_EQ(received, "Hello from Server!");
  });

  std::string test_message = "Hello from Server!";
  auto len = boost::asio::write(*server->get_active_sockets(),
                                boost::asio::buffer(test_message));
  EXPECT_EQ(len, test_message.size());

  cleanup_connection(runner, server, client);
}

TEST(asio_practice, TestSimpleAsyncWrite) {
  auto [runner, server, client, future] = setup_connection();

  auto status = future.wait_for(std::chrono::seconds(1));
  ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
  EXPECT_TRUE(future.get()) << "Connection failed";

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  client->read([](const std::span<uint8_t> &data) {
    std::string received(reinterpret_cast<const char *>(data.data()),
                         data.size());
    EXPECT_EQ(received, "Hello from Server!");
  });

  std::string test_message = "Hello from Server!";
  auto future2 = boost::asio::async_write(*server->get_active_sockets(),
                                          boost::asio::buffer(test_message),
                                          boost::asio::use_future);
  EXPECT_EQ(future2.get(), test_message.size());

  cleanup_connection(runner, server, client);
}

TEST(asio_practice, TestSimpleCoroutineWrite) {
  auto [runner, server, client, future] = setup_connection();

  auto status = future.wait_for(std::chrono::seconds(1));
  ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
  EXPECT_TRUE(future.get()) << "Connection failed";

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  client->read([](const std::span<uint8_t> &data) {
    std::string received(reinterpret_cast<const char *>(data.data()),
                         data.size());
    EXPECT_EQ(received, "Hello from Server!");
  });

  std::string test_message = "Hello from Server!";
  std::promise<std::size_t> write_size_promise;
  auto write_size_future = write_size_promise.get_future();

  boost::asio::co_spawn(
      runner->get_io_context(),
      [&]() -> boost::asio::awaitable<void> {
        auto bytes_written = co_await boost::asio::async_write(
            *server->get_active_sockets(), boost::asio::buffer(test_message),
            boost::asio::use_awaitable);
        write_size_promise.set_value(bytes_written);
      },
      boost::asio::detached);

  EXPECT_EQ(write_size_future.get(), test_message.size());

  cleanup_connection(runner, server, client);
}