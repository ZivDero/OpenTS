# The trigger action and payload enumerations are plain headers with no engine dependencies,
# so the harness compiles them alone. The entry point stays here so the recursive glob
# building OpenTS cannot pick it up.
add_executable(ActionIds
    "${CMAKE_CURRENT_SOURCE_DIR}/actionidstest.cpp"
)

target_compile_features(ActionIds PRIVATE cxx_std_20)

target_include_directories(ActionIds PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

target_compile_definitions(ActionIds PRIVATE WIN32 _WINDOWS _MBCS NOMINMAX)

target_compile_options(ActionIds PRIVATE
    $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
    $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
)

target_link_libraries(ActionIds PRIVATE kernel32 user32 shell32)

set_target_properties(ActionIds PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME actionids COMMAND ActionIds)
