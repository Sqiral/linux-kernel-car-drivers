#!/bin/bash
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

assert_cmd() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}[PASS] $2${NC}"
    else
        echo -e "${RED}[FAIL] $2${NC}"
        exit 1
    fi
}

make clean && make default
assert_cmd $? "Компиляция модулей"

sudo insmod led_module.ko gpio_pin=518
assert_cmd $? "Загрузка led_module.ko (GPIO 518)"

[ -c /dev/led_controller ]
assert_cmd $? "Файл устройства /dev/led_controller создан"

echo "on" | sudo tee /dev/led_controller > /dev/null
sudo cat /dev/led_controller | grep -q "Led is ON"
assert_cmd $? "Команда 'on' успешно переключила состояние драйвера"

echo "off" | sudo tee /dev/led_controller > /dev/null
sudo cat /dev/led_controller | grep -q "Led is OFF"
assert_cmd $? "Команда 'off' успешно переключила состояние драйвера"

echo "   on   " | sudo tee /dev/led_controller > /dev/null
sudo cat /dev/led_controller | grep -q "Led is ON"
assert_cmd $? "Драйвер успешно очистил пробелы вокруг команды 'on'"

sudo sh -c 'echo "invalid_command" > /dev/led_controller' 2>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${GREEN}[PASS] Драйвер отклонил некорректную команду с ошибкой (EINVAL)${NC}"
else
    echo -e "${RED}[FAIL] Драйвер пропустил некорректную команду 'invalid_command'${NC}"
    exit 1
fi

sudo rmmod led_module
assert_cmd $? "Выгрузка led_module из ядра"

echo -e "${GREEN} Тестирование led_module завершено успешно${NC}\n"