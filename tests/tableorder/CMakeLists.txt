set(flecs_dir "${CMAKE_SOURCE_DIR}/thirdparty/flecs")

opents_add_test(TableOrderTest
    NAME tableorder
    SOURCES tableordertest.cpp flecsstorage.c
    INCLUDES "${flecs_dir}/include" "${flecs_dir}/src"
    DEFINITIONS NOMINMAX flecs_STATIC
    LIBRARIES flecs
    DOCTEST
)
