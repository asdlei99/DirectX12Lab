﻿CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(09-PARTICLES)

SET(TargetName 09_Particles)

ADD_EXECUTABLE(${TargetName} "main.cpp" "particles.h" "particles.cpp")

SET_PROPERTY(TARGET ${TargetName} PROPERTY CXX_STANDARD 17)
SET_PROPERTY(TARGET ${TargetName} PROPERTY CXX_STANDARD_REQUIRED ON)

TARGET_LINK_LIBRARIES(${TargetName} PUBLIC D3D12Lab)

SET_PROPERTY(TARGET ${TargetName}
    PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/../../")
