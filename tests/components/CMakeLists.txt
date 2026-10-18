# The harness drives the component storage through the entity slot an object keeps, so it
# needs no game object. It compiles the save stream it writes through and the checksum it
# folds into, and links the same flecs library the engine does.

opents_add_test(ComponentTest
    NAME components
    SOURCES componenttest.cpp
    ENGINE
        component.cpp
        crc.cpp
        savestream.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    LIBRARIES flecs
)
