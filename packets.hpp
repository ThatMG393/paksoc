#pragma once

#include "logging.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#define PACKED __attribute__((packed))

typedef uint8_t byte;
typedef std::vector<byte> bytearray;

typedef uint16_t PacketType;
typedef uint64_t PacketLength;

struct PACKED PacketHeader {
	PacketType type;
	PacketLength length;
};

class Packet;

class Serializeable {
public:
	virtual void deserialize(Packet& packet) = 0;
	virtual void serialize(Packet& packet) = 0;
};

class Packet {
private:
	PacketHeader header;
	bytearray data;

	size_t offset;

	bool can_read(size_t length) {
		if (offset + length > data.size()) {
			Logging::get_logger()->warning("Attempted to read " + std::to_string(length) + " more bytes, but there's not enough data left!");
			return false;
		}

		return true;
	}

public:
	Packet(PacketType type) : header({ .type = type, .length = 0 }) { }
	Packet(PacketType type, const bytearray& data) : header({ .type = type, .length = data.size() }), data(data) { }
	Packet(const PacketHeader header, const bytearray& data) : header(header), data(data) { }
	Packet(PacketType type, Serializeable& serializeable) : header({ .type = type, .length = 0 }) {
		serializeable.serialize(*this);
		header.length = data.size();
	}

	void set_offset(size_t offset) {
		Logging::get_logger()->debug("set_offset(" + std::to_string(offset) + ")");

		if (offset < 0) return;
		if (offset > data.size()) {
			Logging::get_logger()->warning("Offset surpassed the data size. Setting offset to data size....");
			this->offset = data.size();

			return;
		}

		this->offset = offset;
	}

	Packet* read(void* data, size_t read_length) {
		if (!can_read(read_length)) return this;

		std::memcpy(data, this->data.data() + this->offset, read_length);
		this->set_offset(this->offset + read_length);

		return this;
	}

	Packet* write(const void* data, size_t data_length) {
		const byte* src = static_cast<const byte*>(data);
		this->data.insert(this->data.end(), src, src + data_length);

		return this;
	}

	template<typename T>
	typename std::enable_if<std::is_pod<T>::value, Packet*>::type 
	read(T& value) {
		read(&value, sizeof(T));

		return this;
	}

	template<typename T>
	typename std::enable_if<std::is_pod<T>::value, Packet*>::type 
	write(const T data) {
		write(&data, sizeof(T));

		return this;
	}

	Packet* read(std::string& str) {
		uint32_t length;
		read(length);

		if (!length) return this;

		std::vector<char> str_buf(length);
		read(str_buf.data(), length);

		str = std::string(str_buf.data());
		
		return this;
	}

	Packet* write(const std::string str) {
		if (str.empty()) return this;
		uint32_t length = static_cast<uint32_t>(str.length());

		write(&length, sizeof(length));
		write(str.data(), str.length());

		return this;
	}

	Packet* rpad(uint32_t amount) {
		if (!can_read(amount)) return this;
		this->set_offset(this->offset + amount);
		
		return this;
	}

	Packet* wpad(uint32_t amount) {
		byte* padding = static_cast<byte*>(calloc(amount, sizeof(byte)));
		this->data.insert(this->data.end(), padding, padding + amount);

		free(static_cast<void*>(padding));
		return this;
	}

	void read(Serializeable& serializeable) {
		serializeable.deserialize(*this);
	}

	void write(Serializeable& serializeable) {
		serializeable.serialize(*this);
	}

	const PacketHeader get_packet_header() {
		return this->header;
	}

	const bytearray get_data() {
		return this->data;
	}

	const size_t get_data_size() {
		return this->data.size();
	}

	const bytearray to_sendable_data() {
		Logging::get_logger()->info("to_sendable_data() -> " + std::to_string(data.size()));
		header.length = data.size();

		bytearray full_data = bytearray(sizeof(header) + data.size());
		std::memcpy(full_data.data(), &header, sizeof(header));
		full_data.insert(full_data.begin() + sizeof(header), this->data.begin(), this->data.end());

		return full_data;
	}

	const size_t get_sendable_data_size() {
		return sizeof(PacketHeader) + this->data.size();
	}
};

