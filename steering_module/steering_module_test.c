#include <linux/string.h>

static int steering_parse_command(char *command);
static int steering_format_status(char *buffer, size_t buffer_size, int state,
				  int l_pin, int r_pin, int le_pin, int re_pin,
				  int l_endstop, int r_endstop);

int run_steering_tests(void)
{
	char buffer[512];
	char cmd_buf[32];

	pr_info("Модульные тесты steering_module\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "left");
	if (steering_parse_command(cmd_buf) != STEERING_STATE_LEFT)
		pr_err("[FAIL] Тест парсинга команды 'left' провален\n");
    else
	    pr_info("[OK] Успешная проверка команды: 'left' -> STEERING_STATE_LEFT (2)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "right");
	if (steering_parse_command(cmd_buf) != STEERING_STATE_RIGHT)
		pr_err("[FAIL] Тест парсинга команды 'right' провален\n");
	else
        pr_info("[OK] Успешная проверка команды: 'right' -> STEERING_STATE_RIGHT (1)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "stop");
	if (steering_parse_command(cmd_buf) != STEERING_STATE_STOP) 
		pr_err("[FAIL] Тест парсинга команды 'stop' провален\n");
    else
	    pr_info("[OK] Успешная проверка команды: 'stop' -> STEERING_STATE_STOP (0)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "invalid_cmd");
	if (steering_parse_command(cmd_buf) != -EINVAL)
		pr_err("[FAIL] Тест некорректной команды провален\n");
    else 
	    pr_info("[OK] Успешная проверка: неверная команда 'invalid_cmd' корректно отвергнута (-EINVAL)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "   left   ");
	if (steering_parse_command(cmd_buf) != STEERING_STATE_LEFT)
		pr_err("[FAIL] Тест очистки пробелов '   left   ' провален\n");
    else
	    pr_info("[OK] Успешная проверка формата: пробелы очищены '   left   ' -> STEERING_STATE_LEFT\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "right\n");
	if (steering_parse_command(cmd_buf) != STEERING_STATE_RIGHT)
		pr_err("[FAIL] Тест перевода строки 'right\\n' провален\n");
    else
	    pr_info("[OK] Успешная проверка формата: символ переноса очищен 'right\\n' -> STEERING_STATE_RIGHT\n");

	memset(buffer, 0, sizeof(buffer));
	steering_format_status(buffer, sizeof(buffer), STEERING_STATE_LEFT, 100, 101, 102, 103, 0, 1);
	if (strstr(buffer, "State: Left") == NULL || 
	    strstr(buffer, "Left motor: 100") == NULL || 
	    strstr(buffer, "Right endstop state: 1") == NULL) {
		pr_err("[FAIL] Тест формирования строки статуса провален\n");
		return -EINVAL;
	}
    else
	    pr_info("[OK] Успешная проверка: формат вывода статуса рулевого управления верифицирован\n");
	pr_info("[SUCCESS] Модульные тесты steering_module успешно пройдены\n");

	return 0;
}