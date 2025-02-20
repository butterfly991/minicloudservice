#pragma once
#include "interfaces.h"

namespace cloud_iaas{
	struct Config {
		struct {
			std::vector<std::string> essential_plugins;
		} plugins;

		struct {
			bool dpdk_enabled;
			std::vector<std::string> interfaces;
		} network;

		struct {
			int port;
			std::string ssl_cert;
			std::string ssl_key;
		} api;

		static Config from_json(const json& j);
	};
}