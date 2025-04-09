#pragma once

#include "architecture.h"

#include <boost/asio.hpp>
#include <memory>

namespace core {

    namespace network {

        struct PacketBuffer {
		    void* data;
		    size_t size;
		    uint16_t protocol;
		    uint32_t source_ip;
	    };

    	class AdvancedPacketProcessor {
	    private:
		    struct Impl;
		    std::unique_ptr<Impl> impl_;
    	public:
	    	explicit AdvancedPacketProcessor(size_t ring_size);
		    void process_packets();
		    void register_handler(uint16_t protocol,
			std::is_function<void(PacketBuffer&&)>handler);
		    void enable_rdma(bool enable);
		    void configure_dpdk(const std::string& interface);
    	};

	    struct QuicConfig {
		    uint16_t max_streams;
		    uint32_t connection_timeout_ms;
		    bool enable_0rtt;
    	};

	    class QuantumSafeTunell {
	    public:
		    void establish(const std::stringbuf& endpoint);
		    void send_encrypted(std::vector<uint8_t> data);
	    };

    }

}
