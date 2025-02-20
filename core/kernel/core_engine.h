#pragma once
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
#include <seastar/core/future.hh>
#include "hypervisor/hypervisor_manager.h"
#include "storage/storage_controller.h"
#include "api/rest_server.h"
#include "logging/logger.h"
#include "vault/secret_vault.h"
#include "metrics/reporter.h"
#include "config/validator.h"
#include "network/network_config.h"
#include "plugins/plugin_manager.h"
#include "error.h"

namespace asio = boost::asio;

Class CoreEngine{
public:
    
    CoreEngine(
        std::string config_path,
        std::shared_ptr<SecretVault> secret_vault,
        std::shared_ptr<MetricsReporter> metrics_reporter,
        std::shared_ptr<ConfigValidator> config_validator,
        std::shared_ptr<Logger> logger,
        std::shared_ptr<NetworkConfig> network_config = nullptr,
        std::shared_ptr<PluginManager> plugin_manager = nullptr
        );

    ~CoreEngine() noexcept;

    seastar::future<> start();
    seastar::future<> stop();

    template<typename Module,typename... Args>
    void register_module(Args&&... args);

private:

    asio::io_context m_io_ctx;
    std::string m_config_path;
    std::shared_ptr<SecretVault> m_secret_vault;
    std::shared_ptr<MetricsReporter> m_metrics_reporter;
    std::shared_ptr<ConfigValidator> m_config_validator;
    std::shared_ptr<Logger> m_logger;
    std::shared_ptr<NetworkConfig> m_network_config;
    std::shared_ptr<PluginManager> m_plugin_manager;
    std::unique_ptr<HypervisorManager> m_hypervisor;
    std::unique_ptr<StorageController> m_storage;
    std::unique_ptr<RestServer> m_rest_server;
    std::vector<std::unique_ptr<BaseModule>> m_modules;
    std::atomic_bool m_running{false};
    std::atomic<bool> m_shutdown_initiated{false};

    //инициализация аппаратных компанентов(m_hypervisor,m_storage)
     seastar::future<> init_hardware();
    //асинхронная загрузка конфигурации системы из внешних источников
    seastar::future<> load_and_config();
    //старт всех сервисов и модулей в движке
    seastar::future<> start_services();
    seastar::future<> stop_services();
    seastar::future<> handle_emergency_shutdown(); 
};
