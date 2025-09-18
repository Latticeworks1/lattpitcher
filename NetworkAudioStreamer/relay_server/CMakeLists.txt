cmake_minimum_required(VERSION 3.16)
project(AudioRelayServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)

# Original UDP-only relay server
add_executable(relay_server RelayServer.cpp)

target_link_libraries(relay_server 
    Boost::system 
    nlohmann_json::nlohmann_json
    pthread
)

# Hybrid relay server with Puter API support
add_executable(hybrid_relay_server HybridRelayServer.cpp)

target_link_libraries(hybrid_relay_server 
    Boost::system 
    nlohmann_json::nlohmann_json
    ${CURL_LIBRARIES}
    pthread
)

target_include_directories(hybrid_relay_server PRIVATE ${CURL_INCLUDE_DIRS})

target_compile_definitions(relay_server PRIVATE
    BOOST_ASIO_HAS_STD_ADDRESSOF
    BOOST_ASIO_HAS_STD_ARRAY
    BOOST_ASIO_HAS_CSTDINT
    BOOST_ASIO_HAS_STD_SHARED_PTR
)