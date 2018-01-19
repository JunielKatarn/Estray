/**
 * @file async_server.hpp
 */

/*
 * The following license applies to the code in this file:
 *
 * **************************************************************************
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * **************************************************************************
 *
 * Author: Dr. Rüdiger Berlich of Gemfony scientific UG (haftungsbeschraenkt)
 * See http://www.gemfony.eu for further information.
 *
 * This code is based on the Beast Websocket library by Vinnie Falco, as published
 * together with Boost 1.66 and above. For further information on Beast, see
 * https://github.com/boostorg/beast for the latest release, or download
 * Boost 1.66 or newer from http://www.boost.org .
 */

#ifndef EVALUATOR_SERVER_HPP
#define EVALUATOR_SERVER_HPP

// Standard headers go here
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <cassert>

// Boost headers go here
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/random.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/cstdint.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/policies.hpp>

// Our own headers go here
#include "payload.hpp"
#include "misc.hpp"

/******************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////////
/******************************************************************************************/

class async_websocket_client final
	: public std::enable_shared_from_this<async_websocket_client>
{
	 //--------------------------------------------------------------
	 // Make the code easier to read

	 using error_code = boost::system::error_code;
	 using resolver = boost::asio::ip::tcp::resolver;
	 using socket = boost::asio::ip::tcp::socket;
	 using close_code = boost::beast::websocket::close_code;
	 using frame_type = boost::beast::websocket::frame_type;
	 using string_view = boost::beast::string_view;

public:
	 //--------------------------------------------------------------
	 // External "API"

	 // Initialization with host/ip and port
	 async_websocket_client(const std::string&, unsigned short);
	 // Starts the main async_start_run-loop
	 void run();

	 //--------------------------------------------------------------

	 // Deleted default-constructor -- enforce usage of a particular constructor
	 async_websocket_client() = delete;
	 // Deleted copy-constructors and assignment operators -- the client is non-copyable
	 async_websocket_client(const async_websocket_client&) = delete;
	 async_websocket_client(async_websocket_client&&) = delete;
	 async_websocket_client& operator=(const async_websocket_client&) = delete;
	 async_websocket_client& operator=(async_websocket_client&&) = delete;

private:
	 //--------------------------------------------------------------
	 // Communication and processing

	 void async_start_write(const std::string&);
	 void async_start_read();

	 void when_resolved(error_code, const resolver::results_type&);
	 void when_connected(error_code);
	 void when_handshake_complete(error_code);
	 void when_written(error_code, std::size_t);
	 void when_read(error_code, std::size_t);

	 std::string process_request();

	 void do_close(boost::beast::websocket::close_code);

	 /** @brief Callback for control frames */
	 std::function<void(boost::beast::websocket::frame_type, boost::beast::string_view)> f_when_control_frame_arrived;

	 //--------------------------------------------------------------
	 // Data

	 boost::asio::io_context m_io_context;
	 resolver m_resolver{m_io_context};
	 boost::beast::websocket::stream<socket> m_ws{m_io_context};

	 std::string m_address;
	 unsigned int m_port;

	 boost::beast::multi_buffer m_incoming_buffer;
	 std::string m_outgoing_message;

	 std::random_device m_nondet_rng; ///< Source of non-deterministic random numbers
	 std::mt19937 m_rng_engine{m_nondet_rng()}; ///< The actual random number engine, seeded my m_nondet_rng

	 boost::beast::websocket::close_code m_close_code
		 = boost::beast::websocket::close_code::normal; ///< Holds the close code when terminating the connection

	 command_container m_command_container{payload_command::NONE, nullptr}; ///< Holds the current command and payload (if any)

	 //--------------------------------------------------------------
};

/******************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////////
/******************************************************************************************/

class async_websocket_server_session final
	: public std::enable_shared_from_this<async_websocket_server_session>
{
	 // Simplify usage of namespaces
	 using error_code = boost::system::error_code;
	 using frame_type = boost::beast::websocket::frame_type;
	 using string_view = boost::beast::string_view;

public:
	 //--------------------------------------------------------------
	 // External "API"

	 // Takes ownership of the socket
	 async_websocket_server_session(
		 boost::asio::ip::tcp::socket /* socket */
		 , std::function<bool(payload_base*& plb_ptr)> /* get_next_payload_item */
		 , std::function<bool()> /* check_stopped */
		 , std::function<void(bool)> /* sign_on */
	 );

	 // The destructor
	 ~async_websocket_server_session() = default;

	 // Initiates all communication and processing
	 void async_start_run();

	 //--------------------------------------------------------------
	 // Deleted functions
	 async_websocket_server_session() = delete;
	 async_websocket_server_session(const async_websocket_server_session&) = delete;
	 async_websocket_server_session(async_websocket_server_session&&) = delete;
	 async_websocket_server_session& operator=(const async_websocket_server_session&) = delete;
	 async_websocket_server_session& operator=(async_websocket_server_session&&) = delete;

private:
	 //--------------------------------------------------------------
	 // Callbacks and connection functions

	 void async_start_read();
	 void async_start_write(const std::string&);
	 void async_start_ping();
	 void async_start_accept();

	 void when_ping_sent(error_code ec);
	 void when_timer_fired(error_code);
	 void when_connection_accepted(error_code);
	 void when_read(error_code, std::size_t);
	 void when_written(error_code, std::size_t);

	 /** @brief Callback for control frames */
	 std::function<void(boost::beast::websocket::frame_type, boost::beast::string_view)> f_when_control_frame_arrived;

	 void do_close(boost::beast::websocket::close_code);

	 // --------------------------------------------------------------
	 // Processing and interaction with the server

	 std::string process_request();
	 std::string getAndSerializeWorkItem();

	 // --------------------------------------------------------------
	 // Data

	 boost::beast::websocket::stream<boost::asio::ip::tcp::socket> m_ws;
	 boost::asio::strand<boost::asio::io_context::executor_type> m_strand;

	 boost::beast::multi_buffer m_incoming_buffer;
	 std::string m_outgoing_message;

	 std::function<bool(payload_base*& plb_ptr)> m_get_next_payload_item;

	 const std::chrono::seconds m_ping_interval{DEFAULTPINGINTERVAL}; // Time between two pings in seconds
	 boost::asio::steady_timer m_timer;
	 std::atomic<ping_state> m_ping_state{ping_state::CONNECTION_IS_ALIVE};
	 const boost::beast::websocket::ping_data m_ping_data{};

	 command_container m_command_container{payload_command::NONE, nullptr}; ///< Holds the current command and payload (if any)

	 std::function<bool()> m_check_stopped;
	 boost::beast::websocket::close_code m_close_code
		 = boost::beast::websocket::close_code::normal; ///< Holds the close code when terminating the connection

	 std::function<void(bool)> m_sign_on;

	 // --------------------------------------------------------------
};

/******************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////////
/******************************************************************************************/

class async_websocket_server final
	: public std::enable_shared_from_this<async_websocket_server>
{
	 // --------------------------------------------------------------
	 // Simplify usage of namespaces
	 using error_code = boost::system::error_code;

public:
	 // --------------------------------------------------------------
	 // External "API"

	 async_websocket_server(
	 	 const std::string& /* address */
		 , unsigned short /* port */
		 , std::size_t /* n_context_threads */
		 , std::size_t /* n_producer_threads */
		 , std::size_t /* n_max_packages_served */
		 , payload_type /* payload_type */
		 , std::size_t /* container_size */
	 	 , double /* sleep_time */
		 , std::size_t /* full_queue_sleep_ms */
	 	 , std::size_t /* max_queue_size */
	 );

	 void run();

	 // --------------------------------------------------------------
	 // Deleted default constructor, copy-/move-constructors and assignment operators.
	 // We want to enforce the usage of a single, specialized constructor.

	 async_websocket_server() = delete;
	 async_websocket_server(const async_websocket_server&) = delete;
	 async_websocket_server(async_websocket_server&&) = delete;
	 async_websocket_server& operator=(const async_websocket_server&) = delete;
	 async_websocket_server& operator=(async_websocket_server&&) = delete;

private:
	 // --------------------------------------------------------------
	 // Communication and data retrieval

	 void async_start_accept();
	 void when_accepted(error_code);

	 bool getNextPayloadItem(payload_base*&);

	 void container_payload_producer(std::size_t, std::size_t);
	 void sleep_payload_producer(double, std::size_t);

	 bool server_stopped() const;

	 // --------------------------------------------------------------
	 // Data and Queues

	 boost::asio::ip::tcp::endpoint m_endpoint;
	 std::size_t m_n_listener_threads;
	 boost::asio::io_context m_io_context{boost::numeric_cast<int>(m_n_listener_threads)};
	 boost::asio::ip::tcp::acceptor m_acceptor{m_io_context};
	 boost::asio::ip::tcp::socket m_socket{m_io_context};
	 std::vector<std::thread> m_context_thread_vec;
	 std::atomic<std::size_t> m_n_active_sessions{0};
	 std::atomic<std::size_t> m_n_packages_served{0};
	 const std::size_t m_n_max_packages_served;
	 const std::size_t m_full_queue_sleep_ms = 5;
	 const std::size_t m_max_queue_size = 5000;

	 std::atomic<bool> m_server_stopped{false};

	 payload_type m_payload_type = payload_type::container; ///< Indicates which sort of payload should be produced

	 std::size_t m_n_producer_threads = 4;
	 std::vector<std::thread> m_producer_threads_vec; ///< Holds threads used to produce payload packages

	 std::size_t m_container_size = 1000; ///< The size of container_payload objects
	 double m_sleep_time = 1.; ///< The sleep time of sleep_payload objects

	 // Holds payloads to be passed to the sessions
	 boost::lockfree::queue<payload_base *, boost::lockfree::fixed_sized<true>> m_payload_queue;

	 // --------------------------------------------------------------
};

/******************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////////
/******************************************************************************************/

#endif /* EVALUATOR_SERVER_HPP */
