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

sudo insmod steering_module.ko left_motor_pin=530 right_motor_pin=531 left_endstop_pin=535 right_endstop_pin=537
assert_cmd $? "Загрузка steering_module.ko"

[ -c /dev/steering_controller ]
assert_cmd $? "Файл устройства /dev/steering_controller создан"

echo "left" | sudo tee /dev/steering_controller > /dev/null
sudo cat /dev/steering_controller | grep -q "State: Left"
assert_cmd $? "Команда 'left' перевела рулевой механизм влево"

echo "right" | sudo tee /dev/steering_controller > /dev/null
sudo cat /dev/steering_controller | grep -q "State: Right"
assert_cmd $? "Команда 'right' перевела рулевой механизм вправо"

echo "stop" | sudo tee /dev/steering_controller > /dev/null
sudo cat /dev/steering_controller | grep -q "State: Stop"
assert_cmd $? "Команда 'stop' отключила питание рулевого привода"

sudo sh -c 'echo "" > /dev/steering_controller' 2>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${GREEN}[PASS] Запись пустой строки была корректно заблокирована драйвером${NC}"
else
    echo -e "${RED}[FAIL] Драйвер проигнорировал пустую запись и вернул статус успеха${NC}"
    exit 1
fi

sudo rmmod steering_module
assert_cmd $? "Выгрузка steering_module из ядра"

echo -e "${GREEN}Тестирование steering_module завершено успешно${NC}\n"