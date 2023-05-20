#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>


MODULE_LICENSE("GPL");

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;
static char ptree_buffer[TASK_COMM_LEN+14];
static char ptree_node_format[]="%s (%u)\n";
static struct debugfs_blob_wrapper ptreeblob = {
        .data ="",
        .size=0
};

struct proc{
        char proc_name[TASK_COMM_LEN];
        pid_t pid;
        struct list_head list;
};



// assuming input pid is always valid
static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
        pid_t input_pid;
        struct task_struct *temp;
        struct proc proctree;
        struct proc *new_proc;
        struct list_head *cur=0, *next=0; 
        struct proc *curproc;
        char *newdata;


        INIT_LIST_HEAD(&proctree.list);
        ptreeblob.data="";
        ptreeblob.size=0;
        sscanf(user_buffer, "%u", &input_pid);

        // Find task_struct using input_pid. 
        if (!(curr = pid_task(find_vpid(input_pid), PIDTYPE_PID))){
                printk("no task exists for such pid\n");
                return -1;
        }

        // Tracing process tree from input_pid to init(1) process
        // construct root-headed linked list using list_add 
        temp = curr;
        while(temp->pid!=1){
                if(!(new_proc = (struct proc*)kmalloc(sizeof(struct proc), GFP_KERNEL))){
                        printk("struct proc allocation failed\n");
                        return -1;
                };
                strncpy(new_proc->proc_name, temp->comm, sizeof(temp->comm));
                new_proc->pid = temp->pid;
                list_add(&new_proc->list, &proctree.list);
                temp=temp->parent;
        }
        if(!(new_proc = (struct proc*)kmalloc(sizeof(struct proc), GFP_KERNEL))){
                printk("struct proc allocation failed\n");
                return -1;
        };
        strncpy(new_proc->proc_name, temp->comm, sizeof(temp->comm));
        new_proc->pid = temp->pid;
        list_add(&new_proc->list, &proctree.list);

        // printing the content of each proc node to the ptree file.
        // uses 'safe' func as we're deleting the node while looping.
        // list_entry retreives the proc node from its list_head field!!
        list_for_each_safe(cur, next, &proctree.list){
                curproc = list_entry(cur, struct proc, list);
                snprintf(ptree_buffer, sizeof(ptree_buffer), ptree_node_format, curproc->proc_name, curproc->pid);
                newdata = kmalloc(strlen(ptree_buffer)+ptreeblob.size+1, GFP_KERNEL);
                if (!strcmp(ptreeblob.data,"")) kfree(ptreeblob.data);
                strcpy(newdata, ptreeblob.data);
                strcat(newdata, ptree_buffer);
                ptreeblob.data= (void*)newdata;
                ptreeblob.size = strlen(newdata);
                memset(ptree_buffer, 0, sizeof(ptree_buffer));
                list_del(cur);
                kfree(curproc);
        }
        return length;
}


static const struct file_operations dbfs_fops = {
        .write = write_pid_to_input,
};

static int __init dbfs_module_init(void)
{


        dir = debugfs_create_dir("ptree", NULL);
        
        if (!dir) {
                printk("Cannot create ptree dir\n");
                return -1;
        }
        
        inputdir = debugfs_create_file("input", 0644, dir, NULL, &dbfs_fops);
        ptreedir = debugfs_create_blob("ptree", 0644, dir, &ptreeblob); 

        if (!inputdir |!ptreedir){
                printk("input/ptree creation failed\n");
                return -1;
        }
	
	printk("dbfs_ptree module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
        // Implement exit module code
	debugfs_remove_recursive(dir);
	printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
