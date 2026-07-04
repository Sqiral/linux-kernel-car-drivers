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

sudo insmod motors_module.ko left_fwd_pin=519 left_bwd_pin=520 right_fwd_pin=522 right_bwd_pin=523
assert_cmd $? "Загрузка motors_module.ko"

[ -c /dev/motors_controller ]
assert_cmd $? "Файл устройства /dev/motors_controller создан"

echo "forward" | sudo tee /dev/motors_controller > /dev/null
sudo cat /dev/motors_controller | grep -q "State: Forward"
assert_cmd $? "Команда 'forward' перевела моторы в режим движения вперед"

echo "backward" | sudo tee /dev/motors_controller > /dev/null
sudo cat /dev/motors_controller | grep -q "State: Backward"
assert_cmd $? "Команда 'backward' перевела моторы в режим движения назад"

echo "stop" | sudo tee /dev/motors_controller > /dev/null
sudo cat /dev/motors_controller | grep -q "State: Stop"
assert_cmd $? "Команда 'stop' успешно остановила моторы"

sudo sh -c 'echo "forw\nard" > /dev/motors_controller' 2>/dev/null
if [ $? -ne 0 ]; then
    echo -e "${GREEN}[PASS] Драйвер успешно отклонил разорванную строку${NC}"
else
    echo -e "${RED}[FAIL] Драйвер принял некорректно форматированную команду${NC}"
    exit 1
fi

sudo rmmod motors_module
assert_cmd $? "Выгрузка motors_module из ядра"

echo -e "${GREEN}Тестирование motors_module завершено успешно${NC}\n"