add_library(attitude_estimator STATIC)

target_sources(attitude_estimator PRIVATE src/attitude_ekf.cc)
target_include_directories(attitude_estimator
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attitude_estimator PUBLIC control_contracts)
