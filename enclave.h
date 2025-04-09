#pragma once

#include "architecture.h"
#include <array>
#include <memory>

namespace core {

    namespace security {
       
        class SecurityVault {
        public:
            virtual void calibrate_quantum_sensors() = 0;
            // Другие методы
        };

         class SecurityVault {
	    private:
		    struct Impl;
	    	std::unique_ptr<Impl> impl_;
	    public:
		    void store_key(const std::string& id,
		    	const std::array<uint8_t, 32>& key);
	    	std::array<uint8_t, 32>retrieve_key(const std::string& id);
	    };

    	class EnclaveSession {
    	private:
    		void* enclave_;
	    	uint64_t session_id_;
    	public:
	    	explicit EnclaveSession(const std::string& enclave_path);
		    void enter();
	    	void exit();
	    	void* get_secure_memory(size_t size);
	    };

    	class QuantumResistantCrypto {
    	public:
	    	static std::vector<uint8_t>kyber_encrypth(
		    	const std::array<uint8_t, 32>& public_key,
	    		const std::array<uint8_t>& data);
                
	    	static std::vector<uint8_t>kyber_decrypt(
		    	const std::array<uint8_t,32>& private_key,
		    	const std::vector<uint8_t>& ciphertext);
	    };

    }
}