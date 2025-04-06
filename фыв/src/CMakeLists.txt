# Основные компоненты системы
add_subdirectory(core)
add_subdirectory(network)
add_subdirectory(storage)
add_subdirectory(security)
add_subdirectory(blockchain)
add_subdirectory(api)
add_subdirectory(monitoring)

# Создание основного исполняемого файла
add_executable(cloud-service
    main.cpp
)

# Линковка с основными компонентами
target_link_libraries(cloud-service
    PRIVATE
    core
    network
    storage
    security
    blockchain
    api
    monitoring
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    protobuf::libprotobuf
    gRPC::grpc++
    Redis::Redis
    Boost::system
    Boost::thread
    Boost::filesystem
) 