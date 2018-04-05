#include "async_websocket_server.hpp"

/******************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////////
/******************************************************************************************/

async_websocket_client::async_websocket_client(
	const std::string& address
	, unsigned short port
	, bool verbose_control_frames
)
	: m_address(address)
	, m_port(port)
	, m_verbose_control_frames(verbose_control_frames)
{
	// Set the auto_fragment option, so control frames are delivered timely
	m_ws.auto_fragment(true);
	m_ws.write_buffer_size(16384);

	// Set the transfer mode according to the defines in CMakeLists.txt
	set_transfer_mode(m_ws);

	// Set a control-frame callback
	f_when_control_frame_arrived
		= [this](frame_type frame_t, string_view s)
	{
		if (this->m_verbose_control_frames)
		{
			if (boost::beast::websocket::frame_type::close == frame_t)
			{
				std::cout << "Client has received a close frame" << std::endl;
			}
			else if (boost::beast::websocket::frame_type::ping == frame_t)
			{
				std::cout << "Client has received a ping frame" << std::endl;
			}
			else if (boost::beast::websocket::frame_type::pong == frame_t)
			{
				std::cout << "Client has received a pong frame" << std::endl;
			}
		}
	};

	// Set the callback to be executed on every incoming control frame.
	m_ws.control_callback(f_when_control_frame_arrived);
}

/******************************************************************************************/

void async_websocket_client::run()
{
	// Start looking up the domain name. This call will return immediately,
	// when_resolved() will be called once the operation is complete.
	auto self = shared_from_this();
	m_resolver.async_resolve(
		m_address
		, std::to_string(m_port)
		, [self](
			boost::system::error_code ec
			, const resolver::results_type &results
			)
	{
		self->when_resolved(ec, results);
	}
	);

	// We need an additional thread for the processing of incoming work items
	std::thread processing_thread(
		[this]()
	{
		this->m_io_context.run();
	}
	);

	// This call will block until no more work remains in the ASIO work queue
	m_io_context.run();

	// Finally close all outstanding connections
	std::cout << "async_websocket_client::async_start_run(): Closing down remaining connections" << std::endl;
	processing_thread.join();
	do_close(m_close_code);
}

/******************************************************************************************/

void async_websocket_client::when_resolved(
	boost::system::error_code ec
	, const resolver::results_type &results
)
{
	if (ec)
	{
		std::cout
			<< "In async_websocket_client::when_resolved():" << std::endl
			<< "Got ec(\"" << ec.message() << "\"). async_connect() will not be executed." << std::endl
			<< "This will terminate the client." << std::endl;

		// Give the audience a hint why we are terminating
		m_close_code = boost::beast::websocket::close_code::going_away;

		return;
	}

	// Make the connection on the endpoint we get from a lookup
	auto self = shared_from_this();
	boost::asio::async_connect(
		m_ws.next_layer()
		, results.begin()
		, results.end()
		, [self](boost::system::error_code ec, boost::asio::ip::tcp::resolver::iterator /* unused */)
	{
		self->when_connected(ec);
	}
	);
}

/******************************************************************************************/

void async_websocket_client::when_connected(boost::system::error_code ec)
{
	if (ec)
	{
		std::cout
			<< "In async_websocket_client::when_connected():" << std::endl
			<< "Got ec(\"" << ec.message() << "\"). async_handshake() will not be executed." << std::endl
			<< "This will terminate the client." << std::endl;

		// Give the audience a hint why we are terminating
		m_close_code = boost::beast::websocket::close_code::going_away;

		return;
	}

	// Perform the websocket handshake
	auto self = shared_from_this();
	m_ws.async_handshake(
		m_address
		, "/"
		, [self](boost::system::error_code ec)
	{
		self->when_handshake_complete(ec);
	}
	);
}

/******************************************************************************************/

void async_websocket_client::when_handshake_complete(boost::system::error_code ec)
{
	if (ec)
	{
		std::cout
			<< "In async_websocket_client::when_handshake_complete():" << std::endl
			<< "Got ec(\"" << ec.message() << "\"). async_start_write() will not be executed." << std::endl
			<< "This will terminate the client." << std::endl;

		// Give the audience a hint why we are terminating
		m_close_code = boost::beast::websocket::close_code::going_away;

		// This will terminate the client
		return;
	}

	// Send the first command to the server
	async_start_write(
		m_command_container.reset(
			payload_command::GETDATA
		).to_string()
	);

	// Start the read cycle -- it will keep itself alife
	async_start_read();
}

/******************************************************************************************/

void async_websocket_client::async_start_write(const std::string& message)
{
	// We need to persist the message for asynchronous operations.
	// It is hence stored in a class variable.
	m_outgoing_message = message;

	// Send the message
	auto self = shared_from_this();
	m_ws.async_write(
		boost::asio::buffer(m_outgoing_message)
		, [self](
			boost::system::error_code ec
			, std::size_t nBytesTransferred
			)
	{
		self->when_written(ec, nBytesTransferred);
	}
	);
}

/******************************************************************************************/

void async_websocket_client::when_written(
	boost::system::error_code ec
	, std::size_t /* nothing */
)
{
	if (ec)
	{
		std::cout
			<< "In async_websocket_client::when_written():" << std::endl
			<< "Got ec(\"" << ec.message() << "\")." << std::endl
			<< "This will terminate the client." << std::endl;

		// Give the audience a hint why we are terminating
		m_close_code = boost::beast::websocket::close_code::going_away;

		// This will terminate the client
		return;
	}

	// Clear the outgoing message -- no longer needed
	m_outgoing_message.clear();
}

/******************************************************************************************/

void async_websocket_client::async_start_read()
{
	auto self = shared_from_this();
	m_ws.async_read(
		m_incoming_buffer
		, [self](
			boost::system::error_code ec
			, std::size_t nBytesTransferred
			)
	{
		self->when_read(ec, nBytesTransferred);
	}
	);
}

/******************************************************************************************/

void async_websocket_client::when_read(
	boost::system::error_code ec
	, std::size_t /* nothing */
)
{
	if (ec)
	{
		std::cout
			<< "In async_websocket_client::when_read():" << std::endl
			<< "Got ec(\"" << ec.message() << "\"). No  more processing will take place" << std::endl
			<< "This will terminate the client." << std::endl;

		// Give the audience a hint why we are terminating
		m_close_code = boost::beast::websocket::close_code::going_away;

		// This will terminate the client
		return;
	}

	// Deal with the message and send a response back. Processing
	// of work items is done inside of process_request().
	try
	{
		// Start asynchronous processing of the work item.
		// As we need to keep the read-cycle alife in parallel,
		// this requires that the io_context::run()-function is
		// started in at least two threads.
		auto self = shared_from_this();
		m_io_context.post(
			[self]()
		{
			self->process_request();
		}
		);

		// Start a new read cycle so we may react to control frames
		// (in particular ping and close)
		async_start_read();
	}
	catch (...)
	{
		// Give the audience a hint why we are terminating
		m_close_code = boost::beast::websocket::close_code::internal_error;
	}
}

/******************************************************************************************/

void async_websocket_client::process_request()
{
	// Extract the string from the buffer
	auto message = boost::beast::buffers_to_string(m_incoming_buffer.data());

	// De-serialize the object
	try
	{
		m_command_container.from_string(message);
	}
	catch (...)
	{
		throw std::runtime_error("async_websocket_client::process_request(): Caught exception while de-serializing");
	}

	// Clear the buffer, so we may later fill it with data to be sent
	m_incoming_buffer.consume(m_incoming_buffer.size());

	// Extract the command
	auto inboundCommand = m_command_container.get_command();

	// Act on the command received
	switch (inboundCommand)
	{
		case payload_command::COMPUTE: {
			// Process the work item
			m_command_container.process();

			// Set the command for the way back to the server
			m_command_container.set_command(payload_command::RESULT);
		} break;

		case payload_command::NODATA: // This must be a command payload
		case payload_command::ERRORR: { // We simply ask for new work
										// sleep for a short while (between 10 and 50 milliseconds, randomly),
										// before we ask for new work.
			std::uniform_int_distribution<> dist(10, 50);
			std::this_thread::sleep_for(std::chrono::milliseconds(dist(m_rng_engine)));

			// Tell the server again we need work
			m_command_container.reset(payload_command::GETDATA);
		} break;

		default: {
			//throw std::runtime_error(
			//	"async_websocket_client::process_request(): Got unknown or invalid command " + boost::lexical_cast<std::string>(inboundCommand)
			//);
		} break;
	}

	// Serialize the object again and return the result
	async_start_write(m_command_container.to_string());
}

/******************************************************************************************/

void async_websocket_client::do_close(close_code cc)
{
	if (m_ws.is_open())
	{
		m_ws.close(cc);
	}

	if (m_ws.next_layer().is_open())
	{
		boost::system::error_code ec;

		m_ws.next_layer().shutdown(socket::shutdown_both, ec);
		m_ws.next_layer().close(ec);

		if (ec)
		{
			std::cout
				<< "In async_websocket_client::do_close():" << std::endl
				<< "Got ec(\"" << ec.message() << "\")." << std::endl
				<< "We will throw an exception, as there are no other options left" << std::endl;

			// Not much more we can do
			throw std::runtime_error("async_websocket_client::do_close(): Shutdown of next layer has failed");
		}
	}
}
