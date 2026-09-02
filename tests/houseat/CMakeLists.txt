# The house-at helpers are compiled straight into the harness. They live in code/ but have no
# engine dependencies, so the harness compiles them alone. The entry point stays here so the
# recursive glob building OpenTS cannot pick it up.
add_executable(HouseAt
    "${CMAKE_CURRENT_SOURCE_DIR}/houseattest.cpp"
    "${CMAKE_SOURCE_DIR}/code/houseat.cpp"
)

target_compile_features(HouseAt PRIVATE cxx_std_20)

target_include_directories(HouseAt PRIVATE
    "${CMAKE_SOURCE_DIR}/code"
)

target_compile_definitions(HouseAt PRIVATE WIN32 _WINDOWS _MBCS NOMINMAX)

target_compile_options(HouseAt PRIVATE
    $<$<CONFIG:Debug>:/MTd /EHsc /Zc:__cplusplus>
    $<$<CONFIG:Release>:/MT /EHsc /Zc:__cplusplus>
)

target_link_libraries(HouseAt PRIVATE kernel32 user32 shell32)

set_target_properties(HouseAt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_test(NAME houseat COMMAND HouseAt)
