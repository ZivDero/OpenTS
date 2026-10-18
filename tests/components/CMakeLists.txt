# The harness drives component storage on bare game objects, so it compiles AbstractClass
# itself, the save stream it writes through and the checksum it folds into, and links the
# same flecs library the engine does. It supplies its own swizzle manager and scenario
# stand-ins in place of the rest of the game.

opents_add_test(ComponentTest
    NAME components
    SOURCES componenttest.cpp
    ENGINE
        abstract.cpp
        component.cpp
        crc.cpp
        savestream.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    LIBRARIES flecs
)
