#include "logging.hpp"
#include "packets.hpp"
#include "sockets.hpp"

#include <cassert>
#include <cstring>
#include <errno.h>
#include <functional>
#include <linux/in.h>
#include <memory>
#include <mutex>
#include <string>
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

BaseSocket::BaseSocket(int port)
: BaseSocket(
	socket(AF_INET, SOCK_STREAM, 0),
	[&]() {
		sockaddr_in info {
			.sin_family = AF_INET,
			.sin_port = htons(port),
			.sin_addr = { .s_addr = htonl(INADDR_ANY) }
		};

		return info;
	}()
) { }

BaseSocket::BaseSocket(SocketFd socket_fd, sockaddr_in socket_info)
: socket_fd(socket_fd), socket_info(socket_info) { }

BaseSocket::~BaseSocket() {
	this->close();
}

void BaseSocket::init() {
	if (this->is_ready) return;
	assert(this->socket_fd > 0 && "Socket is invalid");

	int on = 1;
	int result_1 = setsockopt(
		this->socket_fd,
		SOL_SOCKET,
		SO_REUSEADDR,
		&on,
		sizeof(on)
	);

	int result_2 = bind(
		this->socket_fd,
		reinterpret_cast<sockaddr*>(
			const_cast<sockaddr_in*>(&socket_info)
		),
		sizeof(socket_info)
	);

	this->is_ready = (result_1 == 0 && result_2 == 0);
	if (!this->is_ready) {
		Logging::get_logger()->warning("Socket is still not ready!");
		if (result_1 == -1) Logging::get_logger()->warning("setsockopt failed! " + std::to_string(result_1));
		if (result_2 == -1) Logging::get_logger()->warning("bind failed! " + std::to_string(result_2));
	}
}

void BaseSocket::close() {
	if (this->socket_fd > 0) {
		shutdown(socket_fd, SHUT_RDWR);
		::close(socket_fd);

		this->socket_fd = -1;
		this->is_ready = false;

		Logging::get_logger()->debug("Successfully shutted down the socket");
	}
}

void BaseSocket::send_data(bytearray& data) {
	assert(this->is_ready && "Socket is not ready!");
	assert(data.capacity() > 0);

	if (send(
			this->socket_fd,
			data.data(),
			data.size(),
			0
		) == -1
	) {
		Logging::get_logger()->warning(
			"Failed to send " + std::to_string(data.size()) + " amount of data"
		);
	}
}

bytearray BaseSocket::recieve_data(size_t data_size, bool ensure_full) {
	assert(this->is_ready && "Socket is not ready!");
	assert(data_size > 0);

	bytearray buf(data_size);

	int bytes_recieved = recv(
		this->socket_fd, buf.data(),
		data_size, 
		ensure_full ? MSG_WAITALL : 0
	);

	if (bytes_recieved == -1) {
		Logging::get_logger()->warning(
			"Failed to recieve " + std::to_string(data_size) + " data"
		);
	}

	return buf;
}

void BaseSocket::recieve_data(bytearray& data, bool ensure_full) {
	assert(this->is_ready && "Socket is not ready!");
	assert(data.capacity() > 0);

	data = this->recieve_data(data.capacity(), ensure_full);
}

PollingSocket::~PollingSocket() {
	Logging::get_logger()->debug("Stopping polling thread...");

	this->interrupt_polling();
	this->close();
	if (this->polling_thread.joinable()) this->polling_thread.join();
}

void PollingSocket::init() {
	BaseSocket::init();
	
	Logging::get_logger()->debug("Starting polling thread...");
	this->polling_thread = std::thread(&PollingSocket::poll, this);
}

ServerClientSocket::ServerClientSocket(
	ServerSocket* parent,
	SocketFd client_fd,
	sockaddr_in client_info
) : PollingSocket(client_fd, client_info), parent(parent) {
	this->is_ready = true;
}

void ServerClientSocket::init() {
	PollingSocket::init();

	this->start_polling();
}

void ServerClientSocket::disconnect() {
	this->interrupt_polling();
	this->close();

	if (this->polling_thread.joinable()) this->polling_thread.join();

	this->parent->schedule_client_removal(this);
}

void ServerClientSocket::poll() {
	PacketHeader header { };

	while (!this->poll_interrupt.load()) {
		if (this->should_poll.load()) {
			bytearray header_bytes = this->recieve_data(sizeof(PacketHeader), true);
			if (header_bytes.empty()) continue;
			if (header_bytes.size() > 0 || header_bytes.size() < sizeof(PacketHeader)) {
				Logging::get_logger()->error("Client disconnected!");
				this->disconnect();
				
				break;
			}

			std::memcpy(&header, header_bytes.data(), sizeof(PacketHeader));
			if (!header.length) continue;

			bytearray data = this->recieve_data(header.length, true);
			if (data.size() > 0 || data.size() < header.length) {
				Logging::get_logger()->error("Client disconnected!");
				this->disconnect();
				
				break;
			}

			this->parent->dispatch_packet_event(this, Packet(header, data));
		}
	}

	Logging::get_logger()->debug("Successfully shutdown polling thread!");
}

ServerSocket::ServerSocket(int port)
: PollingSocket(port) {
	Logging::get_logger()->info("Instantiating socket server on port " + std::to_string(port) + "...");
}

ServerSocket::~ServerSocket() {
	Logging::get_logger()->info("Shutting down socket server...");
	this->clients.clear();
}

void ServerSocket::init() {
	PollingSocket::init();

	assert(this->is_socket_ready() && "Socket is not ready!");
	if (listen(this->get_socket_fd(), 1) == -1) {
		Logging::get_logger()->error("Failed to make socket server listenable!");
		Logging::get_logger()->error(strerror(errno));

		return;
	}

	Logging::get_logger()->info("Will now listen for clients.");

	this->start_polling();
}

void ServerSocket::send_packet_to_all(Packet packet) {
	bytearray data = packet.to_sendable_data();

	std::lock_guard<std::mutex> lock(this->pending_client_removal_lock);
	for (const auto& client : this->clients) client.second->send_data(data);
}

void ServerSocket::on(std::function<void(ServerClientSocket*, Packet)> callback) {
	this->packet_listeners.push_back(callback);
}

void ServerSocket::on(PacketType type, std::function<void(ServerClientSocket*, Packet)> callback) {
	this->specific_packet_listeners[type].push_back(callback);
}

void ServerSocket::dispatch_packet_event(ServerClientSocket* from, Packet packet) {
	for (const auto& callback : this->specific_packet_listeners[packet.get_packet_header().type]) {
		callback(from, packet);
	}

	for (const auto& callback : this->packet_listeners) {
		callback(from, packet);
	}
}

void ServerSocket::schedule_client_removal(ServerClientSocket* client) {    
	std::lock_guard<std::mutex> lock(this->pending_client_removal_lock);
	pending_client_removal.push_back(client);
}

void ServerSocket::poll() {
	sockaddr_in client_info = { };
	socklen_t client_info_length = sizeof(client_info);

	while (!this->poll_interrupt.load()) {
		{
			std::lock_guard<std::mutex> lock(this->pending_client_removal_lock);
			if (!pending_client_removal.empty()) {
				for (const auto& client : pending_client_removal) {
					auto it = std::find_if(
						clients.begin(), clients.end(),
        				[client](const auto& pair) {
            				return (*client) == (*pair.second.get());
        				}
    				);

    				if (it != clients.end()) {
						Logging::get_logger()->info("Removed client from connected clients.");
						clients.erase(it);
    				}
				}
			}
		}

		if (this->should_poll.load()) {
			SocketFd client_fd = accept(
				this->get_socket_fd(), reinterpret_cast<sockaddr*>(&client_info), &client_info_length
			);

			if (client_fd == -1) continue;
			Logging::get_logger()->info("A client connected!");

			this->clients.insert({
				client_fd,
				std::make_unique<ServerClientSocket>(
					this,
					client_fd,
					client_info
				)
			});
		}
	}

	Logging::get_logger()->debug("Successfully shutdown polling thread!");
}

void ClientSocket::connect(sockaddr_in target_info) {
	assert(this->is_socket_ready() && "Socket is not ready! Call init() first!");

	Logging::get_logger()->info("Connecting to " + std::to_string(target_info.sin_addr.s_addr) + ":" + std::to_string(target_info.sin_port));

	if (
		::connect(
			this->get_socket_fd(),
			reinterpret_cast<sockaddr*>(&target_info),
			sizeof(target_info)
		) == -1
	) {
		Logging::get_logger()->error("Failed to connect to " + std::to_string(target_info.sin_addr.s_addr) + ":" + std::to_string(target_info.sin_port));
		Logging::get_logger()->error(strerror(errno));
		
		return;
	}

	this->start_polling();	
}

void ClientSocket::disconnect() {
	this->interrupt_polling();
	this->close();

	if (this->polling_thread.joinable()) this->polling_thread.join();
}


void ClientSocket::on(std::function<void(Packet)> callback) {
	this->packet_listeners.push_back(callback);
}

void ClientSocket::on(PacketType type, std::function<void(Packet)> callback) {
	this->specific_packet_listeners[type].push_back(callback);
}

void ClientSocket::dispatch_packet_event(Packet packet) {
	for (const auto& callback : this->specific_packet_listeners[packet.get_packet_header().type]) {
		callback(packet);
	}

	for (const auto& callback : this->packet_listeners) {
		callback(packet);
	}
}

void ClientSocket::poll() {
	PacketHeader header { };

	while (!this->poll_interrupt.load()) {
		if (this->should_poll.load()) {
			bytearray header_bytes = this->recieve_data(sizeof(PacketHeader), true);
			if (header_bytes.empty()) continue;
			if (header_bytes.size() > 0 || header_bytes.size() < sizeof(PacketHeader)) {
				Logging::get_logger()->error("Client disconnected!");
				this->close();
				
				break;
			}

			std::memcpy(&header, header_bytes.data(), sizeof(PacketHeader));
			if (!header.length) continue;

			bytearray data = this->recieve_data(header.length);
			if (data.size() > 0 || data.size() < header.length) {
				Logging::get_logger()->error("Client disconnected!");
				this->close();
				
				break;
			}

			this->dispatch_packet_event(Packet(header, data));
		}
	}

	Logging::get_logger()->debug("Successfully shutdown polling thread!");
}
