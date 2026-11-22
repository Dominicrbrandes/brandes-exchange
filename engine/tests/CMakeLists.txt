add_executable(exchange_tests
    test_main.cpp
    test_order_book.cpp
    test_matching.cpp
    test_stp.cpp
    test_event_format.cpp
    test_risk.cpp
    test_replay.cpp
    test_fuzz.cpp
    test_book_queue.cpp
    test_time_in_force.cpp
    test_replace.cpp
    test_model.cpp
    test_protocol.cpp
    test_recovery.cpp
    test_account.cpp
)

target_link_libraries(exchange_tests PRIVATE
    exchange_core
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(exchange_tests)