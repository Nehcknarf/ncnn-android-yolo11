project(yolo11ncnn)

cmake_minimum_required(VERSION 3.10)

set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/opencv-mobile-4.11.0-android/sdk/native/jni)
find_package(OpenCV REQUIRED core imgproc)

set(ncnn_DIR ${CMAKE_SOURCE_DIR}/ncnn-20250503-android-vulkan/${ANDROID_ABI}/lib/cmake/ncnn)
find_package(ncnn REQUIRED)

set(apriltag_DIR ${CMAKE_SOURCE_DIR}/apriltag/${ANDROID_ABI}/lib/apriltag/cmake)
find_package(apriltag REQUIRED)

add_library(yolo11ncnn SHARED yolo11ncnn.cpp yolo11.cpp yolo11_det.cpp yolo11_seg.cpp yolo11_pose.cpp yolo11_cls.cpp yolo11_obb.cpp ndkcamera.cpp)

target_link_libraries(yolo11ncnn ncnn ${OpenCV_LIBS} camera2ndk mediandk apriltag)
