cmake_minimum_required(VERSION 3.10)
project(FlowShop)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Globalne ostrzeżenia kompilatora
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Wyszukiwanie pakietu Threads (wymagane globalnie przed target_link_libraries)
find_package(Threads REQUIRED)

# Dołączenie katalogu nagłówkowego
include_directories(include)

# Katalog wyjściowy dla gotowych programów binarnych
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Definicje źródeł danych
set(SOURCES
    src/config/ConfigLoader.cpp
    src/messages/MessageConstructor.cpp
    src/ReactorModel.cpp
    src/UDPSocket.cpp
    src/main.cpp
)

set(CONTROLLER_SOURCES
    src/config/ConfigLoader.cpp
    src/messages/MessageConstructor.cpp
    src/UDPSocket.cpp
    src/Controller.cpp
)

# -------------------------------------------------------------------
# 1. Cel: MAIN & MODEL LINUX RT (Kompilacja lokalna/domyślna)
# -------------------------------------------------------------------
add_executable(main ${SOURCES})

add_executable(model_linux_rt src/main_linux_rt.cpp)
target_link_libraries(model_linux_rt PRIVATE Threads::Threads rt) # Dodano 'rt' dla Linux RT
target_compile_definitions(model_linux_rt PRIVATE DISABLE_RT_LOGGING)

# -------------------------------------------------------------------
# 2. Cel: FEED_CONTROLLER -> RASPBERRY PI 4 (Cortex-A72)
# -------------------------------------------------------------------
add_executable(coolant_controller_rpi4 src/coolant_main.cpp ${CONTROLLER_SOURCES})
target_compile_definitions(coolant_controller_rpi4 PRIVATE DISABLE_RT_LOGGING)

# Dodanie bibliotek czasu rzeczywistego i wątków
target_link_libraries(coolant_controller_rpi4 PRIVATE Threads::Threads rt)

# Flagi optymalizacyjne pod procesor RPi 4 (Cortex-A72, domyślnie 64-bit AArch64)
# Jeśli Twój kompilator skrośny na RPi 4 to 32-bit, możesz zmienić armv8-a na armv7-a
target_compile_options(coolant_controller_rpi4 PRIVATE 
    -O3 
    -mcpu=cortex-a72 
    -march=armv8-a+crc+simd
)

# -------------------------------------------------------------------
# 3. Cel: COOLANT_CONTROLLER -> RASPBERRY PI 1 (ARM1176JZF-S)
# -------------------------------------------------------------------
add_executable(feed_controller_pi1 src/feed_main.cpp ${CONTROLLER_SOURCES})
target_compile_definitions(feed_controller_pi1 PRIVATE DISABLE_RT_LOGGING)

# Dodanie bibliotek czasu rzeczywistego i wątków
target_link_libraries(feed_controller_pi1 PRIVATE Threads::Threads rt)

# Rygorystyczne flagi dla starej architektury ARMv6 (Raspberry Pi 1)
# Zapobiegają błędowi "Illegal instruction"
target_compile_options(feed_controller_pi1 PRIVATE 
    -g                    # <--- TA FLAGA JEST KLUCZOWA DLA GDB
    -O0                    # <--- Zmień na czas debugowania z -O3 na -O0 (wyłącza optymalizacje, kod będzie czytelny dla GDB)
    -marm 
    -mcpu=arm1176jzf-s 
    -mfloat-abi=hard 
    -mfpu=vfp
)