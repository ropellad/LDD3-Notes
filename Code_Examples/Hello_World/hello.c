// Hello.c file demonstartes command line argument passing with the kernel

// Start by including the appropriate header files
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>

// Give it some sort of license and author so that the kernel does not complain
MODULE_LICENSE("GPL");
MODULE_AUTHOR("DSR");

// Lets initialize some variables of a bunch of different types to play with
static short int short_int = 1;
static int reg_int = 56;
static long int long_int = 1900;
static char *sample_string = "test string";
static int intArray[6] = {1,2,3,4,5,-8};
static int arr_argc = 0;


/* This part introduces command line parameter passing
 * module_param(foo, int, 0000)
 * The first param is the parameters name
 * The second param is it's data type
 * The final argument is the permissions bits, 
 * for exposing parameters in sysfs (if non-zero) at a later stage.
 */
module_param(short_int, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(short_int, "A short integer example");
module_param(reg_int, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(reg_int, "A normal integer example");
module_param(long_int, long, S_IRUSR);
MODULE_PARM_DESC(long_int, "A long integer example");
module_param(sample_string, charp, 0000);
MODULE_PARM_DESC(sample_string, "A character string example");

/* There is a slightly different setup for passing an array of parameters
 * module_param_array(name, type, num, perm);
 * The first param is the parameter's (in this case the array's) name
 * The second param is the data type of the elements of the array
 * The third argument is a pointer to the variable that will store the number 
 * of elements of the array initialized by the user at module loading time
 * The fourth argument is the permission bits
 */
module_param_array(intArray, int, &arr_argc, 0000);
MODULE_PARM_DESC(intArray, "An array of integers");

static int __init hello_init(void)
{
	int i;
	printk(KERN_INFO "Hello, world \n============= \n");
	printk(KERN_INFO "short integer: %hd\n", short_int);
	printk(KERN_INFO "integer: %d\n", reg_int);
	printk(KERN_INFO "long integer: %ld\n", long_int);
	printk(KERN_INFO "string: %s\n", sample_string);
	for (i = 0; i < (sizeof intArray / sizeof (int)); i++)
	{
		printk(KERN_INFO "intArray[%d] = %d\n", i, intArray[i]);
	}
	printk(KERN_INFO "got %d arguments for intArray.\n", arr_argc);
	return 0;
}

static void __exit hello_exit(void)
{
	printk(KERN_INFO "Goodbye, cruel world!\n");
}

module_init(hello_init);
module_exit(hello_exit);
