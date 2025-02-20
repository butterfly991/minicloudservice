#pragma once
#include <stdexcept>
#include <string>

namespace cloud_iaas {

	class CoreInitException : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	class PluginLoadException : public std::runtime_error {
	public:
		
		PluginLoadException(const std::string& msg, int code = -1) 
		: std::runtime_error(msg), m_code(code) {}
		
		int code() const { return m_code; }
	private:
		int m_code;
	};

class ConfigValidationError : public std::runtime_error{
public:
		explicit ConfigValidationError(const std::string& errors)
		: std::runtime_error("Config validation failed: "+ errors) {}
	};
}