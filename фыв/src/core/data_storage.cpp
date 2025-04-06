#include "core/data_storage.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <random>
#include <iomanip>
#include <iostream>

namespace cloud {

DataStorage::DataStorage() : max_storage_size_(1024 * 1024 * 1024) { // 1GB по умолчанию
    metrics_.total_items = 0;
    metrics_.total_size = 0;
    metrics_.failed_operations = 0;
    metrics_.last_update = std::chrono::system_clock::now();
    
    storage_path_ = "data/storage";
    initializeStorage();
}

DataStorage::~DataStorage() {
    compactStorage();
}

std::string DataStorage::storeData(const std::string& data, const std::string& type,
                                 const std::unordered_map<std::string, std::string>& metadata) {
    if (!validateData(data)) {
        std::cerr << "DataStorage: Неверные данные" << std::endl;
        metrics_.failed_operations++;
        return "";
    }

    std::lock_guard<std::mutex> lock(storage_mutex_);

    // Проверяем доступное место
    if (metrics_.total_size + data.size() > max_storage_size_) {
        std::cerr << "DataStorage: Недостаточно места" << std::endl;
        metrics_.failed_operations++;
        return "";
    }

    DataItem item;
    item.id = generateItemId();
    item.data = data;
    item.type = type;
    item.created_at = std::chrono::system_clock::now();
    item.updated_at = item.created_at;
    item.metadata = metadata;

    data_items_[item.id] = item;
    metrics_.total_items++;
    metrics_.total_size += data.size();
    updateMetrics("store");

    persistData(item);
    return item.id;
}

bool DataStorage::retrieveData(const std::string& id, DataItem& item) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto it = data_items_.find(id);
    if (it == data_items_.end()) {
        metrics_.failed_operations++;
        return false;
    }

    item = it->second;
    updateMetrics("retrieve");
    return true;
}

bool DataStorage::updateData(const std::string& id, const std::string& data) {
    if (!validateData(data)) {
        std::cerr << "DataStorage: Неверные данные" << std::endl;
        metrics_.failed_operations++;
        return false;
    }

    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto it = data_items_.find(id);
    if (it == data_items_.end()) {
        metrics_.failed_operations++;
        return false;
    }

    // Обновляем размер в метриках
    metrics_.total_size -= it->second.data.size();
    metrics_.total_size += data.size();

    it->second.data = data;
    it->second.updated_at = std::chrono::system_clock::now();
    
    updateMetrics("update");
    persistData(it->second);
    return true;
}

bool DataStorage::deleteData(const std::string& id) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto it = data_items_.find(id);
    if (it == data_items_.end()) {
        metrics_.failed_operations++;
        return false;
    }

    metrics_.total_size -= it->second.data.size();
    metrics_.total_items--;
    data_items_.erase(it);
    
    updateMetrics("delete");
    return true;
}

std::vector<DataStorage::DataItem> DataStorage::findDataByType(const std::string& type) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    std::vector<DataItem> result;
    
    for (const auto& pair : data_items_) {
        if (pair.second.type == type) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<DataStorage::DataItem> DataStorage::findDataByMetadata(
    const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    std::vector<DataItem> result;
    
    for (const auto& pair : data_items_) {
        auto it = pair.second.metadata.find(key);
        if (it != pair.second.metadata.end() && it->second == value) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

std::vector<DataStorage::DataItem> DataStorage::findDataByTimeRange(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    std::vector<DataItem> result;
    
    for (const auto& pair : data_items_) {
        if (pair.second.created_at >= start && pair.second.created_at <= end) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool DataStorage::addMetadata(const std::string& id, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto it = data_items_.find(id);
    if (it == data_items_.end()) {
        metrics_.failed_operations++;
        return false;
    }

    it->second.metadata[key] = value;
    it->second.updated_at = std::chrono::system_clock::now();
    
    updateMetrics("add_metadata");
    persistData(it->second);
    return true;
}

bool DataStorage::removeMetadata(const std::string& id, const std::string& key) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto it = data_items_.find(id);
    if (it == data_items_.end()) {
        metrics_.failed_operations++;
        return false;
    }

    if (it->second.metadata.erase(key) > 0) {
        it->second.updated_at = std::chrono::system_clock::now();
        updateMetrics("remove_metadata");
        persistData(it->second);
        return true;
    }
    
    return false;
}

std::unordered_map<std::string, std::string> DataStorage::getMetadata(const std::string& id) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto it = data_items_.find(id);
    if (it == data_items_.end()) {
        return std::unordered_map<std::string, std::string>();
    }
    
    return it->second.metadata;
}

DataStorage::StorageMetrics DataStorage::getMetrics() const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    return metrics_;
}

size_t DataStorage::getTotalItems() const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    return metrics_.total_items;
}

size_t DataStorage::getTotalSize() const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    return metrics_.total_size;
}

double DataStorage::getStorageUtilization() const {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    return static_cast<double>(metrics_.total_size) / max_storage_size_;
}

void DataStorage::cleanupOldData(std::chrono::system_clock::duration max_age) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    auto now = std::chrono::system_clock::now();
    for (auto it = data_items_.begin(); it != data_items_.end();) {
        if (now - it->second.updated_at > max_age) {
            metrics_.total_size -= it->second.data.size();
            metrics_.total_items--;
            it = data_items_.erase(it);
        } else {
            ++it;
        }
    }
    
    updateMetrics("cleanup");
}

void DataStorage::compactStorage() {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    // Создаем временный файл
    std::string temp_path = storage_path_ + ".tmp";
    std::ofstream temp_file(temp_path, std::ios::binary);
    
    if (!temp_file.is_open()) {
        std::cerr << "DataStorage: Ошибка при создании временного файла" << std::endl;
        return;
    }
    
    // Записываем все данные во временный файл
    for (const auto& pair : data_items_) {
        persistData(pair.second);
    }
    
    temp_file.close();
    
    // Заменяем старый файл новым
    std::filesystem::remove(storage_path_);
    std::filesystem::rename(temp_path, storage_path_);
    
    updateMetrics("compact");
}

void DataStorage::backupData(const std::string& backup_path) {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    
    std::filesystem::copy_file(storage_path_, backup_path,
                              std::filesystem::copy_options::overwrite_existing);
    
    updateMetrics("backup");
}

std::string DataStorage::generateItemId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 16; i++) {
        ss << dis(gen);
    }
    
    return ss.str();
}

void DataStorage::updateMetrics(const std::string& operation) {
    metrics_.last_update = std::chrono::system_clock::now();
}

bool DataStorage::validateData(const std::string& data) const {
    return !data.empty() && data.size() <= max_storage_size_;
}

void DataStorage::persistData(const DataItem& item) {
    std::ofstream file(storage_path_, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        std::cerr << "DataStorage: Ошибка при открытии файла для записи" << std::endl;
        return;
    }
    
    // Записываем данные в бинарном формате
    size_t id_size = item.id.size();
    size_t data_size = item.data.size();
    size_t type_size = item.type.size();
    size_t metadata_size = item.metadata.size();
    
    file.write(reinterpret_cast<const char*>(&id_size), sizeof(id_size));
    file.write(item.id.c_str(), id_size);
    
    file.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    file.write(item.data.c_str(), data_size);
    
    file.write(reinterpret_cast<const char*>(&type_size), sizeof(type_size));
    file.write(item.type.c_str(), type_size);
    
    file.write(reinterpret_cast<const char*>(&metadata_size), sizeof(metadata_size));
    for (const auto& pair : item.metadata) {
        size_t key_size = pair.first.size();
        size_t value_size = pair.second.size();
        
        file.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
        file.write(pair.first.c_str(), key_size);
        
        file.write(reinterpret_cast<const char*>(&value_size), sizeof(value_size));
        file.write(pair.second.c_str(), value_size);
    }
    
    file.close();
}

void DataStorage::loadDataFromStorage() {
    std::ifstream file(storage_path_, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "DataStorage: Ошибка при открытии файла для чтения" << std::endl;
        return;
    }
    
    while (file.good()) {
        DataItem item;
        
        // Читаем ID
        size_t id_size;
        file.read(reinterpret_cast<char*>(&id_size), sizeof(id_size));
        if (!file.good()) break;
        
        item.id.resize(id_size);
        file.read(&item.id[0], id_size);
        
        // Читаем данные
        size_t data_size;
        file.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        if (!file.good()) break;
        
        item.data.resize(data_size);
        file.read(&item.data[0], data_size);
        
        // Читаем тип
        size_t type_size;
        file.read(reinterpret_cast<char*>(&type_size), sizeof(type_size));
        if (!file.good()) break;
        
        item.type.resize(type_size);
        file.read(&item.type[0], type_size);
        
        // Читаем метаданные
        size_t metadata_size;
        file.read(reinterpret_cast<char*>(&metadata_size), sizeof(metadata_size));
        if (!file.good()) break;
        
        for (size_t i = 0; i < metadata_size; ++i) {
            size_t key_size, value_size;
            file.read(reinterpret_cast<char*>(&key_size), sizeof(key_size));
            if (!file.good()) break;
            
            std::string key;
            key.resize(key_size);
            file.read(&key[0], key_size);
            
            file.read(reinterpret_cast<char*>(&value_size), sizeof(value_size));
            if (!file.good()) break;
            
            std::string value;
            value.resize(value_size);
            file.read(&value[0], value_size);
            
            item.metadata[key] = value;
        }
        
        data_items_[item.id] = item;
        metrics_.total_items++;
        metrics_.total_size += item.data.size();
    }
    
    file.close();
    updateMetrics("load");
}

void DataStorage::initializeStorage() {
    std::filesystem::create_directories(std::filesystem::path(storage_path_).parent_path());
    loadDataFromStorage();
}

} // namespace cloud 