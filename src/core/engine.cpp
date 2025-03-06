#include "core/engine.h"
#include "blockchain/fpga_miner.h"
#include "blockchain/gpu_miner.h"
#include "boost/asio/executor_work_guard.hpp"
#include "config/config_loader.h"
#include "network/asio_stack.h"
#include <csignal>
#include <memory>
#include <stdexcept>
#include <sys/sysinfo.h>
#include <thread>

namespace core {

  CoreEngine::CoreEngine(
    std::string config_path,
    std::shared_ptr<SecretVault> secret_vault,
    std::shared_ptr<MetricsReporter> metrics_reporter,
    std::shared_ptr<ConfigValidator> config_validator,
    std::shared_ptr<Logger> logger,
    std::shared_ptr<NetworkConfig> network_config,
    std::shared_ptr<PluginManager> plugin_manager
  ): 
    m_work_guard(asio::make_work_guard(m_io_ctx)),
    m_work_pool(std::thread::hardware_concurrency()),
    m_config_path(std::move(config_path)),
    m_secret_vault(std::move(secret_vault)),
    m_config_validator(std::move(config_validator)),
    m_logger(logger),
    m_network_config(network_config ? std::move(network_config) 
    : std::make_shared<NetworkConfig>()),
    m_plugin_manager(plugin_manager ? std::move(plugin_manager) 
    : std::make_shared<PluginManager>()),
    m_etcd_client("http://etcd:2379"),
    m_autoscaler(m_etcd_client, *metrics_reporter),
    m_task_scheduler(m_io_ctx, std::thread::hardware_concurrency()),
    m_pbft_engine(m_io_ctx, m_etcd_client),
    m_metrics_reporter(metrics_reporter){
      if (!m_secret_vault || !m_metrics_reporter || !logger){
        throw std::invalid_argument("invalid core component initialization");
      }
      try{
        setup_security_infrastructure();
        init_hardware();
        load_configuration();

    //   <<==========]   инициализация сетевого стека   [==========>>
    m_network_stack = std::make_unique<AsioNetworkStack>(m_io_ctx, *m_network_config);
    //   <<==========]   основные модули    [==========>>
    m_hypervisor = std::make_unique<HypervisorManager>(m_logger, m_metrics_reporter);
    m_storage = std::make_unique<StorageController>(m_logger, m_secret_vault);
    m_rest_server = std::make_unique<RestServer>(
      std::make_shared<AuthService>(m_secret_vault, m_logger,m_metrics_reporter),
      m_hypervisor.get(), 
      m_storage.get(),
      m_logger,
      m_network_config
    );

    //   <<==========]   блокчейн и консенсус   [==========>>
    initialize_blockchain();
    m_pbft_engine.initialize(m_secret_vault->get_private_key());

    //   <<==========]   межмодульное взаимодействие   [==========>>
    setup_inter_module_communication();

    m_logger->log(Logger::Level::Info, "CoreEngine initialized",{ 
      {"config_path", m_config_path},
      {"network_mode", m_network_config->dpdk_enabled ? "DPDK" : "Asio"},
      {"hardware_acceleration",{
      {"gpu", m_network_config->gpu_enabled},
      {"fpga", m_network_config->fpga_enabled}
    }}
  });
  }catch (const std::execution& ex){
    m_logger->log(Logger::Level::Critical, "CoreEngine initialization failed",{
      {"error", ex.what()}
    });
    throw;
  }
}
  CoreEngine::~CoreEngine() noexcept {
     if (m_shutdown_initiated.exchange(true))return;
    
     try{
      if (m_running.load()) {
        m_logger->log(Logger::Level::Warning,
        "CoreEngine destroyed without proper shutdown");
        emergency_stop();
      }
      //   <<==========]   Освобождение ресурсов    [==========>>
      m_work_pool.stop();
      m_work_pool.join();
      cleanup_resources();

      m_logger->log(Logger::Level::Info, "CoreEngine shutdown completed");
    }catch (const std::exception& ex) {
      std::cerr<<"Fatal error during destruction: "<<ex.what()<<std::endl;
      std::terminate();
    }
  }

  void CoreEngine::start(){
    if(m_running.exchange(true)){
      m_logger->log(Logger::Level::Warning, "Attempt to start already running engine");
      return;
    }

    try{
      //   <<==========]   Запуск сетевого стека   [==========>>
      start_network_stack();
      //   <<==========]   Инициализация консенсуса   [==========>>
      start_consensus_engine();
      //   <<==========]   планировщик задач   [==========>>
      m_task_scheduler.start();
      //   <<==========]   Фоновые процессы   [==========>>
      start_monitoring();
      schedule_maintenance();

      //   <<==========]   Запуск рабочих потоков    [==========>>
      for(size_t i=0,i<std::thread::hardware_concurrency(); ++i){
        asio::post(m_work_pool,[this] {
          m_io_ctx.run();
        });
      }

      m_metrics_reporter->log_event("SystemStart");
      m_logger->log(Logger::Level::Info, "CoreEngine started successfully");

    }catch(const std::exception& ex){
      handle_critical_failure();
      throw;
    }
  }
  void CoreEngine::emergency_stop() {
    if(!m_running.exchange(false)) return;

    m_logger->log(Logger::Level::Critical, "Initiating emergency stop");

    //   <<==========]   Немедленная остановка   [==========>>
    m_work_guard.reset();
    m_work_pool.stop();
    m_io_ctx.stop();

    //   <<==========]   Сохранение состояния   [==========>>
    save_recovery_state();
    cleanup_resources();

    m_metrics_reporter->log_event("EmergencyStop");
  }

  void CoreEngine::graceful_shutdown(){
    if (!m_running.exchange(false)) return;

    m_logger->log(Logger::Level::Info, "Starting graceful shutdown");

    //   <<==========]   Упорядоченная остановка   [==========>>
    m_task_scheduler.stop();
    m_pbft_engine.stop();
    m_rest_server->shutdown();
    m_autoscaler.disable();

    //   <<==========]   Ожидание завершения   [==========>>
    std::this_thread::sleep_for(std::chrono::seconds(2));
    m_work_pool.stop();
    m_work_pool.join();
    
    m_metrics_reporter->log_event("GracefulShutdown");

  }

  void CoreEngine::init_hardware(){
    if(m_network_config->gpu_enabled){
      init_gpu_context();
    }
    if(m_network_config->fpga_enabled){
      init_fpga_context(m_network_config->fpga_bitstream);
    }
    if(m_network_config->dpdk_enabled){
      m_smartnic_manager.initialize(*m_network_config);
    }
  }
  
  void CoreEngine::init_gpu_context(){
    CUDA_CHECK(cudaSetDevice(0));
    CUDA_CHECK(cudaStreamCreate(&m_cuda_context.stream));
    m_logger->log(Logger::Level::Info, "GPU context initialized");
  }

  void CoreEngine::init_fpga_context(const std::string& bitstream_path){
    m_fpga_accelerator.load_bitstream(bitstream_path);
    m_fpga_accelerator.initialize();
    m_logger->log(Logger::Level::Info, "FPGA context initialuzed", {
      {"bitstream",bitstream_path}
    });
  }

  void CoreEngine::load_configuration(){
    auto config = ConfigLoader::load(m_config_path);
    m_config_validator->validate(config);

    //   <<==========]   применение сетевых настроек   [==========>>
    m_network_config->apply(config.network_settings);

    //   <<==========]   загрузка модулей   [==========>>
    for(const auto& module_cfg : config.modules){
      m_plugin_manager->load_module(module_cfg);
    }
  }

  void CoreEngine::start_network_stack(){
    m_network_stack->start([this](auto packet){
      m_packet_queue.push(packet);
    });
    m_logger->log(Logger::Level::Info, "Networks stack started");
  }

  void CoreEngine::start_consensus_engine(){
    m_pbft_engine.start_consensus();
    m_logger->log(Logger::Level::Info, "Consensus engine started");
  }

  void CoreEngine::start_monitoring(){
    m_resource_monitor.initialize();
    m_resource_monitor.start_watching();
    m_logger->log(Logger::Level::Info, "Monitoring system activated");
  }

  void CoreEngine::schedule_maintenance(){
    asio::post(m_io_ctx,[this](){
      while(m_running.load()){
        check_system_health();
        scale_resources();
        verify_blockchain_integrity();
        std::this_thread::sleep_for(std::chrono::minutes(1));
      }
    });
  }

  void CoreEngine::cleanup_resources(){
    m_hotswap_manager.unload_all();
    m_plugin_manager->unload_all_plugins();

    if(m_network_config->dpdk_enabled){
      m_smartnic_manager.shutdown();
    }
    m_fpga_accelerator.release();
    if(m_network_config->gpu_enabled){
      CUDA_CHECK(cudaStreamDestroy(m_cuda_context.stream));
    }
  }1

  void CoreEngine::check_system_health(){
    try{
      auto metrics = m_resource_monitor.get_current_metrics();

      //   <<==========]   контроль использования CPU   [==========>>
      if(metrics.cpu_usage > 90.0){
        m_logger->log(Logger::Level::Warning, "High CPU usage", metrics);
        m_autoscaler.scale_up();
      }

      //   <<==========]   контроль памяти   [==========>>
      if(metrics.memory_usage > 85.0){
        m_logger->log(Logger::Level::Warning, "High memory usage", metrics);
      }

      //   <<==========]   контроль температуры GPU   [==========>>
      if(metrics.gpu_temperature > 85.0){
        m_logger->log(Logger::Level::Error, "GPU overheating", metrics);
        enable_gpu_acceleration(false);
      }

      //   <<==========]   контроль состояния блокчейна   [==========>>
      if(!m_pbft_engine.is_healthy()){
        m_logger->log(Logger::Level::Critical, "Consensus health check failed");
        handle_critical_failure();
      }

    }catch(const std::exception& ex){
      m_logger->log(Logger::Level::Error,"Health check failed",{{"error",e.what()}});
    }  
  }

  void CoreEngine::handle_critical_failure(){
    m_logger->log(Logger::Level::Critical, "Critical failure detected");

    //   <<==========]   Сохранение состояния для последующего анализа    [==========>>
    save_recovery_state();

    //   <<==========]   Уведомление кластера черезе etcd    [==========>>
    m_etcd_client.report_failure();

    //   <<==========]   аварийное отключение аппаратных компонентов   [==========>>
    m_fpga_accelerator.emergency_shutdown();
    m_smartnic_manager.emergency_stop();

    throw std::runtime_error("Critical system failure");  
  }

  void CoreEngine::save_recovery_state(){
    try{
      BlockchainState state = m_pbft_engine.get_current_state();
      m_snapshot_manager.save_state(state);
      m_logger->log(Logger::Level::Info, "Recovery state saved");
    }catch(const std::execution& ex){
      m_logger->log(Logger::Level::Error, "Failed to save recovery state", {{"error", ex.what()}});
    }
  }

  void CoreEngine::scale_resources(){
    auto metrics = m_metrics_reporter->get_cluster_metrics();

    //   <<==========]   Вертикальное масштабирование   [==========>>
    if(metrics.node_utilization > 80.0){
      m_autoscaler.scale_up();
    } 

    //   <<==========]   Горизонтальное масштабирование   [==========>>
    if(metrics.cluster_load > 75.0){
      m_autoscaler.add_node();
    }
  }

  void CoreEngine::setup_security_infrastructure(){
    //   <<==========]   Инициализация TLS контекста   [==========>>
    m_network_config->init_tls_context(
      m_secret_vault->get_certificate(),
      m_secret_vault->get_private_key()
    );

    //   <<==========]   Настройка брандмауэра   [==========>>
    m_network_stack->configure_firewall(m_config_validator->get_firewall_rules());

    m_logger->log(Logger::Level::Info, "Security infrastructure initialized");
  }

  void CoreEngine::validate_dependencies() const{
    //   <<==========]   Проверка версий библеотек   [==========>>
    if (!check_openssl_version()){
      throw std::runtime_error("OpenSSL version mismatch");
    }
    //   <<==========]   проверка доступности GPU   [==========>>
    if(m_network_config->gpu_enabled && !cuda_available()){
      throw std::runtime_error("CUDA not available");
    }
  }

  void CoreEngine::load_essential_plugins(){
    try{
      m_plugin_manager->load_essential_plugins({
        "/usr/lib/cloud-iaas/plugins/libsecurity.so",
        "/usr/lib/cloud-iaas/plugins/libmonitoring.so"
      });
      m_logger->log(Logger::Level::Info, "Essential plugins loaded");
    }catch(const PluginLoadException& ex){
      m_logger->log(Logger::Level::Critical, "Failed to load core plugins",{
        {"error",ex.what()},
        {"code",ex.code()}
      });
      throw;
    }
  }

  void CoreEngine::initialize_blockchain(){
    //   <<==========]   Инициализация майнеров   [==========>>
    if(m_network_config->gpu_enabled){
      m_pbft_engine.register_miner(std::make_unique<GPUMiner>(m_cuda_context));
    }
    if(m_network_config->fpga_enabled){
      m_pbft_engine.register_miner(std::make_unique<FPGAMiner>(m_fpga_accelerator));
    }

    //   <<==========]   инициализация смарт-контрактов   [==========>> 
    m_contract_vm.initialize(m_storage.get());

    m_logger->log(Logger::Level::Info, "Blockchain subsystem initialized");
  }

  void CoreEngine::setup_inter_module_communication(){
    //   <<==========]   Связь сетевого стека с блокчейном    [==========>>
    m_network_stack->set_packet_handler([this](auto metrics){
      m_autoscaler.update_metrics(metrics);
    });

    //   <<==========]   связь мониторинга с автопроксированием   [==========>>
    m_resource_monitor.set_metrics_handler([this](auto metrics){
      m_autoscaler.update_metrics(metrics);
    });
  }

  void CoreEngine::process_packets(){
    while(m_running.load()){
      NetworkPacket* packet;
      if(m_packet_queue.pop(packet)){
        handle_network_packet(packet);
        delete packet;
      }
    }
  }

  void CoreEngine::update_service_registry(){
    NodeInfo{
      .address = m_network_config->public_ip,
      .status = m_running.load() ? "active" : "inactive"
    };
    m_etcd_client.register_service("core-node",info);
  }

  void CoreEngine::configure_hardware_offloading(){
    if(m_network_config->dpdk_enabled){
      m_smartnic_manager.configure_offloading(
        SmartNIC::OffloadFlags::CHECKSUM |
        SmartNIC::OffloadFlags::CRYPTO
      );
    }
  }

  void CoreEngine::handle_network_packet(*NetworkPacket* packet){
    try{
      //   <<==========]   обработка транзакций блокчейна   [==========>>
      if(packet-type == PacketType::BLOCKCHAIN){
        m_pbft_engine.process_packets(*packet);
      }
      //  <<==========]   Обработка Rest Api запросов   [==========>>
      else if(packet->type == PacketType::REST_API){
        m_rest_server->handle_request(packet->data);
      }
    }catch(const std:;exception& ex){
      m_logger->log(Logger::Level::Error,"Packet handling failed",{
        {"type",static_cast<int>(packet->type)},
        {"error",ex.what()}
      });
    }
  }

  void CoreEngine::deploy_smart_contract(const std::string& contract_code){
    ContractID id = m_contract_vm.deploy(contract_code);
    m_pbft_engine.broadcast_contract(id);
    m_logger->log(Logger::Level::INfo,"Smart contract deployed", {{"contract_id",id}});
  }

  void CoreEngine::execute_contract(const std::string& contract_id){
    ContractResult result = m_contract_vm.execute(contract_id);
    if(!result.success){
      throw std::runtime_error("Contract execution failed: " + result.error);
    }
  }

  void CoreEngine::connect_to_kubernetes(const std::string& kubeconfig_path){
    m_k8s_manager.initialize(kubeconfig_path);
    m_autoscaler.set_kubernetes_manager(&m_k8s_manager);
    m_logger->log(Logger::Level::Info, "Kubernetes integration activated");

  }
} // namespace core
