

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/this_coro.hpp>

#include <async_mqtt5/mqtt_client.hpp>
#include <async_mqtt5/types.hpp>
#include <exception>
#include <iostream>
#include <string>

namespace asio = boost::asio;
namespace mqtt = async_mqtt5;

using client_type = mqtt::mqtt_client<asio::ip::tcp::socket>;

asio::awaitable<bool> subscribe(client_type& client)
{
    // Configure the request to subscribe to a Topic.
    async_mqtt5::subscribe_topic sub_topic = async_mqtt5::subscribe_topic{
        "3b688015-20ce-4da1-9636-15b11e8d8161/+",
        async_mqtt5::subscribe_options{
                                       async_mqtt5::qos_e::exactly_once,            // All messages will arrive at QoS 2.
            async_mqtt5::no_local_e::no,                 // Forward message from Clients with same ID.
            async_mqtt5::retain_as_published_e::retain,  // Keep the original RETAIN flag.
            async_mqtt5::retain_handling_e::send         // Send retained messages when the subscription is
                                                         // established.
        }
    };

    // Subscribe to a single Topic.
    auto&& [ec, sub_codes, sub_props] = co_await client.async_subscribe(
        sub_topic,
        async_mqtt5::subscribe_props{},
        asio::as_tuple
    );
    // Note: you can subscribe to multiple Topics in one mqtt_client::async_subscribe call.

    // An error can occur as a result of:
    //  a) wrong subscribe parameters
    //  b) mqtt_client::cancel is called while the Client is in the process of subscribing
    if (ec)
        std::cout << "Subscribe error occurred: " << ec.message() << std::endl;
    else
        std::cout << "Result of subscribe request: " << sub_codes[0].message() << std::endl;

    co_return !ec && !sub_codes[0];  // True if the subscription was successfully established.
}

asio::awaitable<void> subscribe_and_receive(client_type& client)
{
    // Before attempting to receive an Application Message from the Topic we just subscribed to,
    // it is advisable to verify that the subscription succeeded.
    // It is not recommended to call mqtt_client::async_receive if you do not have any
    // subscription established as the corresponding handler will never be invoked.
    if (!(co_await subscribe(client)))
        co_return;

    for (;;)
    {
        // Receive an Appplication Message from the subscribed Topic(s).
        auto&& [ec, topic, payload, publish_props] = co_await client.async_receive(asio::as_tuple);

        if (ec == mqtt::client::error::session_expired)
        {
            // The Client has reconnected, and the prior session has expired.
            // As a result, any previous subscriptions have been lost and must be reinstated.
            if (co_await subscribe(client))
                continue;
            else
                break;
        }
        else if (ec)
            break;

        std::cout << "Received message from the Broker" << std::endl;
        std::cout << "\t topic: " << topic << std::endl;
        std::cout << "\t payload: " << payload << std::endl;
    }

    co_return;
}

int main()
{
    // Initialise execution context.
    asio::io_context ioc;

    // Initialise the Client to connect to the Broker over TCP.
    client_type client(ioc);
    client.brokers("test.mosquitto.org:1883");

    client.async_run(asio::detached);

    // Set up signals to stop the program on demand.
    asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&client](async_mqtt5::error_code /* ec */, int /* signal */) {
        // After we are done with publishing all the messages, cancel the timer and the Client.
        // Alternatively, use mqtt_client::async_disconnect.
        client.cancel();
    });

    // Spawn the coroutine.
    asio::co_spawn(ioc, subscribe_and_receive(client), [](std::exception_ptr exc) {
        if (exc)
            std::rethrow_exception(exc);
    });

    // Start the execution.
    ioc.run();
}
