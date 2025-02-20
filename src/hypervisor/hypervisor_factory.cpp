#pragma once
#include "interfaces.h"
#include "hypervisor_manager.h"
namespace cloud_iaas{
	class HypervisorFactory {
	public:
		static std::unique_ptr<IHypervisor> create(
			std::shared_ptr<ILogger> logger,
			std::shared_ptr<IMetricsReporter> metrics)
		{
			return std::make_unique<HypervisorManager>(logger,metrics);
		}
	};	


class StorageFactory {
public:
		static std::unique_ptr<IStorage> create(
			std::shared_ptr<ILogger> logger,
			std::shared_ptr<ISecretVault> vault){
			return std::make_unique<CephStorage>(logger,vault);
		}
	};
}




















