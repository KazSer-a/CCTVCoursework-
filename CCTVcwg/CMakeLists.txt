cmake_minimum_required(VERSION 3.19)
project(CCTVNSTU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(CCTVNSTU
    WIN32 MACOSX_BUNDLE

    src/main.cpp

    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.ui
    src/ui/PlanView.h
    src/ui/PlanView.cpp

    src/model/PlanObjects.h
    src/model/CameraTypes.h
    src/model/Project.h
    src/model/Project.cpp

    src/geometry/VisibilityCalculator.h
    src/geometry/VisibilityCalculator.cpp

    src/optimization/CandidateGenerator.cpp
)

target_link_libraries(CCTVNSTU PRIVATE Qt::Core Qt::Widgets)

include(CTest)

if(BUILD_TESTING)
    add_executable(ProjectTests
        tests/projecttests.cpp
        src/model/Project.cpp
        src/model/Project.h
        src/model/PlanObjects.h
        src/model/CameraTypes.h
        src/geometry/VisibilityCalculator.cpp
        src/geometry/VisibilityCalculator.h
    )

    target_include_directories(ProjectTests PRIVATE
        src/model
        src/geometry
    )

    set_target_properties(ProjectTests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )

    add_test(NAME ProjectTests COMMAND ProjectTests)

    add_executable(VisibilityTests
        tests/VisibilityTests.cpp
        src/geometry/VisibilityCalculator.cpp
        src/geometry/VisibilityCalculator.h
        src/model/Project.cpp
        src/model/Project.h
        src/model/PlanObjects.h
        src/model/CameraTypes.h
    )

    target_include_directories(VisibilityTests PRIVATE
        src/model
        src/geometry
    )

    set_target_properties(VisibilityTests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
    )

    add_test(NAME VisibilityTests COMMAND VisibilityTests)
endif()

install(TARGETS CCTVNSTU
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET CCTVNSTU
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(SCRIPT ${deploy_script})