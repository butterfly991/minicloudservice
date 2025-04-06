#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <functional>

namespace cloud {

class DataStorage {
public:
    struct DataItem {
        std::string id;
        std::string data;
        std::string type;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point updated_at;
        std::unordered_map<std::string, std::string> metadata;
    };

    struct StorageMetrics {
        size_t total_items;
        size_t total_size;
        size_t failed_operations;
        std::chrono::system_clock::time_point last_update;
    };

    DataStorage();
    ~DataStorage();

    // Основные операции с данными
    std::string storeData(const std::string& data, const std::string& type,
                         const std::unordered_map<std::string, std::string>& metadata = {});
    bool retrieveData(const std::string& id, DataItem& item);
    bool updateData(const std::string& id, const std::string& data);
    bool deleteData(const std::string& id);

    // Поиск и фильтрация
    std::vector<DataItem> findDataByType(const std::string& type);
    std::vector<DataItem> findDataByMetadata(const std::string& key, const std::string& value);
    std::vector<DataItem> findDataByTimeRange(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end);

    // Управление метаданными
    bool addMetadata(const std::string& id, const std::string& key, const std::string& value);
    bool removeMetadata(const std::string& id, const std::string& key);
    std::unordered_map<std::string, std::string> getMetadata(const std::string& id);

    // Метрики и мониторинг
    StorageMetrics getMetrics() const;
    size_t getTotalItems() const;
    size_t getTotalSize() const;
    double getStorageUtilization() const;

    // Очистка и обслуживание
    void cleanupOldData(std::chrono::system_clock::duration max_age);
    void compactStorage();
    void backupData(const std::string& backup_path);

private:
    std::unordered_map<std::string, DataItem> data_items_;
    mutable std::mutex storage_mutex_;
    StorageMetrics metrics_;
    size_t max_storage_size_;
    std::string storage_path_;

    std::string generateItemId() const;
    void updateMetrics(const std::string& operation);
    bool validateData(const std::string& data) const;
    void persistData(const DataItem& item);
    void loadDataFromStorage();
    void initializeStorage();
};

} // namespace cloud 