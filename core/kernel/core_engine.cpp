#include "core_engine.h"
#include "network/dpdk_stack.h"
#include "config/config_loader.h"
#include <stdexcept>
#include <seastar/core/sleep.hh>
#include "api/rest_server.h"
#include "plugins/plugin_loader.h"
namespace cloud_iaas {
CoreEngine::CoreEngine(
    std::string config_path,
    std::shared_ptr<SecretVault> secret_vault,
    std::shared_ptr<MetricsReporter> metrics_reporter,
    std::shared_ptr<ConfigValidator> config_validator,
    std::shared_ptr<Logger> logger
    std::shared_ptr<NetworkConfig> network_config,
    std::shared_ptr<PluginManager> plugin_manager 
) :
    m_config_path(std::move(config_path)),       // Путь к конфигурации
    m_secret_vault(std::move(secret_vault)),           // Сервис для работы с секретами
    m_metrics_reporter(std::move(metrics)),     // Сервис для сбора метрик
    m_validator(std::move(validator)),          // Валидатор конфигурации
    m_logger(std::move(logger)),                // Логгер
    m_hypervisor(std::make_unique<HypervisorManager>(m_logger)), //Менеджер гипервизора
    m_storage(std::make_unique<StorageController>(m_logger)),    // Контроллер хранилища
    m_rest_server(std::make_unique<RestServer>(
        std::make_shared<AuthService>(m_secret_vault,m_logger), // Сервис аутентификации
        m_hypervisor.get(),                                      // Менеджер гипервизора
        m_logger                                                 // Логгер
        )),
        m_running(false)                                            // Флаг состояния
    {
        // Логирование успешного создания
        m_logger->log(Logger::Level::Info, "CoreEngine initialized", {
            {"config_path", m_config_path},
            {"components_initialized","hypervisor, storage, rest_server"}
        });
    }
}
CoreEngine::~CoreEngine(){
    if(m_running){
        stop().get();
    }
}

seastar::future<Config> CoreEngine::load_and_validate_config(){
    return seastar::async([this] () {
        try 
        {
            ConfigLoader loader;
            auto config =  co_await loader.load_encrypted(
                m_config_path,
                m_secret_vault->get_key()
                );
           
           //Проверка конфигурации
            auto validation_result = m_validator->validate(config);
            if (!validation_result.is_valid){
                throw ConfigValidationError(validation_result.errors);
            }
         
            return config;

        } catch (const std::exception& ex) {
            m_logger->log(Logger:::Level::Error,"Configuration check failed", {
                {"error",ex.what()},
                {"config_path", m_config_path}
            });
            throw;
        }
    });    
}

seastar::future<> CoreEngine::start() {
    return seastar::async([this] () {
        try {
            /* code */
        }
        catch(const std::exception& ex) {
            std::cerr << e.what() << '\n';
        }
        co_await handle_emergency_shutdown();
        throw;


    }

        )






}
