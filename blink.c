#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <uci.h>
#include <strings.h>
#include <libubus.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <signal.h>

#define GPIOCHIP_PATH "/dev/gpiochip0"
#define GPIO_LINE_NUM 26

int blink_delay_ms = 5000; 

//ubus
static struct ubus_context *ctx;
static struct ubus_object my_program_object;
static struct ubus_object_type my_program_object_type;
static struct blob_buf b;

//gpio
static struct uloop_timeout blink_timer;
struct gpiod_line_request *global_request = NULL;
unsigned int global_offset = GPIO_LINE_NUM;
int global_value = 0;

enum {                              //ubas callback
    DELAY_VALUE,
    __DELAY_MAX
};

static const struct blobmsg_policy delay_policy[] = {
    [DELAY_VALUE] = { .name = "delay_ms", .type = BLOBMSG_TYPE_INT32 },
};

static int set_delay_handler(struct ubus_context *ctx, struct ubus_object *obj,
                             struct ubus_request_data *req, const char *method,
                             struct blob_attr *msg)
{
    struct blob_attr *tb[__DELAY_MAX];
    blobmsg_parse(delay_policy, ARRAY_SIZE(delay_policy), tb, blobmsg_data(msg), blobmsg_len(msg));

    if (tb[DELAY_VALUE]) {
        int new_delay = blobmsg_get_u32(tb[DELAY_VALUE]);
        if (new_delay >= 0) {
            blink_delay_ms = new_delay; // Update the global variable
            printf("UBus: Blink delay updated to %d us\n", blink_delay_ms);
            blob_buf_init(&b, 0);
            blobmsg_add_string(&b, "status", "success");
            blobmsg_add_u32(&b, "new_delay_ms", blink_delay_ms);
            ubus_send_reply(ctx, req, b.head);
        } else {
            return UBUS_STATUS_INVALID_ARGUMENT;
        }
    } else {
        return UBUS_STATUS_INVALID_ARGUMENT;
    }

    return UBUS_STATUS_OK;
}

static const struct ubus_method my_program_methods[] = {
    UBUS_METHOD("set_delay", set_delay_handler, delay_policy),
};



static void blink_timeout_cb(struct uloop_timeout *t)
{
    if (global_request) {
        global_value = !global_value;
        gpiod_line_request_set_value(global_request, global_offset, global_value);
    }
    
    uloop_timeout_set(&blink_timer, blink_delay_ms);  
}

void uci_read_delay() 
{
    struct uci_context *uci_ctx;
    struct uci_ptr ptr;
    char uci_path[] = "blink.globals.interval";
    bool extended_syntax = false; 
    
    uci_ctx = uci_alloc_context();
    if (uci_ctx && uci_load(uci_ctx, "blink", NULL) == UCI_OK &&
        uci_lookup_ptr(uci_ctx, &ptr, uci_path, extended_syntax) == UCI_OK &&
        ptr.last && ptr.last->type == UCI_TYPE_OPTION && 
        uci_to_option(ptr.last)->type == UCI_TYPE_STRING) {
            blink_delay_ms = atoi(uci_to_option(ptr.last)->v.string);
            printf("Initial delay from UCI: %d us\n", blink_delay_ms);
    } else {
         printf("Using default delay of %d us (UCI lookup failed or not found).\n", blink_delay_ms);
    }
    uci_free_context(uci_ctx);
    return;
}

void free_event(struct gpiod_chip *chip,
                struct gpiod_line_settings *settings, struct gpiod_line_config *line_cfg,
                struct gpiod_request_config *req_cfg) //This block clean up gpio and ubus.
{
    if(ctx)
    {
        ubus_free(ctx);
        ctx = NULL;
    }
    if (global_request) gpiod_line_request_release(global_request);
    if (chip) gpiod_chip_close(chip);
    if (settings) gpiod_line_settings_free(settings);
    if (line_cfg) gpiod_line_config_free(line_cfg);
    if (req_cfg) gpiod_request_config_free(req_cfg);
    uloop_done();
    return;
}

int main(void)
{
    uci_read_delay();

    struct gpiod_chip *chip = NULL;
    struct gpiod_line_settings *settings = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_request_config *req_cfg = NULL;
    int ret;
    

    chip = gpiod_chip_open(GPIOCHIP_PATH);
    if (!chip) {
        perror("gpiod_chip_open");
        return EXIT_FAILURE;
    }

    settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, 0);

    line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, &global_offset, 1, settings);

    req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "led-blinker");

    global_request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!global_request) {
        perror("gpiod_chip_request_lines");
        free_event(chip, settings, line_cfg, req_cfg);
        return EXIT_FAILURE;
    }
    

    uloop_init();

    ctx = ubus_connect(NULL);
    if (!ctx) {
        fprintf(stderr, "Failed to connect to ubus\n");
        free_event(chip, settings, line_cfg, req_cfg);
    }
    ubus_add_uloop(ctx);

    my_program_object_type.name = "blink.control"; // Define the object name
    my_program_object_type.methods = my_program_methods;
    my_program_object_type.n_methods = ARRAY_SIZE(my_program_methods);
    my_program_object.name = "blink.control";
    my_program_object.type = &my_program_object_type;
    my_program_object.methods = my_program_methods;
    my_program_object.n_methods = ARRAY_SIZE(my_program_methods);
    
    ret = ubus_add_object(ctx, &my_program_object);
    if (ret) {
        fprintf(stderr, "Failed to add ubus object: %s\n", ubus_strerror(ret));
        free_event(chip, settings, line_cfg, req_cfg);
    }
    printf("UBus object 'blink.control' registered.\n");

    //blink
    blink_timer.cb = blink_timeout_cb;
    uloop_timeout_set(&blink_timer, blink_delay_ms / 1000); // Set initial delay

    printf("Starting uloop...\n");
    uloop_run(); // This blocks and handles both ubus and timer events

    free_event(chip, settings, line_cfg, req_cfg);

    return EXIT_SUCCESS;
}

