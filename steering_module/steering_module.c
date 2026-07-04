#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define DEVICE_NAME	"steering_controller"
#define CLASS_NAME	"steering"

enum steering_state {
	STEERING_STATE_STOP  = 0,
	STEERING_STATE_RIGHT = 1,
	STEERING_STATE_LEFT  = 2
};

struct steering_dev_context {
	struct class *steering_class;
	struct device *steering_device;
	struct cdev steering_cdev;
	dev_t dev_num;

	int left_motor_pin;
	int right_motor_pin;
	int left_endstop_pin;
	int right_endstop_pin;

	int irq_l;
	int irq_r;

	int current_turn;
	struct mutex steering_mutex;
	struct work_struct endstop_work;

	int steering_gpios[4];
	int rets[4];
	const char *steering_labels[4];
};

static struct steering_dev_context *steering_cntx = NULL;

static int default_left_motor_pin    = 100;
static int default_right_motor_pin   = 101;
static int default_left_endstop_pin  = 102;
static int default_right_endstop_pin = 103;

module_param_named(left_motor_pin, default_left_motor_pin, int, S_IRUGO);
MODULE_PARM_DESC(left_motor_pin, "GPIO pin for left turn motor");
module_param_named(right_motor_pin, default_right_motor_pin, int, S_IRUGO);
MODULE_PARM_DESC(right_motor_pin, "GPIO pin for right turn motor");
module_param_named(left_endstop_pin, default_left_endstop_pin, int, S_IRUGO);
MODULE_PARM_DESC(left_endstop_pin, "GPIO pin for left endstop sensor");
module_param_named(right_endstop_pin, default_right_endstop_pin, int, S_IRUGO);
MODULE_PARM_DESC(right_endstop_pin, "GPIO pin for right endstop sensor");

#if defined(CONFIG_STEERING_MODULE_TEST)
int run_steering_tests(void);
#endif

static int steering_parse_command(char *command)
{
	char *trimmed_cmd = strim(command);

	if (strcmp(trimmed_cmd, "left") == 0)
		return STEERING_STATE_LEFT;
	if (strcmp(trimmed_cmd, "right") == 0)
		return STEERING_STATE_RIGHT;
	if (strcmp(trimmed_cmd, "stop") == 0)
		return STEERING_STATE_STOP;

	return -EINVAL;
}

static int steering_format_status(char *buffer, size_t buffer_size, int state,
				  int l_pin, int r_pin, int le_pin, int re_pin,
				  int l_endstop, int r_endstop)
{
	const char *state_str;

	switch (state) {
	case STEERING_STATE_STOP:
		state_str = "Stop";
		break;
	case STEERING_STATE_RIGHT:
		state_str = "Right";
		break;
	case STEERING_STATE_LEFT:
		state_str = "Left";
		break;
	default:
		state_str = "Unknown";
	}

	return snprintf(buffer, buffer_size,
		"Turn status:\n"
		"State: %s\n"
		"GPIO configuration:\n"
		"Left motor: %d\n"
		"Right motor: %d\n"
		"Left endstop: %d\n"
		"Right endstop: %d\n"
		"Left endstop state: %d\n"
		"Right endstop state: %d\n",
		state_str, l_pin, r_pin, le_pin, re_pin, l_endstop, r_endstop);
}

static void cleanup_gpios(struct steering_dev_context *cntx)
{
	int i;
	for (i = 0; i < 4; i++) {
		if (cntx->rets[i] == 0) {
			if (i < 2)
				gpio_set_value(cntx->steering_gpios[i], 0);
			gpio_free(cntx->steering_gpios[i]);
			cntx->rets[i] = -1;
		}
	}
}

static int init_gpios(struct steering_dev_context *cntx)
{
	int ret;
	int i;

	if (cntx->left_motor_pin < 0    || cntx->right_motor_pin < 0 ||
	    cntx->left_endstop_pin < 0   || cntx->right_endstop_pin < 0) {
		pr_err("Invalid GPIO pin number configuration\n");
		return -EINVAL;
	}

	cntx->steering_gpios[0] = cntx->left_motor_pin;
	cntx->steering_gpios[1] = cntx->right_motor_pin;
	cntx->steering_gpios[2] = cntx->left_endstop_pin;
	cntx->steering_gpios[3] = cntx->right_endstop_pin;

	cntx->steering_labels[0] = "steering_left_motor";
	cntx->steering_labels[1] = "steering_right_motor";
	cntx->steering_labels[2] = "steering_left_endstop";
	cntx->steering_labels[3] = "steering_right_endstop";

	for (i = 0; i < 4; i++) {
		cntx->rets[i] = gpio_request(cntx->steering_gpios[i], cntx->steering_labels[i]);
		if (cntx->rets[i]) {
			pr_err("Failed to request GPIO %d: %d\n", cntx->steering_gpios[i], cntx->rets[i]);
			cleanup_gpios(cntx);
			return cntx->rets[i];
		}

		if (i < 2)
			ret = gpio_direction_output(cntx->steering_gpios[i], 0);
		else
			ret = gpio_direction_input(cntx->steering_gpios[i]);

		if (ret) {
			pr_err("Failed to set direction for GPIO %d: %d\n", cntx->steering_gpios[i], ret);
			cleanup_gpios(cntx);
			return ret;
		}
	}
	return 0;
}

static void motor_stop(struct steering_dev_context *cntx)
{
	gpio_set_value(cntx->left_motor_pin, 0);
	gpio_set_value(cntx->right_motor_pin, 0);
	cntx->current_turn = STEERING_STATE_STOP;
	pr_info("Turning stopped\n");
}

static void motor_turn_left(struct steering_dev_context *cntx)
{
	if (gpio_get_value(cntx->left_endstop_pin) == 1) {
		motor_stop(cntx);
		pr_info("Left endstop triggered, stopping\n");
		return;
	}
	motor_stop(cntx);
	gpio_set_value(cntx->left_motor_pin, 1);
	cntx->current_turn = STEERING_STATE_LEFT;
	pr_info("Turning left\n");
}

static void motor_turn_right(struct steering_dev_context *cntx)
{
	if (gpio_get_value(cntx->right_endstop_pin) == 1) {
		motor_stop(cntx);
		pr_info("Right endstop triggered, stopping\n");
		return;
	}
	motor_stop(cntx);
	gpio_set_value(cntx->right_motor_pin, 1);
	cntx->current_turn = STEERING_STATE_RIGHT;
	pr_info("Turning right\n");
}

static void endstop_work_handler(struct work_struct *work)
{
	struct steering_dev_context *cntx = container_of(work, struct steering_dev_context, endstop_work);
	int l_triggered = gpio_get_value(cntx->left_endstop_pin);
	int r_triggered = gpio_get_value(cntx->right_endstop_pin);
    
	if (l_triggered || r_triggered) {
		mutex_lock(&cntx->steering_mutex);
		motor_stop(cntx); 
		mutex_unlock(&cntx->steering_mutex);
	}
}

static irqreturn_t left_endstop_isr(int irq, void *dev_id)
{
	struct steering_dev_context *cntx = dev_id;
	if (cntx)
		schedule_work(&cntx->endstop_work);
	return IRQ_HANDLED;
}

static irqreturn_t right_endstop_isr(int irq, void *dev_id)
{
	struct steering_dev_context *cntx = dev_id;
	if (cntx)
		schedule_work(&cntx->endstop_work);
	return IRQ_HANDLED;
}

static int device_open(struct inode *inode, struct file *filep)
{
	struct steering_dev_context *cntx = container_of(inode->i_cdev, struct steering_dev_context, steering_cdev);
	filep->private_data = cntx;
	pr_info("Device opened\n");
	return 0;
}

static int device_release(struct inode *inode, struct file *filep)
{
	pr_info("Device closed\n");
	return 0;
}

static ssize_t device_write(struct file *filep, const char __user *buffer,
			    size_t len, loff_t *offset)
{
	struct steering_dev_context *cntx = filep->private_data;
	char command[32] = {0};
	size_t cmd_len = min(len, sizeof(command) - 1);
	int target_state;
    
	if (copy_from_user(command, buffer, cmd_len)) {
		pr_err("Failed to copy from user\n");
		return -EFAULT;
	}
	command[cmd_len] = '\0';
    
	target_state = steering_parse_command(command);
    
	mutex_lock(&cntx->steering_mutex);
    
	if (target_state == STEERING_STATE_LEFT) {
		motor_turn_left(cntx);
	} else if (target_state == STEERING_STATE_RIGHT) {
		motor_turn_right(cntx);
	} else if (target_state == STEERING_STATE_STOP) {
		motor_stop(cntx);
	} else {
		char *trimmed_cmd = strim(command);
		pr_warn("Unknown command: %s\n", trimmed_cmd);
		mutex_unlock(&cntx->steering_mutex);
		return -EINVAL;
	}
    
	mutex_unlock(&cntx->steering_mutex);
	return len;
}

static ssize_t device_read(struct file *filep, char __user *buffer,
			   size_t len, loff_t *offset)
{
	struct steering_dev_context *cntx = filep->private_data;
	char status[256];
	int status_len, state_copy, l_endstop, r_endstop;
    
	mutex_lock(&cntx->steering_mutex);
	state_copy = cntx->current_turn;
	l_endstop  = gpio_get_value(cntx->left_endstop_pin);
	r_endstop  = gpio_get_value(cntx->right_endstop_pin);
	mutex_unlock(&cntx->steering_mutex);
    
	status_len = steering_format_status(status, sizeof(status), state_copy,
					    cntx->left_motor_pin, cntx->right_motor_pin,
					    cntx->left_endstop_pin, cntx->right_endstop_pin,
					    l_endstop, r_endstop);
    
	return simple_read_from_buffer(buffer, len, offset, status, status_len);
}

static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = device_open,
	.release = device_release,
	.read    = device_read,
	.write   = device_write
};

static int register_steering_chrdev(struct steering_dev_context *cntx)
{
	int ret;

	ret = alloc_chrdev_region(&cntx->dev_num, 0, 1, "steering_controller");
	if (ret) {
		pr_err("Failed to allocate device number: %d\n", ret);
		return ret;
	}
    
	cdev_init(&cntx->steering_cdev, &fops);
	cntx->steering_cdev.owner = THIS_MODULE;
    
	ret = cdev_add(&cntx->steering_cdev, cntx->dev_num, 1);
	if (ret < 0) {
		pr_err("Failed to add cdev\n");
		unregister_chrdev_region(cntx->dev_num, 1);
		return -EBUSY;
	}
   
	cntx->steering_class = class_create(CLASS_NAME);
	if (IS_ERR(cntx->steering_class)) {
		pr_err("Failed to create class\n");
		cdev_del(&cntx->steering_cdev);
		unregister_chrdev_region(cntx->dev_num, 1);
		return PTR_ERR(cntx->steering_class);
	}
    
	cntx->steering_device = device_create(cntx->steering_class, NULL, cntx->dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(cntx->steering_device)) {
		pr_err("Failed to create device\n");
		class_destroy(cntx->steering_class);
		cdev_del(&cntx->steering_cdev);
		unregister_chrdev_region(cntx->dev_num, 1);
		return PTR_ERR(cntx->steering_device);
	}
    
	return 0;
}

static void unregister_steering_chrdev(struct steering_dev_context *cntx)
{
	if (!IS_ERR_OR_NULL(cntx->steering_device))
		device_destroy(cntx->steering_class, cntx->dev_num);

	if (!IS_ERR_OR_NULL(cntx->steering_class))
		class_destroy(cntx->steering_class);

	cdev_del(&cntx->steering_cdev);
	unregister_chrdev_region(cntx->dev_num, 1);
}

static int __init steering_init(void)
{
	int ret;
    
	steering_cntx = kzalloc(sizeof(struct steering_dev_context), GFP_KERNEL);
	if (!steering_cntx)
		return -ENOMEM;

	steering_cntx->left_motor_pin    = default_left_motor_pin;
	steering_cntx->right_motor_pin   = default_right_motor_pin;
	steering_cntx->left_endstop_pin  = default_left_endstop_pin;
	steering_cntx->right_endstop_pin = default_right_endstop_pin;

	steering_cntx->current_turn = STEERING_STATE_STOP;
	steering_cntx->irq_l = -1;
	steering_cntx->irq_r = -1;
	mutex_init(&steering_cntx->steering_mutex);

	ret = init_gpios(steering_cntx);
	if (ret) {
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return ret;
	}
    
	mutex_lock(&steering_cntx->steering_mutex);
	motor_stop(steering_cntx);
	mutex_unlock(&steering_cntx->steering_mutex);

	INIT_WORK(&steering_cntx->endstop_work, endstop_work_handler);

	steering_cntx->irq_l = gpio_to_irq(steering_cntx->left_endstop_pin);
	if (steering_cntx->irq_l < 0) {
		pr_err("Failed to get IRQ for left endstop\n");
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return steering_cntx->irq_l;
	}
    
	ret = request_irq(steering_cntx->irq_l, left_endstop_isr, IRQF_TRIGGER_RISING, "left_endstop", steering_cntx);
	if (ret) {
		pr_err("Failed to request IRQ %d, error: %d\n", steering_cntx->irq_l, ret);
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return ret;
	}
    
	steering_cntx->irq_r = gpio_to_irq(steering_cntx->right_endstop_pin);
	if (steering_cntx->irq_r < 0) {
		pr_err("Failed to get IRQ for right endstop\n");
		free_irq(steering_cntx->irq_l, steering_cntx);
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return steering_cntx->irq_r;
	}
    
	ret = request_irq(steering_cntx->irq_r, right_endstop_isr, IRQF_TRIGGER_RISING, "right_endstop", steering_cntx);
	if (ret) {
		pr_err("Failed to request IRQ %d, error: %d\n", steering_cntx->irq_r, ret);
		free_irq(steering_cntx->irq_l, steering_cntx);
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return ret;
	}

	ret = register_steering_chrdev(steering_cntx);
	if (ret) {
		free_irq(steering_cntx->irq_r, steering_cntx);
		free_irq(steering_cntx->irq_l, steering_cntx);
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return ret;
	}
    
	pr_info("Steering driver loaded successfully\n");

#if defined(CONFIG_STEERING_MODULE_TEST)
	ret = run_steering_tests();
	if (ret) {
		unregister_steering_chrdev(steering_cntx);
		free_irq(steering_cntx->irq_r, steering_cntx);
		free_irq(steering_cntx->irq_l, steering_cntx);
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
		return ret;
	}
#endif

	return 0;
}

static void __exit steering_exit(void)
{
	if (steering_cntx) {
		unregister_steering_chrdev(steering_cntx);

		if (steering_cntx->irq_l >= 0)
			free_irq(steering_cntx->irq_l, steering_cntx);
		if (steering_cntx->irq_r >= 0)
			free_irq(steering_cntx->irq_r, steering_cntx);
		
		cancel_work_sync(&steering_cntx->endstop_work);

		mutex_lock(&steering_cntx->steering_mutex);
		motor_stop(steering_cntx);
		mutex_unlock(&steering_cntx->steering_mutex);
	
		cleanup_gpios(steering_cntx);
		mutex_destroy(&steering_cntx->steering_mutex);
		kfree(steering_cntx);
	}
    
	pr_info("Steering driver unloaded\n");
}

module_init(steering_init);
module_exit(steering_exit);

#if defined(CONFIG_STEERING_MODULE_TEST)
#include "steering_module_test.c"
#endif