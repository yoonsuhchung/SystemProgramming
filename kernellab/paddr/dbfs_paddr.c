#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <linux/pgtable.h> 
#include <linux/slab.h>

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

struct packet {
        pid_t pid;
        unsigned long vaddr;
        unsigned long paddr;
};

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
        pid_t proc_pid;
        unsigned long proc_vaddr;
        pgd_t *pgd;
        p4d_t *p4d;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;
        unsigned long ppn;
        struct packet *kernel_buffer;
        
        kernel_buffer = (struct packet *)kmalloc(length, GFP_KERNEL);
        if(copy_from_user(kernel_buffer, user_buffer, length)){
                printk("copying to kernel space failed.\n");
                return -1;
        }
        proc_pid = kernel_buffer->pid;
        proc_vaddr = kernel_buffer->vaddr;

        // find task for given pid
        if (!(task = pid_task(find_vpid(proc_pid), PIDTYPE_PID))){
                printk("no task exists for such pid\n");
                return -1;
        }
        // tracing multi-level PT
        pgd = pgd_offset(task->mm, proc_vaddr);
        if(pgd_none(*pgd) || pgd_bad(*pgd)){
                printk("pgd fail\n");
                return -1;
        }
        p4d = p4d_offset(pgd, proc_vaddr);
        if(p4d_none(*p4d) || p4d_bad(*p4d)){
                printk("p4d fail\n");
                return -1;
        }
        pud = pud_offset(p4d, proc_vaddr);
        if(pud_none(*pud) || pud_bad(*pud)){
                printk("pud fail\n");
                return -1;
        }
        pmd = pmd_offset(pud, proc_vaddr);
        if(pmd_none(*pmd) || pmd_bad(*pmd)){
                printk("pmd fail\n");
                return -1;
        }
        pte = pte_offset_kernel(pmd, proc_vaddr);
        if (pte_present(*pte)){
                ppn = pte_pfn(*pte);
                kernel_buffer->paddr = (ppn<<12) | (proc_vaddr&(0xfff));
        }
        else{
                printk("pte fail\n");
                return -1;
        }
        if(copy_to_user(user_buffer, kernel_buffer, length)){
                printk("copying to user space failed.\n");
                return -1;
        }
        return length;
}

static const struct file_operations dbfs_fops = {
        // Mapping file operations with your functions
        .read = read_output,
};

static int __init dbfs_module_init(void)
{

        dir = debugfs_create_dir("paddr", NULL);

        if (!dir) {
                printk("Cannot create paddr dir\n");
                return -1;
        }

        // Fill in the arguments below
        output = debugfs_create_file("output", 0644, dir, NULL, &dbfs_fops);


	printk("dbfs_paddr module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
        // Implement exit module
        debugfs_remove_recursive(dir);
	printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
