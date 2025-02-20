#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include <chrono>

namespace cloud_iaas {

using json = nlohmann::json;

//базовые интерфейсы
class ILogger{
public:
    enum Level { Debug, Info, Warning, Error, Fatal};
   		 virtual void log (Level level, 
    	 const std::string& message,
    	 const json& context = {}) = 0;
	    virtual ~ILogger() = default;
};

class ISecretVault {
	public:
		virtual std::string get_key() = 0;
		virtual ~ISecretVault() = defalt;
};

class IMetricsReporter {
	public:
		virtual void report_counter (const std::string& name, 
																		int value = 1) = 0;
		virtual void report_duration (const std::string& name, 
											std::chrono::nanoseconds duration) = 0;
		virtual ~IMetricsReporter() = default ;
};

class IConfigValidator {
public:
	struct ValidatonResult
	{
		bool  is_valid;
		json errors;	
	};
	virtual ValidatonResult validate(const json& config) = 0;
	virtual ~IConfigValidator() = default;
};

//сетевой профиль
class NetworkProfile{
public:
	virtual void configure() = 0;
	virtual json status() const = 0;
	virtual std::string name() const = 0;
	virtual ~NetworkProfile() = default;
};

//менеджер плагинов
class IPluginManager {
public:
	virtual void load_plugins () =0;
	virtual void unload_all () =0;
	virtual ~IPluginManager () = default;
};
}

