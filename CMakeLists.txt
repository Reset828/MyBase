cmake_minimum_required(VERSION 3.25)

project(MyBase VERSION 2.0.0 LANGUAGES C CXX)

option(MYBASE_BUILD_TESTS "Build isolated core tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
add_compile_options(/FS)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Sql Concurrent Gui Widgets WebEngineWidgets PrintSupport Network WebSockets)

include(FetchContent)
if(WIN32)
    set(_mybase_cargo_home "$ENV{CARGO_HOME}")
    if(NOT _mybase_cargo_home AND DEFINED ENV{USERPROFILE})
        set(_mybase_cargo_home "$ENV{USERPROFILE}/.cargo")
    endif()

    # Automerge's C wrapper invokes Cargo during the native build. Make the
    # rustup default installation discoverable even when CMake was launched
    # from an IDE that inherited a stale PATH.
    if(_mybase_cargo_home)
        set(ENV{CARGO_HOME} "${_mybase_cargo_home}")
        if(EXISTS "${_mybase_cargo_home}/bin")
            set(ENV{PATH} "${_mybase_cargo_home}/bin;$ENV{PATH}")
        endif()
    endif()

    find_program(CARGO_CMD NAMES cargo cargo.exe
        HINTS "${_mybase_cargo_home}/bin"
        DOC "Cargo executable required by Automerge C")
    find_program(RUSTC_CMD NAMES rustc rustc.exe
        HINTS "${_mybase_cargo_home}/bin"
        DOC "Rust compiler required by Automerge C")
    if(NOT CARGO_CMD OR NOT RUSTC_CMD)
        message(FATAL_ERROR
            "Automerge requires the Rust toolchain. Install rustup with the nightly toolchain and rust-src component, "
            "then reconfigure. On Windows, the default Cargo location is %USERPROFILE%\\.cargo\\bin.")
    endif()
endif()
set(BUILD_TESTING OFF CACHE BOOL "Disable Automerge C upstream tests" FORCE)
set(UTF32_INDEXING ON CACHE BOOL "Use Unicode scalar indexing in Automerge text" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Automerge C wrapper as static" FORCE)
FetchContent_Declare(
    automerge_c
    GIT_REPOSITORY https://github.com/automerge/automerge.git
    GIT_TAG 5bae7757ef7b92c8cd6850def388f22767305ccb
    SOURCE_SUBDIR rust/automerge-c
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(automerge_c)
if(WIN32)
    # Rust std uses WaitOnAddress/WakeByAddress APIs on Windows.
    target_link_libraries(automerge INTERFACE synchronization)
endif()
if(NOT WIN32)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
endif()

set(SOURCES
    src/main.cpp
    src/applicationcontext.cpp
    src/databasemanager.cpp
    src/sqlitenoterepository.cpp
    src/sqlitetagrepository.cpp
    src/sqlitenotetagrepository.cpp
    src/sqlitesearchrepository.cpp
    src/databaseschema.cpp
    src/note.cpp
    src/tag.cpp
    src/tagmanager.cpp
    src/tagtreemodel.cpp
    src/markdownoutlinemodel.cpp
    src/searchengine.cpp
    src/markdownparser.cpp
    src/markdownhtmlrenderer.cpp
    src/contentdelivery.cpp
    src/localfeatures.cpp
    src/sessionstate.cpp
    src/aicredentials.cpp
    src/aiwritingservice.cpp
    src/notemanager.cpp
    src/notelistmodel.cpp
    src/passwordkdf.cpp
    src/vaultmanager.cpp
    src/vaultcredentialstore.cpp
    src/databaseerrorhandler.cpp
    src/markdownhighlighter.cpp
    src/noteeditor.cpp
    src/mainwindow.cpp
    src/notetabmanager.cpp
    src/searchresultmodel.cpp
    src/tagtreeview.cpp
    src/tagselectiondialog.cpp
    src/trashdialog.cpp
    src/aescrypto.cpp
    src/passworddialog.cpp
    src/encryptednotesdialog.cpp
    src/encryptednoteviewdialog.cpp
    src/collaboration/automerge_document.cpp
    src/collaboration/collaboration_client.cpp
)

set(HEADERS
    src/applicationcontext.h
    src/databasemanager.h
    src/databaseschema.h
    src/databasesqlhelpers.h
    src/note.h
    src/tag.h
    src/tagmanager.h
    src/tagtreemodel.h
    src/markdownoutlinemodel.h
    src/searchresult.h
    src/searchengine.h
    src/sqliterepositories.h
    src/markdownparser.h
    src/markdownhtmlrenderer.h
    src/contentdelivery.h
    src/localfeatures.h
    src/sessionstate.h
    src/aicredentials.h
    src/aiwritingservice.h
    src/markdownblock.h
    src/notemanager.h
    src/notelistmodel.h
    src/vaultmanager.h
    src/vaultcredentialstore.h
    src/repositories.h
    src/passwordkdf.h
    src/databaseerrorhandler.h
    src/markdownhighlighter.h
    src/noteeditor.h
    src/mainwindow.h
    src/notetabmanager.h
    src/searchresultmodel.h
    src/tagtreeview.h
    src/tagselectiondialog.h
    src/trashdialog.h
    src/themestylesheet.h
    src/aescrypto.h
    src/passworddialog.h
    src/encryptednotesdialog.h
    src/encryptednoteviewdialog.h
    src/collaboration/automerge_document.h
    src/collaboration/collaboration_client.h
)

add_executable(MyBaseCollaborationServer
    src/collaboration/server_main.cpp
    src/collaboration/collaboration_server.cpp
    src/collaboration/collaboration_server.h
    src/collaboration/automerge_document.cpp
    src/collaboration/automerge_document.h
)
target_include_directories(MyBaseCollaborationServer PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(MyBaseCollaborationServer PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::WebSockets automerge)
if(WIN32)
    target_link_libraries(MyBaseCollaborationServer PRIVATE bcrypt advapi32)
endif()

add_executable(MyBase ${SOURCES} ${HEADERS})
target_compile_definitions(MyBase PRIVATE MYBASE_VERSION="${PROJECT_VERSION}")

target_include_directories(MyBase PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(MyBase PRIVATE
    Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Widgets Qt6::WebEngineWidgets Qt6::PrintSupport Qt6::Network Qt6::WebSockets automerge
)

if(MSVC)
    target_compile_options(MyBase PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O1> /GS- /sdl-)
endif()

if(WIN32)
    target_link_libraries(MyBase PRIVATE bcrypt advapi32 crypt32)
endif()

if(MYBASE_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(MyBaseCoreTests
        src/applicationcontext.cpp
        src/contentdelivery.cpp
        src/markdownhtmlrenderer.cpp
        src/localfeatures.cpp
    src/sessionstate.cpp
        src/aicredentials.cpp
        src/aiwritingservice.cpp
        src/collaboration/automerge_document.cpp
        src/collaboration/collaboration_client.cpp
        tests/test_core.cpp
        src/aescrypto.cpp
        src/databasemanager.cpp
        src/sqlitenoterepository.cpp
        src/sqlitetagrepository.cpp
        src/sqlitenotetagrepository.cpp
        src/sqlitesearchrepository.cpp
        src/databaseschema.cpp
        src/notemanager.cpp
        src/notelistmodel.cpp
        src/passwordkdf.cpp
        src/searchengine.cpp
        src/tagmanager.cpp
        src/tagtreemodel.cpp
        src/markdownoutlinemodel.cpp
        src/vaultmanager.cpp
        src/vaultcredentialstore.cpp
    )

    target_include_directories(MyBaseCoreTests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )

    target_link_libraries(MyBaseCoreTests PRIVATE
        Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Gui Qt6::PrintSupport Qt6::Network Qt6::WebSockets Qt6::Test automerge
    )
    if(WIN32)
        target_link_libraries(MyBaseCoreTests PRIVATE bcrypt advapi32 crypt32)
    endif()

    add_test(NAME MyBaseCoreTests COMMAND MyBaseCoreTests)
endif()

if(NOT WIN32)
    target_link_libraries(MyBase PRIVATE OpenSSL::Crypto)
    if(MYBASE_BUILD_TESTS)
        target_link_libraries(MyBaseCoreTests PRIVATE OpenSSL::Crypto)
    endif()
endif()
