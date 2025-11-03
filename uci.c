#include <uci.h>
#include <stdio.h>
#include <stdbool.h>

int main() {
    struct uci_context *ctx;
    struct uci_ptr ptr;
    char uci_path[] = "network.lan.ipaddr"; // The UCI tuple string
    bool extended_syntax = false; // Set to true if using extended syntax like @interface[0]

    ctx = uci_alloc_context();
    if (!ctx) {
        fprintf(stderr, "Failed to allocate UCI context\n");
        return 1;
    }

    // Load the 'network' package
    struct uci_package *p = NULL;
    if (uci_load(ctx, "network", &p) != UCI_OK) {
        fprintf(stderr, "Failed to load 'network' package\n");
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
            printf("Value of %s: %s\n", uci_path, option->v.string);
        } else if (option->type == UCI_TYPE_LIST) {
            printf("Value of %s is a list:\n", uci_path);
            struct uci_element *e;
            uci_foreach_element(&option->v.list, e) {
                printf("  - %s\n", e->name);
            }
        }
    } else {
        printf("Element at '%s' not found or is not an option.\n", uci_path);
    }

    // Clean up
    uci_unload(ctx, p); // Unload the package
    uci_free_context(ctx);
    return 0;
}
