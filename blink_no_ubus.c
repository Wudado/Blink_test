#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <uci.h>
#include <strings.h>

#define GPIOCHIP_PATH "/dev/gpiochip0"
#define GPIO_LINE_NUM 26

int main(void)
{

    struct gpiod_chip *chip = NULL;
    struct gpiod_line_settings *settings = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_request_config *req_cfg = NULL;
    struct gpiod_line_request *request = NULL;
    unsigned int offset = GPIO_LINE_NUM;
    int ret, value = 0;
    int blink_delay_us = 0;

     struct uci_context *ctx;
    struct uci_ptr ptr;
    char uci_path[] = "blink.globals.interval"; // The UCI tuple string
    bool extended_syntax = false; // Set to true if using extended syntax like @interface[0]

    ctx = uci_alloc_context();
    if (!ctx) {
        fprintf(stderr, "Failed to allocate UCI context\n");
        return 1;
    }

    // Load the 'network' package
    struct uci_package *p = NULL;
    if (uci_load(ctx, "blink", &p) != UCI_OK) {
        fprintf(stderr, "Failed to load 'blink' package\n");
        uci_free_context(ctx);
        return 1;
    }

    // Lookup the element using uci_lookup_ptr
    if (uci_lookup_ptr(ctx, &ptr, uci_path, extended_syntax) != UCI_OK) {
        fprintf(stderr, "Failed to lookup UCI path '%s'\n", uci_path);
        uci_free_context(ctx);
        return 1;
    }

    // Check if the lookup was successful and retrieve the element
    if (ptr.last && ptr.last->type == UCI_TYPE_OPTION) {
        struct uci_option *option = uci_to_option(ptr.last);
        if (option->type == UCI_TYPE_STRING) {
            blink_delay_us = atoi(option->v.string);
            printf("Value of %s: %s\n", uci_path, option->v.string);
        }
    } else {
        printf("Element at '%s' not found or is not an option.\n", uci_path);
    }

    // Clean up
    uci_unload(ctx, p); // Unload the package
    uci_free_context(ctx);


    printf("Blinking LED on GPIO %u using libgpiod v2.x...\n", offset);

    /* Open the GPIO chip */
    chip = gpiod_chip_open(GPIOCHIP_PATH);
    if (!chip) {
        perror("gpiod_chip_open");
        return EXIT_FAILURE;
    }

    /* Create line settings */
    settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, 0);

    /* Create line configuration and add our line */
    line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);

    /* Create request configuration */
    req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "led-blinker");

    /* Request the line */
    request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!request) {
        perror("gpiod_chip_request_lines");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

    /* Blink loop */
    while (1) {
        value = !value;
        ret = gpiod_line_request_set_value(request, offset, value);
        if (ret < 0) {
            perror("gpiod_line_request_set_value");
            break;
        }
        usleep(blink_delay_us);
    }

    /* Cleanup — only close what’s required */
    gpiod_line_request_release(request);
    gpiod_chip_close(chip);

    return EXIT_SUCCESS;
}

