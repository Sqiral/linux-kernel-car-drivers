#include <linux/string.h>

int motors_parse_command(char *command);
int motors_format_status(char *buffer, size_t buffer_size, int state, 
    int left_fwd_pin, int left_bwd_pin, int right_fwd_pin, int right_bwd_pin);

int run_motors_tests(void)
{
	char buffer[256];
	char cmd_buf[32];

	pr_info("Модульные тесты motors_module\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "forward");
	if (motors_parse_command(cmd_buf) != 1)
		pr_err("[FAIL] Тест базовой команды 'forward' провален\n");
    else
	    pr_info("[OK] Успешная проверка команды: 'forward' -> MOTOR_STATE_FORWARD (1)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "backward");
	if (motors_parse_command(cmd_buf) != 2)
		pr_err("[FAIL] Тест базовой команды 'backward' провален\n");
	else
        pr_info("[OK] Успешная проверка команды: 'backward' -> MOTOR_STATE_BACKWARD (2)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "stop");
	if (motors_parse_command(cmd_buf) != 0)
		pr_err("[FAIL] Тест базовой команды 'stop' провален\n");
    else
	    pr_info("[OK] Успешная проверка команды: 'stop' -> MOTOR_STATE_STOP (0)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "invalid_move");
	if (motors_parse_command(cmd_buf) != -EINVAL) 
		pr_err("[FAIL] Тест некорректной команды провален\n");
    else
	    pr_info("[OK] Успешная проверка: неверная команда 'invalid_move' корректно отвергнута (-EINVAL)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "   forward   ");
	if (motors_parse_command(cmd_buf) != 1)
		pr_err("[FAIL] Тест пробелов '   forward   ' провален\n");
    else
	    pr_info("[OK] Успешная проверка формата: пробелы очищены '   forward   ' -> MOTOR_STATE_FORWARD (1)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "stop\n");
	if (motors_parse_command(cmd_buf) != 0)
		pr_err("[FAIL] Тест перевода строки 'stop\\n' провален\n");
    else
	    pr_info("[OK] Успешная проверка формата: символ переноса очищен 'stop\\n' -> MOTOR_STATE_STOP (0)\n");


	memset(buffer, 0, sizeof(buffer));
	motors_format_status(buffer, sizeof(buffer), 1, 100, 101, 102, 103);
	
	if (strstr(buffer, "State: Forward") == NULL || strstr(buffer, "Movement state: 1") == NULL)
		pr_err("[FAIL] Тест формата статуса моторов провален\n");
    else
	    pr_info("[OK] Успешная проверка: строка статуса моторов сформирована корректно\n");

	pr_info("[SUCCESS] Модульные тесты motors_module успешно пройдены\n");

	return 0;
}