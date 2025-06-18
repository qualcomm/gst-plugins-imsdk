# Common CXX compiler flags.
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

add_subdirectory(gst-ml-qnn-app)
add_subdirectory(gst-ml-snpe-app)
add_subdirectory(gst-ml-tflite-app)
