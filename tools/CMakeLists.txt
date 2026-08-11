add_executable(btaudio-debug
    btaudio-debug.cpp
)

target_link_libraries(btaudio-debug PRIVATE
    multibtaudiocore
    Qt6::Core
)
