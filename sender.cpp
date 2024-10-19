

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/cancellation_type.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/error_code.hpp>

#include <async_mqtt5/mqtt_client.hpp>
#include <async_mqtt5/types.hpp>
#include <chrono>
#include <exception>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

namespace asio = boost::asio;
namespace mqtt = async_mqtt5;

using boost::system::error_code;

using client_type = mqtt::mqtt_client<asio::ip::tcp::socket>;

static std::random_device rd;

// Simulate some sensors
class sensor
{
    std::uniform_real_distribution<> dist_;
    std::default_random_engine eng_{rd()};

public:
    sensor(double minval, double maxval) : dist_(minval, maxval) {}
    double read() { return dist_(eng_); }
};

struct sensors
{
    sensor speed{30.0, 60.0};        // rpm
    sensor temperature{25.0, 40.0};  // ºC
};

// A period task reading a sensor
asio::awaitable<void> read_sensor(
    sensor& sensor_to_read,
    std::string_view sensor_name,
    std::chrono::milliseconds period,
    client_type& cli
)
{
    // Create a timer to perform wait
    asio::steady_timer timer(co_await asio::this_coro::executor);

    // Wait loop
    auto next_tp = std::chrono::steady_clock::now();
    while (true)
    {
        // Read the sensor
        double measure = sensor_to_read.read();

        // Publish the measure
        co_await cli.async_publish<mqtt::qos_e::at_most_once>(
            std::string(sensor_name),
            std::to_string(measure),
            mqtt::retain_e::yes,
            mqtt::publish_props{}
        );

        // Wait until the next measure is due
        next_tp += period;
        timer.expires_at(next_tp);
        auto [ec] = co_await timer.async_wait(asio::as_tuple(asio::deferred));
        if (ec)
            co_return;
    }
}

int main()
{
    // Initialise execution context.
    asio::io_context ioc;

    // Initialize the sensors
    sensors s;

    // Initialise the Client to connect to the Broker over TCP.
    client_type client(ioc);
    client.brokers("test.mosquitto.org:1883");
    asio::cancellation_signal sig1, sig2;

    // Set up signals to stop the program on demand.
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](async_mqtt5::error_code /* ec */, int /* signal */) {
        // After we are done with publishing all the messages, cancel the timer and the Client.
        // Alternatively, use mqtt_client::async_disconnect.
        client.async_disconnect(asio::detached);
        sig1.emit(asio::cancellation_type_t::terminal);
        sig2.emit(asio::cancellation_type_t::terminal);
    });

    // Launch the runner
    client.async_run([](error_code ec) {
        std::cout << "Client finished with error code: " << ec << std::endl;
    });

    // // Spawn the readers
    auto rethrow = [](std::exception_ptr exc) {
        if (exc)
            std::rethrow_exception(exc);
    };
    asio::co_spawn(
        ioc,
        [&] {
            return read_sensor(
                s.speed,
                "3b688015-20ce-4da1-9636-15b11e8d8161/speed",
                std::chrono::milliseconds(1000),
                client
            );
        },
        asio::bind_cancellation_slot(sig1.slot(), rethrow)
    );
    asio::co_spawn(
        ioc,
        [&] {
            return read_sensor(
                s.temperature,
                "3b688015-20ce-4da1-9636-15b11e8d8161/temperature",
                std::chrono::seconds(5),
                client
            );
        },
        asio::bind_cancellation_slot(sig2.slot(), rethrow)
    );

    // Start the execution.
    ioc.run();
}
