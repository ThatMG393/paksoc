#pragma once

#include <memory>
#include <string>

struct BaseLogger {
	virtual void info(std::string) = 0;
	virtual void debug(std::string) = 0;
	virtual void warning(std::string) = 0;
	virtual void error(std::string) = 0;

	BaseLogger() = default;
	virtual ~BaseLogger() = default;
};

class IOLogger : public BaseLogger {
public:
	void info(std::string message) override {
		print("I", message);
	}

	void debug(std::string message) override {
#ifndef NDEBUG
		print("D", message);
#endif
	}

	void warning(std::string message) override {
		print("W", message);
	}

	void error(std::string message) override {
		print("E", message);
	}

private:
	void print(std::string level, std::string& message) {
		printf("[%s] : %s\n", level.c_str(), message.c_str());
	}
};

namespace Logging {
	inline std::shared_ptr<BaseLogger> logger = nullptr;

	template<typename LoggerType>
	inline void set_logger() {
		logger = std::make_shared<LoggerType>(LoggerType());
	}

	inline std::shared_ptr<BaseLogger> get_logger() {
		if (!logger) set_logger<IOLogger>();
		return logger;
	}
}
