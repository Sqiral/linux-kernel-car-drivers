#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define DEVICE_NAME "led_controller"
#define CLASS_NAME "led"

enum led_state {
    LED_STATE_OFF = 0,
    LED_STATE_ON = 1
};

struct led_dev_context {
    struct class* led_class;
    struct device* led_device;
    struct cdev led_cdev;
    dev_t dev_num;

    int gpio_pin;
    int current_state;
    int gpios_requested;
    struct mutex led_mutex;
};

static struct led_dev_context *led_cntx = NULL;

static int default_gpio_pin = 100;
module_param_named(gpio_pin, default_gpio_pin, int, S_IRUGO);
MODULE_PARM_DESC(gpio_pin, "GPIO pin number for LED control");

#if defined(CONFIG_LED_MODULE_TEST)
int run_led_tests(void);
#endif

static int led_parse_command(char *command)
{
    char *trimmed_cmd;
    trimmed_cmd = strim(command);

    if (strcmp(trimmed_cmd, "on") == 0) {
        return LED_STATE_ON;
    } else if (strcmp(trimmed_cmd, "off") == 0) {
        return LED_STATE_OFF;
    }
    return -EINVAL;
}

static int led_format_status(char *buffer, size_t buffer_size, int state, int pin)
{
    return snprintf(buffer, buffer_size, "Led is %s (GPIO %d)\n", 
                    state == LED_STATE_ON ? "ON" : "OFF", pin);
}

static int init_gpios(struct led_dev_context *cntx) {
    int ret;
    if (cntx->gpio_pin < 0) {
        pr_err("Invalid GPIO pin number configuration\n");
        return -EINVAL;
    }

    ret = gpio_request(cntx->gpio_pin, "led_pin");
    if (ret) {
        pr_err("Failed to request GPIO %d, error: %d\n", cntx->gpio_pin, ret);
        return ret;
    }
    cntx->gpios_requested = 0;

    ret = gpio_direction_output(cntx->gpio_pin, 0);
    if (ret) {
        pr_err("Failed to set direction for GPIO %d, error: %d\n", cntx->gpio_pin, ret);
        gpio_free(cntx->gpio_pin);
        cntx->gpios_requested = -1;
        return ret;
    }

    return 0;
}

static void cleanup_gpios(struct led_dev_context *cntx) {
    if (cntx->gpios_requested == 0) {
        gpio_set_value(cntx->gpio_pin, 0);
        gpio_free(cntx->gpio_pin);
        cntx->gpios_requested = -1;
    }
}

static int device_open(struct inode *inode, struct file *filep) {
    struct led_dev_context *cntx = container_of(inode->i_cdev, struct led_dev_context, led_cdev);
    filep->private_data = cntx;
    return 0;
}

static int device_release(struct inode *inode, struct file *filep) {
    return 0;
}

static void led_on(struct led_dev_context *cntx)
{
    if(cntx->current_state == LED_STATE_ON)
        return;
    gpio_set_value(cntx->gpio_pin, 1);
    cntx->current_state = LED_STATE_ON;
    pr_info("Turning LED ON\n");
}
static void led_off(struct led_dev_context *cntx)
{
    if(cntx->current_state == LED_STATE_OFF)
        return;
    gpio_set_value(cntx->gpio_pin, 0);
    cntx->current_state = LED_STATE_OFF;
    pr_info("Turning LED OFF\n");
}

static ssize_t device_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    struct led_dev_context *cntx = filep->private_data;
    char command[32] = {0};
    size_t cmd_len = min(len, sizeof(command) - 1);
    int target_state;

    if (copy_from_user(command, buffer, cmd_len)) {
        pr_err("Failed to copy from user\n");
        return -EFAULT;
    }
    command[cmd_len] = '\0';

    target_state = led_parse_command(command);

    mutex_lock(&cntx->led_mutex);
    if (target_state == LED_STATE_ON) {
        led_on(cntx);
    } else if (target_state == LED_STATE_OFF) {
        led_off(cntx);
    } else {
        char *trimmed_cmd = strim(command);
        pr_warn("Unknown command: %s\n", trimmed_cmd);
        mutex_unlock(&cntx->led_mutex);
        return -EINVAL;
    }
    mutex_unlock(&cntx->led_mutex);

    return len;
}

static ssize_t device_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    struct led_dev_context *cntx = filep->private_data;
    char status[64];
    int status_len;
    int state_copy;

    mutex_lock(&cntx->led_mutex);
    state_copy = cntx->current_state;
    mutex_unlock(&cntx->led_mutex);

    status_len = led_format_status(status, sizeof(status), state_copy, cntx->gpio_pin);

    return simple_read_from_buffer(buffer, len, offset, status, status_len);
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_release,
    .read = device_read,
    .write = device_write
};

static int register_led_chrdev(struct led_dev_context *cntx) {
    int ret;
    ret = alloc_chrdev_region(&cntx->dev_num, 0, 1, "led_controller");
    if (ret) {
        pr_err("Failed to allocate device number: %d\n", ret);
        return ret;
    }

    cdev_init(&cntx->led_cdev, &fops);
    cntx->led_cdev.owner = THIS_MODULE;

    ret = cdev_add(&cntx->led_cdev, cntx->dev_num, 1);
    if (ret) {
        pr_err("Failed to add cdev\n");
        unregister_chrdev_region(cntx->dev_num, 1);
        return ret;
    }
    
    cntx->led_class = class_create(CLASS_NAME);
    if (IS_ERR(cntx->led_class)) {
        pr_err("Failed to create class\n");
        cdev_del(&cntx->led_cdev);
        unregister_chrdev_region(cntx->dev_num, 1);
        return PTR_ERR(cntx->led_class);
    }
    
    cntx->led_device = device_create(cntx->led_class, NULL, cntx->dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(cntx->led_device)) {
        pr_err("Failed to create device\n");
        class_destroy(cntx->led_class);
        cdev_del(&cntx->led_cdev);
        unregister_chrdev_region(cntx->dev_num, 1);
        return PTR_ERR(cntx->led_device);
    }
    return 0;
}

static void unregister_led_chrdev(struct led_dev_context *cntx) {
    if (!IS_ERR_OR_NULL(cntx->led_device))
        device_destroy(cntx->led_class, cntx->dev_num);
    if (!IS_ERR_OR_NULL(cntx->led_class)) 
        class_destroy(cntx->led_class);
    cdev_del(&cntx->led_cdev); 
    unregister_chrdev_region(cntx->dev_num, 1);
}

static int __init led_init(void) {
    int ret;

    led_cntx = kzalloc(sizeof(struct led_dev_context), GFP_KERNEL);
    if(!led_cntx) return -ENOMEM;

    led_cntx->gpio_pin = default_gpio_pin;
    led_cntx->current_state = LED_STATE_OFF;
    led_cntx->gpios_requested = -1;
    mutex_init(&led_cntx->led_mutex);

    ret = init_gpios(led_cntx);
    if (ret){
        mutex_destroy(&led_cntx->led_mutex);
        kfree(led_cntx);
        return ret;
    }

    mutex_lock(&led_cntx->led_mutex);
    led_off(led_cntx);
    mutex_unlock(&led_cntx->led_mutex);

    ret = register_led_chrdev(led_cntx);
    if (ret) { 
        cleanup_gpios(led_cntx);
        mutex_destroy(&led_cntx->led_mutex);
        kfree(led_cntx);
        return ret;
    }

    pr_info("LED driver loaded successfully\n");

#if defined(CONFIG_LED_MODULE_TEST)
    ret = run_led_tests();
    if (ret) {
        unregister_led_chrdev(led_cntx);
        cleanup_gpios(led_cntx);
        mutex_destroy(&led_cntx->led_mutex);
        kfree(led_cntx);
        return ret;
    }
#endif

    return 0;
}

static void __exit led_exit(void) {
    if (led_cntx) {
        mutex_lock(&led_cntx->led_mutex);
        led_off(led_cntx);
        mutex_unlock(&led_cntx->led_mutex);

        unregister_led_chrdev(led_cntx);
        cleanup_gpios(led_cntx);
        mutex_destroy(&led_cntx->led_mutex);
        kfree(led_cntx);
    }

    pr_info("LED driver unloaded\n");
}

module_init(led_init);
module_exit(led_exit);

#if defined(CONFIG_LED_MODULE_TEST)
#include "led_module_test.c"
#endif