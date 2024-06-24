#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/string.h>

static struct kobject *nlp_kobj;
static char model_path[256] = "/path/to/default/model";
static int preprocessing_option = 0;
static int postprocessing_option = 0;

static ssize_t model_path_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%s\n", model_path);
}

static ssize_t model_path_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    strncpy(model_path, buf, sizeof(model_path) - 1);
    model_path[sizeof(model_path) - 1] = '\0';
    return count;
}

static ssize_t preprocessing_option_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", preprocessing_option);
}

static ssize_t preprocessing_option_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    sscanf(buf, "%d", &preprocessing_option);
    return count;
}

static ssize_t postprocessing_option_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", postprocessing_option);
}

static ssize_t postprocessing_option_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    sscanf(buf, "%d", &postprocessing_option);
    return count;
}

static struct kobj_attribute model_path_attribute = __ATTR(model_path, 0660, model_path_show, model_path_store);
static struct kobj_attribute preprocessing_option_attribute = __ATTR(preprocessing_option, 0660, preprocessing_option_show, preprocessing_option_store);
static struct kobj_attribute postprocessing_option_attribute = __ATTR(postprocessing_option, 0660, postprocessing_option_show, postprocessing_option_store);

static struct attribute *attrs[] = {
    &model_path_attribute.attr,
    &preprocessing_option_attribute.attr,
    &postprocessing_option_attribute.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};

static int __init nlp_config_init(void) {
    int retval;

    nlp_kobj = kobject_create_and_add("nlp_config", kernel_kobj);
    if (!nlp_kobj)
        return -ENOMEM;

    retval = sysfs_create_group(nlp_kobj, &attr_group);
    if (retval)
        kobject_put(nlp_kobj);

    return retval;
}

static void __exit nlp_config_exit(void) {
    kobject_put(nlp_kobj);
}

module_init(nlp_config_init);
module_exit(nlp_config_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kasinadhsarma, Devin");
MODULE_DESCRIPTION("Configuration Interface for NLP Kernel Module");
MODULE_VERSION("0.1");
