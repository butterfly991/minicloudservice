#include "core/blockchain_kernel.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <chrono>

namespace cloud {

BlockchainKernel::BlockchainKernel() : KernelBase("BlockchainKernel") {
    metrics_.last_update = std::chrono::system_clock::now();
    private_key_ = nullptr;
    public_key_ = nullptr;
}

BlockchainKernel::~BlockchainKernel() {
    cleanupResources();
}

bool BlockchainKernel::addTransaction(const Transaction& transaction) {
    if (!verifyTransaction(transaction)) {
        std::cerr << "BlockchainKernel: Недействительная транзакция" << std::endl;
        return false;
    }

    pending_transactions_.push_back(transaction);
    metrics_.transactions_processed++;
    return true;
}

bool BlockchainKernel::mineBlock() {
    if (pending_transactions_.empty()) {
        return false;
    }

    Block new_block;
    new_block.previous_hash = chain_.empty() ? "0" : chain_.back().hash;
    new_block.timestamp = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    
    // Добавляем транзакции в блок
    for (const auto& transaction : pending_transactions_) {
        std::stringstream ss;
        ss << transaction.sender << transaction.recipient 
           << transaction.data << transaction.timestamp;
        new_block.transactions.push_back(ss.str());
    }
    
    // Майнинг блока (поиск хэша с определенным количеством нулей в начале)
    new_block.nonce = 0;
    do {
        new_block.hash = calculateBlockHash(new_block);
        new_block.nonce++;
    } while (new_block.hash.substr(0, 4) != "0000");
    
    // Добавляем блок в цепочку
    chain_.push_back(new_block);
    pending_transactions_.clear();
    metrics_.blocks_mined++;
    
    return true;
}

bool BlockchainKernel::validateChain() const {
    for (size_t i = 1; i < chain_.size(); ++i) {
        const auto& current_block = chain_[i];
        const auto& previous_block = chain_[i - 1];
        
        // Проверяем хэш предыдущего блока
        if (current_block.previous_hash != previous_block.hash) {
            return false;
        }
        
        // Проверяем хэш текущего блока
        if (current_block.hash != calculateBlockHash(current_block)) {
            return false;
        }
    }
    
    return true;
}

std::string BlockchainKernel::getLatestBlockHash() const {
    return chain_.empty() ? "0" : chain_.back().hash;
}

bool BlockchainKernel::deployContract(const std::string& contract_code) {
    // Генерируем уникальный ID контракта
    std::string contract_id = calculateBlockHash(Block{
        getLatestBlockHash(),
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count()),
        {contract_code},
        "",
        0
    });
    
    contracts_[contract_id] = contract_code;
    metrics_.contracts_deployed++;
    return true;
}

bool BlockchainKernel::executeContract(const std::string& contract_id,
                                    const std::string& method,
                                    const std::vector<std::string>& params) {
    auto it = contracts_.find(contract_id);
    if (it == contracts_.end()) {
        std::cerr << "BlockchainKernel: Контракт не найден" << std::endl;
        return false;
    }
    
    // Здесь должна быть реализация выполнения смарт-контракта
    // В данном примере просто добавляем транзакцию
    Transaction transaction;
    transaction.sender = "system";
    transaction.recipient = contract_id;
    transaction.data = method + ":" + params[0]; // Упрощенная версия
    transaction.timestamp = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    transaction.signature = signData(transaction.data);
    
    return addTransaction(transaction);
}

void BlockchainKernel::sendMessage(const std::string& target_kernel, const std::string& message) {
    // Преобразуем сообщение в транзакцию
    Transaction transaction;
    transaction.sender = getName();
    transaction.recipient = target_kernel;
    transaction.data = message;
    transaction.timestamp = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    transaction.signature = signData(message);
    
    addTransaction(transaction);
}

double BlockchainKernel::getLoad() const {
    updateMetrics();
    
    // Вычисляем нагрузку на основе количества блоков, транзакций и контрактов
    double block_load = static_cast<double>(metrics_.blocks_mined) / 1000.0;
    double transaction_load = static_cast<double>(metrics_.transactions_processed) / 10000.0;
    double contract_load = static_cast<double>(metrics_.contracts_deployed) / 100.0;
    
    return std::min(1.0, block_load + transaction_load + contract_load);
}

std::vector<Block> BlockchainKernel::getChain() const {
    return chain_;
}

std::vector<Transaction> BlockchainKernel::getPendingTransactions() const {
    return pending_transactions_;
}

bool BlockchainKernel::isTransactionValid(const Transaction& transaction) const {
    return verifyTransaction(transaction);
}

void BlockchainKernel::initializeResources() {
    // Инициализация криптографических компонентов
    OpenSSL_add_all_algorithms();
    
    // Генерация ключевой пары
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
    EVP_PKEY_keygen(ctx, &private_key_);
    EVP_PKEY_CTX_free(ctx);
    
    // Получение публичного ключа
    public_key_ = EVP_PKEY_dup(private_key_);
}

void BlockchainKernel::cleanupResources() {
    // Очистка криптографических компонентов
    if (private_key_) {
        EVP_PKEY_free(private_key_);
        private_key_ = nullptr;
    }
    if (public_key_) {
        EVP_PKEY_free(public_key_);
        public_key_ = nullptr;
    }
    
    EVP_cleanup();
}

std::string BlockchainKernel::calculateBlockHash(const Block& block) const {
    std::stringstream ss;
    ss << block.previous_hash << block.timestamp;
    for (const auto& transaction : block.transactions) {
        ss << transaction;
    }
    ss << block.nonce;
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, ss.str().c_str(), ss.str().size());
    SHA256_Final(hash, &sha256);
    
    std::stringstream hex;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return hex.str();
}

bool BlockchainKernel::verifyTransaction(const Transaction& transaction) const {
    return verifySignature(transaction.data, transaction.signature);
}

void BlockchainKernel::processTasks() {
    while (!task_queue_.empty()) {
        const auto& task = task_queue_.front();
        
        if (task.type == "mine") {
            mineBlock();
        } else if (task.type == "deploy") {
            deployContract(task.data);
        }
        
        task_queue_.pop();
    }
}

void BlockchainKernel::updateMetrics() const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - metrics_.last_update).count();
    
    if (elapsed >= 1) {
        metrics_.last_update = now;
    }
}

std::string BlockchainKernel::signData(const std::string& data) const {
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, private_key_);
    EVP_DigestSignUpdate(mdctx, data.c_str(), data.size());
    
    size_t signature_len;
    EVP_DigestSignFinal(mdctx, nullptr, &signature_len);
    
    std::vector<unsigned char> signature(signature_len);
    EVP_DigestSignFinal(mdctx, signature.data(), &signature_len);
    EVP_MD_CTX_free(mdctx);
    
    std::stringstream hex;
    for (size_t i = 0; i < signature_len; i++) {
        hex << std::hex << std::setw(2) << std::setfill('0') 
            << static_cast<int>(signature[i]);
    }
    
    return hex.str();
}

bool BlockchainKernel::verifySignature(const std::string& data, const std::string& signature) const {
    std::vector<unsigned char> sig_bytes;
    for (size_t i = 0; i < signature.length(); i += 2) {
        std::string byte = signature.substr(i, 2);
        sig_bytes.push_back(static_cast<unsigned char>(std::stoi(byte, nullptr, 16)));
    }
    
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, public_key_);
    EVP_DigestVerifyUpdate(mdctx, data.c_str(), data.size());
    
    int result = EVP_DigestVerifyFinal(mdctx, sig_bytes.data(), sig_bytes.size());
    EVP_MD_CTX_free(mdctx);
    
    return result == 1;
}

} // namespace cloud 