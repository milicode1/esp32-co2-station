
# ESP32 CO2 Station

## 📌 Описание проекта

Проект представляет собой индикатор качества воздуха на базе **ESP32-C3-ZERO** с дисплеем **GC9A01**, датчиком CO2 **SCD41** и "физической" кнопкой для управления. 
При попытке сборки "из коробки" возник ряд сложностей.
Ниже приведены изменения/настройки при которых проект собирается.
В данном репозитории все изменения уже сделаны, за исключением п.4.2 - исправление необходимо будет сделать ПОСЛЕ первого запуска сборки (когда будет создана папка "managed_components/jef-sure__scd4x").

---

## 🔌 Схема подключения

| Компонент | Сигнал | GPIO | Примечание |
|-----------|--------|------|------------|
| **Дисплей GC9A01** | SDA (MOSI) | GPIO6 | SPI data |
| | SCL (CLK) | GPIO4 | SPI clock |
| | CS | GPIO7 | Chip select |
| | DC | GPIO1 | Data/Command |
| | RST | GPIO2 | **Strapping pin!** Должен быть HIGH при загрузке |
| **Датчик SCD41** | SDA | GPIO3 | I2C data |
| | SCL | GPIO5 | I2C clock |
| **Кнопка (TTP223)** | OUT | GPIO0 | Активный уровень HIGH |

#### Все компоненты запитываются от выхода 3.3В платы контроллера  
---

## 🛠️ Требования к среде разработки

- **ESP-IDF v5.2** (установлена через ESP-IDF Installation Manager)
- **VS Code** с расширением **ESP-IDF**
- **Python 3.12+**

---



## ⚙️ Настройка и сборка

### 1. Установка ESP-IDF v5.2

 Установить через ESP-IDF Installation Manager
 Или скачать с официального сайта Espressif

### 2. Клонирование проекта
    git clone https://github.com/jef-sure/esp32-co2-station.git
    cd esp32-co2-station-main

### 3. Скачать графическую библиотеку dgx 
     https://components.espressif.com/components/jef-sure/dgx/versions/0.0.12

    Содержимое поместить в папку проекта 
     components/dgx

### 4. Исправление компонентов под ESP-IDF v5.2
    4.1. Исправление dgx (графическая библиотека)
        Файл: components/dgx/CMakeLists.txt

        Было:
            REQUIRES esp_driver_i2c esp_driver_gpio

        Стало:
            REQUIRES driver

    4.2. Исправление scd4x (датчик CO2) - папка появляется при первой сборке "и сразу выпадает в ошибку"
        Файл: managed_components/jef-sure__scd4x/CMakeLists.txt

        Было:
            REQUIRES esp_driver_i2c esp_driver_gpio

        Стало:
            REQUIRES driver

### 5. Настройка menuconfig
    idf.py menuconfig

    5.1. Настройка Bluetooth (NimBLE)
        Component config → Bluetooth → Host → NimBLE - BLE only

    5.2. Настройка размера Flash
        Serial flasher config → Flash size → 4 MB

    5.3. Оптимизация по размеру
        Compiler options → Optimization Level → Release (-Os)

    5.4. Включение драйверов DGX
        Component config → DGX → Enable SPI transport
        Component config → DGX → Enable GC9a01_panel_driver
        Component config → DGX → Enable RAM-backed virtual_screen
    
    5.5. Выбрать Custom partition table CSV
        Partition Table → Partition Table (Custom partition table CSV) → Custom partition table CSV
        
         Убедиться что появился  пункт (в скобках имя вашего файла с настройками)
        Partition Table → (partitions.csv) Custom partition CSV file

### 6. Настройка файла main\idf_component.yml
    dependencies:
        idf:
            version: '>=5.2.0'
        dgx:
            path: ../components/dgx
        jef-sure/scd4x: '*'
        espressif/qrcode: '*'

### 7. Настройка файла main\CMakeLists.txt
    set(app_sources
        app_fonts.c
        app_mqtt.c
        app_settings.c
        app_webserver.c
        main.c
        provisioning.c
        str.c
        tzones.c
    )

    idf_component_register(
        SRCS ${app_sources}
        REQUIRES esp_event esp_wifi nvs_flash protocomm wifi_provisioning esp_event mbedtls esp_http_client esp_http_server esp_adc driver scd4x mqtt
        INCLUDE_DIRS "."
        WHOLE_ARCHIVE 
            ${CMAKE_BINARY_DIR}/components/dgx/libdgx.a 
            ${CMAKE_BINARY_DIR}/esp-idf/bt/libbt.a
            ${CMAKE_BINARY_DIR}/esp-idf/driver/libdriver.a
    )

    target_compile_options(${COMPONENT_LIB} PRIVATE -Os)

    # ... остальная часть файла без изменений ...

### 8. Настройка partitions.csv
    Name,   Type, SubType, Offset,  Size, Flags
    nvs,      data, nvs,     ,        0x6000,
    phy_init, data, phy,     ,        0x1000,
    factory,  app,  factory, ,        0x3F0000,

### 9. Сборка проекта
    idf.py fullclean
    Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
    idf.py build

### 10. Прошивка
    idf.py -p COM10 flash
    Замените COM10 на ваш COM-порт.

### 11. Мониторинг
    idf.py -p COM10 monitor


### 📦 Зависимости
    Проект использует следующие компоненты:

    jef-sure/dgx — графическая библиотека (локальная копия в components/dgx. Оригинал: https://components.espressif.com/components/jef-sure/dgx/versions/0.0.12)

    jef-sure/scd4x — драйвер датчика SCD41

    espressif/qrcode — генерация QR-кодов для Provisioning

### 📋 Результат сборки
    co2station.bin binary size 0x14f1f0 bytes. 
    Smallest app partition is 0x3f0000 bytes. 
    0x2a0e10 bytes (67%) free.


Дата последнего обновления: июль 2026

