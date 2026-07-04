#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/slab.h> 

MODULE_LICENSE("GPL");

#define DEVICE_NAME	"motors_controller"
#define CLASS_NAME	"motors"

enum motor_state {
	MOTOR_STATE_STOP     = 0,
	MOTOR_STATE_FORWARD  = 1,
	MOTOR_STATE_BACKWARD = 2
};

static int default_left_fwd_pin  = 100;
static int default_left_bwd_pin  = 101;
static int default_right_fwd_pin = 102;
static int default_right_bwd_pin = 103;

module_param_named(left_fwd_pin, default_left_fwd_pin, int, S_IRUGO);
MODULE_PARM_DESC(left_fwd_pin, "GPIO pin for left motor forward");
module_param_named(left_bwd_pin, default_left_bwd_pin, int, S_IRUGO);
MODULE_PARM_DESC(left_bwd_pin, "GPIO pin for left motor backward");
module_param_named(right_fwd_pin, default_right_fwd_pin, int, S_IRUGO);
MODULE_PARM_DESC(right_fwd_pin, "GPIO pin for right motor forward");
module_param_named(right_bwd_pin, default_right_bwd_pin, int, S_IRUGO);
MODULE_PARM_DESC(right_bwd_pin, "GPIO pin for right motor backward");

struct motors_dev_context {
	struct class *motors_class;
	struct device *motors_device;
	struct cdev motor_cdev;
	dev_t dev_num;

	int left_fwd_pin;
	int left_bwd_pin;
	int right_fwd_pin;
	int right_bwd_pin;

	int current_state;
	struct mutex motors_mutex;

	int motors_gpios[4];
	int rets[4];
	const char *motors_labels[4];
};

static struct motors_dev_context *motors_cntx = NULL;

#if defined(CONFIG_MOTORS_MODULE_TEST)
int run_motors_tests(void);
#endif

static int motors_parse_command(char *command)
{
	char *trimmed_cmd;
	trimmed_cmd = strim(command);
    if (strcmp(trimmed_cmd, "forward") == 0)
		return MOTOR_STATE_FORWARD;
	else if (strcmp(trimmed_cmd, "backward") == 0)
		return MOTOR_STATE_BACKWARD;
	else if (strcmp(trimmed_cmd, "stop") == 0)
        return MOTOR_STATE_STOP;        
    else 
        return -EINVAL;
}

static int motors_format_status(char *buffer, size_t buffer_size, int state, 
    int left_fwd_pin, int left_bwd_pin, int right_fwd_pin, int right_bwd_pin)
{
    const char *state_str;
    switch (state) {
	case MOTOR_STATE_STOP:
		state_str = "Stop";
		break;
	case MOTOR_STATE_FORWARD:
		state_str = "Forward";
		break;
	case MOTOR_STATE_BACKWARD:
		state_str = "Backward";
		break;
	default:
		state_str = "Unknown";
	}

    return snprintf(buffer, buffer_size,
		"Motors status:\n"
		"State: %s\n"
		"Left motor: GPIO%d (FWD), GPIO%d (BWD)\n"
		"Right motor: GPIO%d (FWD), GPIO%d (BWD)\n"
		"Movement state: %d\n",
		state_str, left_fwd_pin, left_bwd_pin,
		right_fwd_pin, right_bwd_pin, state);
}

static void cleanup_gpios(struct motors_dev_context *cntx)
{
	for (int i = 0; i < 4; i++) {	
		if (cntx->rets[i] == 0) {
			gpio_set_value(cntx->motors_gpios[i], 0);
			gpio_free(cntx->motors_gpios[i]);
			cntx->rets[i] = -1;
		}
	}
}

static int init_gpios(struct motors_dev_context *cntx)
{   
	int ret;

	if (cntx->left_fwd_pin < 0  || cntx->left_bwd_pin < 0 || 
	    cntx->right_fwd_pin < 0 || cntx->right_bwd_pin < 0) {
		pr_err("Invalid GPIO pin number configuration\n");
		return -EINVAL;
	}

	cntx->motors_gpios[0] = cntx->left_fwd_pin;
	cntx->motors_gpios[1] = cntx->left_bwd_pin;
	cntx->motors_gpios[2] = cntx->right_fwd_pin;
	cntx->motors_gpios[3] = cntx->right_bwd_pin;

	cntx->motors_labels[0] = "motor_left_fwd";
	cntx->motors_labels[1] = "motor_left_bwd";
	cntx->motors_labels[2] = "motor_right_fwd";
	cntx->motors_labels[3] = "motor_right_bwd";

	for (int i = 0; i < 4; i++) {
		cntx->rets[i] = gpio_request(cntx->motors_gpios[i], cntx->motors_labels[i]);
		if (cntx->rets[i]) {
			pr_err("Failed to request GPIO %d: %d\n", cntx->motors_gpios[i], cntx->rets[i]);
			cleanup_gpios(cntx);
			return cntx->rets[i];
		}
		
		ret = gpio_direction_output(cntx->motors_gpios[i], 0);
		if (ret) {
			pr_err("Failed to set GPIO %d direction: %d\n", cntx->motors_gpios[i], ret);
			cleanup_gpios(cntx);
			return ret;
		}
	}
	return 0;
}

static void set_motors_pins(struct motors_dev_context *cntx, int l_fwd, int l_bwd, int r_fwd, int r_bwd)
{
	gpio_set_value(cntx->left_fwd_pin, l_fwd);
	gpio_set_value(cntx->left_bwd_pin, l_bwd);
	gpio_set_value(cntx->right_fwd_pin, r_fwd);
	gpio_set_value(cntx->right_bwd_pin, r_bwd);
}

static void stop_moving(struct motors_dev_context *cntx)
{
	if (cntx->current_state == MOTOR_STATE_STOP)
		return;
	
	set_motors_pins(cntx, 0, 0, 0, 0);
	cntx->current_state = MOTOR_STATE_STOP;
	pr_info("Motors stopped\n");
}

static void move_forward(struct motors_dev_context *cntx)
{
	set_motors_pins(cntx, 1, 0, 1, 0);
	cntx->current_state = MOTOR_STATE_FORWARD;
	pr_info("Moving forward\n");
}

static void move_backward(struct motors_dev_context *cntx)
{	
	set_motors_pins(cntx, 0, 1, 0, 1);
	cntx->current_state = MOTOR_STATE_BACKWARD;
	pr_info("Moving backward\n");
}

static void change_state(struct motors_dev_context *cntx, int new_state)
{
	if (cntx->current_state == new_state)
		return;
	
	stop_moving(cntx);
	
	mutex_unlock(&cntx->motors_mutex);
	msleep(300);
	mutex_lock(&cntx->motors_mutex);

	if (new_state == MOTOR_STATE_FORWARD) {
		move_forward(cntx);
	} else if (new_state == MOTOR_STATE_BACKWARD) {
		move_backward(cntx);
	}
}

static int device_open(struct inode *inode, struct file *filep)
{
	struct motors_dev_context *cntx = container_of(inode->i_cdev, struct motors_dev_context, motor_cdev);
	filep->private_data = cntx;
	return 0;
}

static int device_release(struct inode *inode, struct file *filep)
{
	return 0;
}

static ssize_t device_write(struct file *filep, const char __user *buffer,
			    size_t len, loff_t *offset)
{
	struct motors_dev_context *cntx = filep->private_data;
	char command[32] = {0};
	size_t cmd_len = min(len, sizeof(command) - 1);
    int target_state;

	if (copy_from_user(command, buffer, cmd_len)){
		pr_err("Failed to copy from user\n");
		return -EFAULT;
	}
	command[cmd_len] = '\0';

	mutex_lock(&cntx->motors_mutex);

    target_state = motors_parse_command(command);
    if(target_state < 0){
        mutex_unlock(&cntx->motors_mutex);
        return -EINVAL;
    }

    change_state(cntx, target_state);

	mutex_unlock(&cntx->motors_mutex);

	return len;
}

static ssize_t device_read(struct file *filep, char __user *buffer,
			   size_t len, loff_t *offset)
{
	struct motors_dev_context *cntx = filep->private_data;
	char status[256];
	int status_len, state;

	mutex_lock(&cntx->motors_mutex);
	state = cntx->current_state;
	mutex_unlock(&cntx->motors_mutex);
    
	status_len = motors_format_status(status, sizeof(status), state, cntx->left_fwd_pin, cntx->left_bwd_pin,
		cntx->right_fwd_pin, cntx->right_bwd_pin);
    
	return simple_read_from_buffer(buffer, len, offset, status, status_len);
}

static struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = device_open,
	.release = device_release,
	.read    = device_read,
	.write   = device_write
};

static int register_motors_chrdev(struct motors_dev_context *cntx)
{
	int ret;

	ret = alloc_chrdev_region(&cntx->dev_num, 0, 1, "motors_controller");
	if (ret) {
		pr_err("Failed to allocate device number: %d\n", ret);
		return ret;
	}
    
	cdev_init(&cntx->motor_cdev, &fops);
	cntx->motor_cdev.owner = THIS_MODULE;
    
	ret = cdev_add(&cntx->motor_cdev, cntx->dev_num, 1);
	if (ret) {
		pr_err("Failed to add cdev\n");
		unregister_chrdev_region(cntx->dev_num, 1);
		return ret;
	}
   
	cntx->motors_class = class_create(CLASS_NAME);
	if (IS_ERR(cntx->motors_class)) {
		pr_err("Failed to create class\n");
		cdev_del(&cntx->motor_cdev);
		unregister_chrdev_region(cntx->dev_num, 1);
		return PTR_ERR(cntx->motors_class);
	}
    
	cntx->motors_device = device_create(cntx->motors_class, NULL, cntx->dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(cntx->motors_device)) {
		pr_err("Failed to create device\n");
		class_destroy(cntx->motors_class);
		cdev_del(&cntx->motor_cdev);
		unregister_chrdev_region(cntx->dev_num, 1);
		return PTR_ERR(cntx->motors_device);
	}
    
	return 0;
}

static void unregister_motors_chrdev(struct motors_dev_context *cntx)
{
	if (!IS_ERR_OR_NULL(cntx->motors_device))
		device_destroy(cntx->motors_class, cntx->dev_num);

	if (!IS_ERR_OR_NULL(cntx->motors_class))
		class_destroy(cntx->motors_class);

	cdev_del(&cntx->motor_cdev);
	unregister_chrdev_region(cntx->dev_num, 1);
}

static int __init motors_init(void)
{   
	int ret;
    
	motors_cntx = kzalloc(sizeof(struct motors_dev_context), GFP_KERNEL);
	if (!motors_cntx)
		return -ENOMEM;

	motors_cntx->left_fwd_pin  = default_left_fwd_pin;
	motors_cntx->left_bwd_pin  = default_left_bwd_pin;
	motors_cntx->right_fwd_pin = default_right_fwd_pin;
	motors_cntx->right_bwd_pin = default_right_bwd_pin;

	motors_cntx->current_state = MOTOR_STATE_STOP;
	mutex_init(&motors_cntx->motors_mutex);

	for (int i = 0; i < 4; i++) {
		motors_cntx->rets[i] = -1;
	}

	ret = init_gpios(motors_cntx);
	if (ret) {
		mutex_destroy(&motors_cntx->motors_mutex);
		kfree(motors_cntx);
		return ret;
	}

	mutex_lock(&motors_cntx->motors_mutex);
    stop_moving(motors_cntx);
    mutex_unlock(&motors_cntx->motors_mutex);
    
	ret = register_motors_chrdev(motors_cntx);
	if (ret) {
		cleanup_gpios(motors_cntx);
		mutex_destroy(&motors_cntx->motors_mutex);
		kfree(motors_cntx);
		return ret;
	}
    
	pr_info("Motors driver loaded successfully\n");

	#if defined(CONFIG_MOTORS_MODULE_TEST)
    ret = run_motors_tests();
    if (ret) {
        unregister_motors_chrdev(motors_cntx);
        cleanup_gpios(motors_cntx);
        mutex_destroy(&motors_cntx->motors_mutex);
        kfree(motors_cntx);
        return ret;
    }
    #endif

	return 0;
}

static void __exit motors_exit(void)
{
	if (motors_cntx) {
		unregister_motors_chrdev(motors_cntx);

		mutex_lock(&motors_cntx->motors_mutex);
		stop_moving(motors_cntx);
		mutex_unlock(&motors_cntx->motors_mutex);

		cleanup_gpios(motors_cntx);
		mutex_destroy(&motors_cntx->motors_mutex);
		kfree(motors_cntx);
	}
    
	pr_info("Motors driver unloaded\n");
}

module_init(motors_init);
module_exit(motors_exit);

#if defined(CONFIG_MOTORS_MODULE_TEST)
#include "motors_module_test.c"
#endif