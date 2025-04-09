#pragma once
#include "architecture.h"

#include <memory>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

namespace core{
    
    namespace runtime {
    
        class JITOptimizationProfile {
        public:
            void optimize();
            // Другие методы
        };

        class JITCompiler {
	    private:
		    class ExecutionSession;
		    std::unique_ptr<ExecutionSession> session_;
		    llvm::DataLayout data_layout_;
	    public:
		    JITCompiler();
	    	~JITCompiler();

		    template<typename FuncPtr>
		    FuncPtr compile(const std::string& name, llvm::Module* module);
		
    		void optimize_module(llvm::Module& module, OptimizationLevel level);
	    	void add_IR_module(llvm::ThreadSafeModule module);
	    };

	    class ProfileGuidedOptimizer {
	    private:
		    std::unordered_map<std::string, uint64_t> function_counts_; 
	    public:
		    void recovery_execution(const std::string& function_name);
		    void apply_optimizations(JITCompiler& compiler);
    	};

    }

}