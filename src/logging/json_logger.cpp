#include "interfaces.h"
#include <fstream>

namespace cloud_iaas {
	class JsonLogger : public ILogger {
	public:
		JsonLogger( const std::string& path = "logs/cloud-iaas.log")
		: m_stream(path, std::ios::app)
		{
			if (!m_stream){ throw std::runtime_error("Failed to open log file")};			
		}

		void log(Level level, const std::string& message, const json& context) override { 
			const auto now = std::chrono::system_clock::now();
			json entry = {
				{"timestamp",std::chrono::duration_cast<std::chrono::milliseconds>
				(now.time_since_epoch()).count()},
				{"level",level_to_string(level)},
				{"message",message},
				{"context",context}
			};
			std::lock_guard<std::mutex> lock (m_mutex);
			m_stream<<entry.dump(4)<<"\n";
		}
	private:
		std::ofstream m_stream{"logs/system.log"};
		std::mutex m_mutex;

		static std::string level_to_string(Level lvl) {
			switch(lvl) {
			case Debug: return "Debug";
			case Info: return "INFO";
			case Warning: return "WARNING";
			case Error: return "ERROR";
			case Fatal: return "FATAL";
			default: return "UNKNOWN";
			}
		}
	};
}
