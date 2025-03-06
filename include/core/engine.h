#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/uses_executor.hpp>
#include "api/rest_server.h"
#include "blockchain/pdft_engine.h"
#include "cluster/etcd_client.h"
#include "config/validator.h"
#include "fpga/accelerator.h"
#include "hotswap/module_manager.h"
#include "hypervisor/hypervisor_manager.h"
#include "logging/logger.h"
#include "metrics/reporter.h"
#include "monitoring/resource_monitor.h"
#include "network/network_config.h"
#include "plugins/plugin_manager.h"
#include "scheduler/task_scheduler.h"
#include "smartnic/manager"
#include "storage/storage_controller.h"
#include "vault/secret_vault.h"


namespace asio = boost::asio;

namespace core {
  class CoreEngine {
    public:
      explicit CoreEngine(
        std::string config_path,
        std::shared_ptr<SecretVault> secret_vault,
        std::shared_ptr<MetricsReporter> metrics_reporter,
        std::shared_ptr<ConfigValidator> config_validator,
        std::shared_ptr<Logger> logger,
        std::shared_ptr<NetworkConfig> network_config = nullptr,
        std::shared_ptr<PluginManager> plugin_manager = nullptr
      );

      ~CoreEngine() noexcept;

  //   <<==========] управлениe жизненным циклом [==========>>
  void start();
  void emergency_stop();
  void graceful_shutdown();

  //   <<==========] регистрация пользовательских модулей [==========>>
  template <typename Module, typename... Args>
  void register_module(Args &&...args);

  //   <<==========] управление аппаратным ускорением [==========>>
  void enable_gpu_acceleration(bool enable);
  void enable_fpga_acceleration(const std::string& bitstream_path);

  //   <<==========] управление смарт-контрактами [==========>>
  void deploy_smart_contract(const std::string& contract_code);
  void execute_contract(const std::string& contract_id);

  //   <<==========] Kubernetes [==========>>
  void connect_to_kubernetes(const std::string& kubeconfig_path);
  
  private:

  //   <<==========] ядро asio [==========>>
  asio::io_context m_io_ctx;
  asio::executor_work_guard<boost::asio::io_context::executor_type> m_work_guard;
  asio::thread_pool m_work_pool;

  //   <<==========] рабочее состояние системы [==========>>
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_shutdown_initiated{false};

  //  <<==========] конфигурация и безопасность [==========>>
  std::string m_config_path;
  std::shared_ptr<SecretVault> m_secret_vault;
  std::shared_ptr<ConfigValidator> m_config_validator;
  std::shared_ptr<Logger> m_logger;

  //  <<==========] cетевой стек [==========>>
  std::shared_ptr<NetworkConfig> m_network_config;
  std::unique_ptr<AsioNetworkStack> m_network_stack;

  //  <<==========] модули инфраструктуры [==========>>
  std::unique_ptr<HypervisorManager> m_hypervisor;
  std::unique_ptr<StorageController> m_storage;
  std::unique_ptr<RestServer> m_rest_server;

  //  <<==========] аппаратное ускорение [==========>>
  FPGA::Accelerator m_fpga_accelerator;
  SmartNIC::Manager m_smartnic_manager;
  CUDAContext m_cuda_context;

  //  <<==========] кластерное управление [==========>>
  cluster::EtcdClient m_etcd_client;
  cluster::Autoscaler m_autoscaler;
  KubernetesManager m_k8s_manager;

  //  <<==========] мониторинг и восстановление [==========>>
  std::shared_ptr<MetricsReporter> m_metrics_reporter;
  monitoring::ResourceMonitor m_resource_monitor;
  recovery::SnapshotManager m_snapshot_manager;

  //  <<==========] динамические модули [==========>>
  std::shared_ptr<PluginManager> m_plugin_manager;
  HotSWap::ModuleManager m_hotswap_manager;
  std::vector<std::unique_ptr<BaseModule>> m_modules;

  //  <<==========] блокчейн [==========>>
  blockchain::PBFTEngine m_pdft_engine;
  SmartContractVM m_contract_vm;

  //  <<==========] управление задачами [==========>>
  scheduler::TaskScheduler m_task_scheduler;

  //  <<==========] lock-free структуры [==========>>
  boost::lockfree::queue<NetworkPacket*> m_packet_queue{1024};

  //  <<==========] приватные методы  [==========>>
  void init_hardware();
  void load_configuration();
  void start_network_stack();
  void start_consensus_engine();
  void start_monitoring();
  void check_system_health();
  void handle_critical_failure();
  void save_recovery_state();
  void scale_resources();
  void init_gpu_context();
  void init_fpga_context(const std::string& bitstream_path);
  void configure_hardware_offloading();
  void process_packets();
  void schedule_maintenance();
  void update_service_registry();
  void validate_dependencies() const;
  void setup_security_infrastructure();
  void load_essential_plugins();
  void initialize_blockchain();
  void setup_inter_module_communication();
  void verify_blockchain_integrity();
  void handle_network_packet(NetworkPacket* packet);
  void cleanup_resources();

  };
} // namespace core
