#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include "data_preprocessing_component.c"

static int __init test_data_preprocessing_init(void) {
    char *input = "Hello World from Kernel Space";
    char *tokens[10];
    int num_tokens;
    int i;

    printk(KERN_INFO "Test Data Preprocessing Component Initialized\n");

    preprocess_input(input, tokens, &num_tokens);

    printk(KERN_INFO "Input: %s\n", input);
    printk(KERN_INFO "Number of Tokens: %d\n", num_tokens);
    for (i = 0; i < num_tokens; i++) {
        printk(KERN_INFO "Token %d: %s\n", i, tokens[i]);
        kfree(tokens[i]);
    }

    return 0;
}

static void __exit test_data_preprocessing_exit(void) {
    printk(KERN_INFO "Test Data Preprocessing Component Exited\n");
}

module_init(test_data_preprocessing_init);
module_exit(test_data_preprocessing_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Devin");
MODULE_DESCRIPTION("Test Module for Data Preprocessing Component");
