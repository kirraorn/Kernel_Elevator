#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kirra Orndorff, Kate Payen, Ludginie Dorval");
MODULE_DESCRIPTION("A timer kernel module: Part 2 of Project");
MODULE_VERSION("1.0");

#define ENTRY_NAME "timer"
#define PERMS 0666
#define PARENT NULL
#define BUF_LEN 256

//proc entry pointer
static struct proc_dir_entry* proc_entry;

//store last time for calcs
static struct timespec64 last_time = { .tv_sec = 0, .tv_nsec = 0};
//procfile read function
static ssize_t procfile_read(struct file* file, char* ubuf, size_t count, loff_t *ppos)
{
	int len = 0;
	char msg[BUF_LEN];
	struct timespec64 current_time;
	struct timespec64 diff_time;

	s64 elapsed_ns = 0;
	long long elapsed_sec = 0;
	long elapsed_nsecs = 0;

	//see if data already read or not
	if (*ppos > 0)
		return 0;

	// get current time using the get_real funct
	ktime_get_real_ts64(&current_time);

	//calc elapsed time
	if (last_time.tv_sec != 0 || last_time.tv_nsec != 0)
	{
		diff_time = timespec64_sub(current_time, last_time);
		elapsed_ns = timespec64_to_ns(&diff_time);

		elapsed_sec = elapsed_ns / 1000000000;
		elapsed_nsecs = elapsed_ns % 1000000000;
	}

	//output
	if (last_time.tv_sec == 0 && last_time.tv_nsec == 0)
	{
		len = snprintf(msg, BUF_LEN, "current time: %lld.%09ld\n", (long long)current_time.tv_sec, 
					current_time.tv_nsec);
	}
	else
	{
		len = snprintf(msg, BUF_LEN, "current time: %lld.%09ld\n" "elapsed time: %lld.%09ld\n",
					(long long)current_time.tv_sec, current_time.tv_nsec, elapsed_sec, elapsed_nsecs);
	}

	// copy data to user buffer
	size_t copy_len = min((size_t)len, count);
	if (copy_to_user(ubuf, msg, copy_len))
	{
		return -EFAULT;
	}

	//update last time for next read
	last_time = current_time;

	//update offset and return bytes read
	*ppos = copy_len;
	return copy_len;
}

// make the proc operations strucutre
static const struct proc_ops procfile_fops = {
	.proc_read = procfile_read,
};

//module installation
static int __init my_timer_init(void) {
	proc_entry = proc_create(ENTRY_NAME, PERMS, PARENT, &procfile_fops);

	if (proc_entry == NULL)
		{ return -ENOMEM; }

	//resert last time of module loading
	last_time.tv_sec = 0;
	last_time.tv_nsec = 0;

	return 0;
}

// module exit for when unloading
static void __exit my_timer_exit(void){

	proc_remove(proc_entry);
}

//register the mmodule functs
module_init(my_timer_init);
module_exit(my_timer_exit);
