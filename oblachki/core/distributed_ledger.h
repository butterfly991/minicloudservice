#pragma once

#include "enclave.h"
#include "advanced_synchronization.h"

#include <vector>
#include <array>
#include <memory>

namespace core {
    
    namespace blockchain {

        struct Block {
            uint64_t height;
		    std::array<uint8_t, 64> hash;
		    std::vector<uint8_t> transactions;
	    	uint64_t timestamp;
	    };

	    enum class ConsensusAlgorithm {
		    PBFT,
		    RAFT,
		    POET,
	    	CUSTOM_BFT
	    };

	    struct ConsensusPolicy {
		    ConsensusAlgorithm algorithm;
		    uint32_t fault_tolerance;
		    uint64_t block_time_ms;
		    bool enable_smart_contracts;
	    };

	    class DistributedLedger {
        private:
		    struct Impl;
		    std::unique_ptr<Impl> impl_;
	    public:
		    explicit DistributedLedger(const ConsensusPolicy& policy);
		    bool validate_block(const Block& block);
		    void commit_block(Block&& block);
		    std::vector<Block>get_chain(uint64_t from_height);
		    template<typename Validator>
		    void register_validator(Validator&& v);
            void initialize(uint32_t quorum_size);
            // Другие методы
	    };

	    class MerkleTree {
        private:
	    	std::vector<std::array<uint8_t, 32>> nodes_;
	    public:
	    	explicit MerkleTree(const std::vector<Block>& blocks);
	    	std::array<uint8_t, 32> root_hash() const;
	    };
    
    }

}