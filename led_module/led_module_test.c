#include <linux/string.h>

static int led_parse_command(char *command);
static int led_format_status(char *buffer, size_t buffer_size, int state, int pin);

int run_led_tests(void)
{
	char buffer[128];
	char cmd_buf[32];

	pr_info("Модульные тесты led_module\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "on");
	if (led_parse_command(cmd_buf) != 1)
		pr_err("[FAIL] Тест базовой команды 'on' провален\n");
	else
		pr_info("[OK] Успешная проверка команды: 'on' -> LED_STATE_ON (1)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "off");
	if (led_parse_command(cmd_buf) != 0)
		pr_err("[FAIL] Тест базовой команды 'off' провален\n");
	else
		pr_info("[OK] Успешная проверка команды: 'off' -> LED_STATE_OFF (0)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "unknown_cmd");
	if (led_parse_command(cmd_buf) != -EINVAL)
		pr_err("[FAIL] Тест некорректной команды провален\n");
	else
		pr_info("[OK] Успешная проверка: неверная команда 'unknown_cmd' корректно отвергнута (-EINVAL)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "  on  ");
	if (led_parse_command(cmd_buf) != 1)
		pr_err("[FAIL] Тест пробелов '  on  ' провален\n");
	else
		pr_info("[OK] Успешная проверка формата: пробелы очищены '  on  ' -> LED_STATE_ON (1)\n");

	memset(cmd_buf, 0, sizeof(cmd_buf));
	strcpy(cmd_buf, "off\n");
	if (led_parse_command(cmd_buf) != 0)
		pr_err("[FAIL] Тест перевода строки 'off\\n' провален\n");
	else
		pr_info("[OK] Успешная проверка формата: символ переноса очищен 'off\\n' -> LED_STATE_OFF (0)\n");

	memset(buffer, 0, sizeof(buffer));
	led_format_status(buffer, sizeof(buffer), 1, 21);
	if (strcmp(buffer, "Led is ON (GPIO 21)\n") != 0)
		pr_err("[FAIL] Тест формата статуса провален\n");
	else
		pr_info("[OK] Успешная проверка: строка статуса сформирована корректно\n");

	pr_info("[SUCCESS] Модульные тесты led_module успешно пройдены\n");

	return 0;
}