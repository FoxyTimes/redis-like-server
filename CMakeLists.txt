cmake_minimum_required(VERSION 3.28)
project(repa C)

set(CMAKE_C_STANDARD 23)

#add_compile_options(
#        -fsanitize=address,undefined
#        -fno-omit-frame-pointer
#        -g
#)
#add_link_options(
#        -fsanitize=address,undefined
#        -fno-omit-frame-pointer
#)
#add_compile_options(-fsanitize=leak)
#add_link_options(-fsanitize=leak)

add_compile_options(-O3)

# Папка с заголовками
include_directories(src/include)

# -----------------------------
# Сервер
# -----------------------------
set(SERVER_SOURCES
        src/main.c
        src/logger.c
        src/time.c
        src/listener.c
        src/dispatcher.c
        src/blocking_queue.c
        src/protocol_parser.c
        src/utilities.c
        src/worker.c
        src/workers_pool.c
        src/database.c
        src/memory_pool.c
        src/types.c
        src/config.c
        src/signal_shutdown.c
        src/coroutine_pool.c
)
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(repa ${SERVER_SOURCES})

# pthread нужен для потоков
#target_link_libraries(repa pthread)

# -----------------------------
# Клиент
# -----------------------------
#add_executable(client src/client.c)

