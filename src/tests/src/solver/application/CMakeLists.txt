add_executable(test-application)
target_sources(test-application
        PRIVATE
        test-application.cpp
)
target_link_libraries(test-application
        PRIVATE
        Boost::unit_test_framework
        yuni-static-core
)

add_test(NAME test-application COMMAND test-application)

# Storing the executable under the folder Unit-tests in Visual Studio
set_target_properties(test-application PROPERTIES FOLDER Unit-tests)
set_property(TEST test-application PROPERTY LABELS unit)
