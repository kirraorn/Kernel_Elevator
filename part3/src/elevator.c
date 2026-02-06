#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/sched.h> 
 
// Constants and Pet Structures
#define MIN_FLOOR 1
#define MAX_FLOOR 5
#define MAX_PETS 5
#define MAX_WEIGHT 50
#define NUM_FLOORS 5
#define PROC_FILENAME "elevator"

//Pet types + weights
//part d
#define CH_TYPE 0 
#define PU_TYPE 1 
#define PH_TYPE 2 
#define DA_TYPE 3 

#define CH_WEIGHT 3
#define PU_WEIGHT 14
#define PH_WEIGHT 10
#define DA_WEIGHT 16

typedef struct pet
{
  int type;
  int weight;
  int start_floor;
  int dest_floor;
  struct list_head list;  //linked lead node head
} pet_t;

// state of elevator
typedef enum
{
  OFFLINE,
  IDLE,
  LOADING,
  UP,
  DOWN
} elevator_state_t;

// elevator structure 
typedef struct
{
  elevator_state_t state;
  int current_floor;
  int current_load;
  int current_pets;
  int total_serviced;
  int direction; // 1 for UP, -1 for DOWN

  struct list_head pets_in_elevator;
  struct mutex lock; // was under global but i movqed it here for clarity
  
  // multi threads 
  struct task_struct *scheduler_thread;    
  struct task_struct *transfer_worker;     
  wait_queue_head_t request_wq;            

} elevator_t;

// floor struct
typedef struct
{
  struct list_head waiting_queue;
  int waiting_count;
} floor_t;


//Global variables
static elevator_t elevator;
static floor_t floors[NUM_FLOORS]; 
static struct proc_dir_entry *proc_file; 

// system call handles
extern int (*STUB_start_elevator)(void);
extern int (*STUB_issue_request)(int, int, int);
extern int (*STUB_stop_elevator)(void);

// Kthread function prototypes
static int scheduler_thread_run(void *data);
static int transfer_worker_run(void *data);

// Helper function to safely remove and free a pet
static void remove_and_free_pet(pet_t *pet_to_remove) {
    list_del(&pet_to_remove->list); 
    kfree(pet_to_remove); 
}

// helper to get the character code for printing
static char get_pet_char(int type) {
    switch (type) {
        case CH_TYPE: return 'C';
        case PU_TYPE: return 'P';
        case PH_TYPE: return 'H';
        case DA_TYPE: return 'D';
        default: return '?';
    }
}

// helper function to check if any pets are waiting in the entire building
static int are_pets_waiting(void) {
    for (int i = 0; i < NUM_FLOORS; i++) {
        if (floors[i].waiting_count > 0) return 1;
    }
    return 0;
}


//do all the start, request, stop handlers
int start_elevator_handler(void)
{
    if (mutex_lock_interruptible(&elevator.lock)) return -ERESTARTSYS; //added error handling

    if (elevator.state != OFFLINE) { // Already running
        mutex_unlock(&elevator.lock);
        return 1;
    }
    
    // Start multiple threads
    elevator.scheduler_thread = kthread_run(scheduler_thread_run, NULL, "elev_scheduler");
    elevator.transfer_worker = kthread_run(transfer_worker_run, NULL, "elev_transfer");
    
    //whole error handling for threads
    if (IS_ERR(elevator.scheduler_thread) || IS_ERR(elevator.transfer_worker)) {
        if (elevator.scheduler_thread && !IS_ERR(elevator.scheduler_thread)) kthread_stop(elevator.scheduler_thread);
        if (elevator.transfer_worker && !IS_ERR(elevator.transfer_worker)) kthread_stop(elevator.transfer_worker);
        elevator.scheduler_thread = NULL;
        elevator.transfer_worker = NULL;
        mutex_unlock(&elevator.lock);
        return -ENOMEM;
    }

    elevator.state = IDLE;
    elevator.current_floor = 1;
    elevator.direction = 1; // Start going UP
    mutex_unlock(&elevator.lock);
    return 0;
}

int issue_request_handler(int start_floor, int dest_floor, int type)
{
    if (start_floor < MIN_FLOOR || start_floor > MAX_FLOOR) return 1;
    if (dest_floor < MIN_FLOOR || dest_floor > MAX_FLOOR) return 1;
    if (start_floor == dest_floor) return 1;
    if (type < CH_TYPE || type > DA_TYPE) return 1;

    pet_t *new_pet = kmalloc (sizeof(pet_t), GFP_KERNEL);
    if (!new_pet) return -ENOMEM;

    new_pet->type = type;
    new_pet->start_floor = start_floor;
    new_pet->dest_floor = dest_floor;

     switch (type){
    case CH_TYPE: new_pet -> weight = CH_WEIGHT; break;
    case PU_TYPE: new_pet -> weight = PU_WEIGHT; break;
    case PH_TYPE: new_pet -> weight = PH_WEIGHT; break;
    case DA_TYPE: new_pet -> weight = DA_WEIGHT; break;
    }//end of switch
    
    int floor_idx = start_floor - 1;

    if (mutex_lock_interruptible(&elevator.lock)) {
        kfree(new_pet);
        return -ERESTARTSYS;
    }

    list_add_tail(&new_pet->list, &floors[floor_idx].waiting_queue); 
    floors[floor_idx].waiting_count++;
    
    // Wake up the scheduler thread since new work arrived
    wake_up_interruptible(&elevator.request_wq);
    
    mutex_unlock(&elevator.lock);
    return 0;
} //end of issue request handlet

int stop_elevator_handler(void)
{
    int stop_requested = 0;
    
    if (mutex_lock_interruptible(&elevator.lock))
        return -ERESTARTSYS; 

    if (elevator.state == OFFLINE) {
        stop_requested = 1;
    } else {
        elevator.state = OFFLINE;
        
        // Wake up scheduler so it can check state and exit
        wake_up_interruptible(&elevator.request_wq); 
        
        stop_requested = 0;
    }
    
    mutex_unlock(&elevator.lock);
    return stop_requested;
}


// add kthread implememntation with a thread function and a proc file implementation
// Part 3f: LOOK Scheduling Algorithm

// --- TRANSFER WORKER THREAD (Role: Execute Loading/Unloading and 1s Delay) ---
static int transfer_worker_run(void *data)
{
    pet_t *pet, *next; 
    
    while (!kthread_should_stop()) {
        set_current_state(TASK_INTERRUPTIBLE); // Sleep until woken
        schedule();

        if (kthread_should_stop()) break; // Exit if requested
        
        if (mutex_lock_interruptible(&elevator.lock)) return -ERESTARTSYS; 

        // check if loading 
        if (elevator.state == LOADING) {
            int floor_idx = elevator.current_floor - 1;
            
            // Step 1: UNLOAD pets at current floor
            list_for_each_entry_safe(pet, next, &elevator.pets_in_elevator, list) {
                if (pet->dest_floor == elevator.current_floor) {
                    remove_and_free_pet(pet);
                    elevator.current_pets--;
                    elevator.current_load -= pet->weight;
                    elevator.total_serviced++;
                }
            }
            
            // Step 2: LOAD pets at current floor (FIFO with constraints)
            list_for_each_entry_safe(pet, next, &floors[floor_idx].waiting_queue, list) {
                // Check capacity constraints
                if (elevator.current_pets >= MAX_PETS) break;
                if (elevator.current_load + pet->weight > MAX_WEIGHT) continue;

                list_move_tail(&pet->list, &elevator.pets_in_elevator); 
                
                elevator.current_pets++;
                elevator.current_load += pet->weight;
                floors[floor_idx].waiting_count--;
            }
        }
        
        // unlock mutex and then sleep for 1 second to load or unload
        mutex_unlock(&elevator.lock);
        ssleep(1); // 1.0 second delay for transfer 
        
        // 3. Reacquire lock to safely update state and wake scheduler
        if (mutex_lock_interruptible(&elevator.lock)) return -ERESTARTSYS;
        
        // Transfer is complete, return control to scheduler
        elevator.state = IDLE; 
        wake_up_interruptible(&elevator.request_wq); 

        mutex_unlock(&elevator.lock);
    }
    return 0;
}


// --- SCHEDULER THREAD (Role: Movement and State Control) ---
static int scheduler_thread_run(void *data)
{
    pet_t *pet;
    
    while (!kthread_should_stop()) {
        
        // 1. Wait for work: blocks until new work arrives or checks every 1 sec
        wait_event_interruptible_timeout(elevator.request_wq, 
                                         elevator.state != IDLE || elevator.current_pets > 0 || are_pets_waiting(), 
                                         msecs_to_jiffies(1000));
        
        if (mutex_lock_interruptible(&elevator.lock)) return -ERESTARTSYS; 

	//added this to top
	if (elevator.state == LOADING){

            mutex_unlock(&elevator.lock);

            wait_event_interruptible(elevator.request_wq, elevator.state 
		!= LOADING || kthread_should_stop());
            if (mutex_lock_interruptible(&elevator.lock)) return -ERESTARTSYS;
	}


        // if elevator is OFFLINE and empty, exit thread
        if (elevator.state == OFFLINE && elevator.current_pets == 0) {
             mutex_unlock(&elevator.lock);
             // Also stop worker thread on final exit
             if (elevator.transfer_worker) kthread_stop(elevator.transfer_worker);
             break; 
        }

        // check if idle
        if (elevator.current_pets == 0 && !are_pets_waiting()) {
            elevator.state = IDLE;
            mutex_unlock(&elevator.lock);
            continue; // Go back to wait queue
        }

        // 2. TRANSFER LOGIC (Loading/Unloading)
        int needs_transfer = 0;
        
        // Check pets in elevator need moving
        list_for_each_entry(pet, &elevator.pets_in_elevator, list) {
            if (pet->dest_floor == elevator.current_floor) { needs_transfer = 1; break; }
        }
        // Check for Load
        if (floors[elevator.current_floor - 1].waiting_count > 0) {
		
		list_for_each_entry(pet, &floors[elevator.current_floor -1].waiting_queue, list) {
		 if (elevator.current_pets < MAX_PETS && elevator.current_load + pet->weight <= MAX_WEIGHT) 
			{ needs_transfer = 1; 
				break; }} 
	}


        if (needs_transfer) {
            elevator.state = LOADING;
            wake_up_process(elevator.transfer_worker); // Signal worker to handle transfer

            mutex_unlock(&elevator.lock);
            continue; // Wait for worker to signal back
        }

        // Step 4: LOOK Algorithm - Check for requests in current direction
        int has_requests_above = 0;
        int has_requests_below = 0;
        
        // Check pets in elevator
        list_for_each_entry(pet, &elevator.pets_in_elevator, list) {
            if (pet->dest_floor > elevator.current_floor) has_requests_above = 1;
            if (pet->dest_floor < elevator.current_floor) has_requests_below = 1;
        }
        // Check waiting pets on floors
        for (int i = 0; i < NUM_FLOORS; i++) {
            if (floors[i].waiting_count > 0) {
                if ((i + 1) > elevator.current_floor) has_requests_above = 1;
                if ((i + 1) < elevator.current_floor) has_requests_below = 1;
            }
        }

        // tried to simplify movement logic
        if (elevator.direction == 1) { // UP
            if (elevator.current_floor == MAX_FLOOR || !has_requests_above) {
                elevator.direction = -1; // Reverse if at top or no requests above
            }
        } else { // DOWN (-1)
            if (elevator.current_floor == MIN_FLOOR || !has_requests_below) {
                elevator.direction = 1; // Reverse if at bottom or no requests below
            }
        }
        
        // Execute Movement:
        if ((elevator.direction == 1 && has_requests_above) || (elevator.direction == -1 && has_requests_below)) {
            elevator.state = (elevator.direction == 1) ? UP : DOWN;
            elevator.current_floor += elevator.direction;
            
            mutex_unlock(&elevator.lock);
            ssleep(2); // 2.0 seconds between floors
            continue;
        }

        // unlock the mutex if no movement was made
        mutex_unlock(&elevator.lock);
        ssleep(1); // Small sleep if logic failed to find immediate movement
    }
    return 0;
}


// proc file implementation to show elevator status
static int elevator_proc_show(struct seq_file *m, void *v) {
    struct list_head *temp;
    pet_t *pet;
    int total_waiting = 0;
    
    if (mutex_lock_interruptible(&elevator.lock)) return -ERESTARTSYS; 

    // --- A. Print Elevator Status ---
    const char *state_str;
    switch(elevator.state) {
        case OFFLINE: state_str = "OFFLINE"; break;
        case IDLE:    state_str = "IDLE"; break;
        case LOADING: state_str = "LOADING"; break;
        case UP:      state_str = "UP"; break;
        case DOWN:    state_str = "DOWN"; break;
        default:      state_str = "UNKNOWN";
    }
    
    seq_printf(m, "Elevator state: %s\n", state_str);
    seq_printf(m, "Current floor: %d\n", elevator.current_floor);
    seq_printf(m, "Current load: %d lbs\n", elevator.current_load);
    
    seq_printf(m, "Elevator status:");
    list_for_each(temp, &elevator.pets_in_elevator) {
        pet = list_entry(temp, pet_t, list);
        seq_printf(m, " %c%d", get_pet_char(pet->type), pet->dest_floor);
    }
    seq_printf(m, "\n\n");
    
    // --- B. Print Floor Status ---
    for (int i = NUM_FLOORS -1; i >= 0; i--) {
        seq_printf(m, "[%c] Floor %d: %d ", 
                   (elevator.current_floor == i + 1) ? '*' : ' ', 
                   i + 1, 
                   floors[i].waiting_count);
        
        list_for_each(temp, &floors[i].waiting_queue) {
            pet = list_entry(temp, pet_t, list);
            seq_printf(m, "%c%d ", get_pet_char(pet->type), pet->dest_floor);
        }
        seq_printf(m, "\n");
        total_waiting += floors[i].waiting_count;
    }
    
    // --- C. Print Overall Counts ---
    seq_printf(m, "\nNumber of pets waiting: %d\n", total_waiting);
    seq_printf(m, "Number of pets serviced: %d\n", elevator.total_serviced);
    
    mutex_unlock(&elevator.lock); 
    return 0;
}

static int elevator_proc_open(struct inode *inode, struct file *file) {
    return single_open(file, elevator_proc_show, NULL);
}

static const struct proc_ops elevator_proc_ops = {
    .proc_open    = elevator_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

// modukle entry and exit
static int __init elevator_init(void)
{
    // intiailizing mutxes(part3e)
    mutex_init(&elevator.lock); 
    init_waitqueue_head(&elevator.request_wq);

    //initializing the elevator
    elevator.state = OFFLINE;
    elevator.current_floor = 1;
    elevator.current_load = 0;
    elevator.current_pets = 0;
    elevator.total_serviced = 0;
    elevator.scheduler_thread = NULL; // added these two lines 
    elevator.transfer_worker = NULL;
    INIT_LIST_HEAD (&elevator.pets_in_elevator);

    //intializing the floors
    for(int i = 0; i < NUM_FLOORS; i++){
      INIT_LIST_HEAD(&floors[i].waiting_queue);
      floors[i].waiting_count = 0;
    }// end of for loop

    STUB_start_elevator = start_elevator_handler;
    STUB_issue_request = issue_request_handler;
    STUB_stop_elevator = stop_elevator_handler;
    
    //create /proc entry
    proc_file = proc_create(PROC_FILENAME, 0666, NULL, &elevator_proc_ops);
    if (!proc_file) {
        return -ENOMEM;
    }

  //  printk(KERN_INFO "Elevator module initialized and syscall stubs linked.\n");
    return 0;
}

static void __exit elevator_exit(void)
{
    struct list_head *temp, *next;
    
    //stop thread part 3c
    if (elevator.scheduler_thread) kthread_stop(elevator.scheduler_thread);
    if (elevator.transfer_worker) kthread_stop(elevator.transfer_worker);

    // remove /proc entry
    remove_proc_entry(PROC_FILENAME, NULL);
    
    STUB_start_elevator = NULL;
    STUB_issue_request = NULL;
    STUB_stop_elevator = NULL;
    
    list_for_each_safe(temp, next, &elevator.pets_in_elevator) {
        remove_and_free_pet(list_entry(temp, pet_t, list)); // changed to remove and free for error checking
    }
    for (int i = 0; i < NUM_FLOORS; i++) {
        list_for_each_safe (temp, next, &floors[i].waiting_queue) {
            remove_and_free_pet(list_entry(temp, pet_t, list));
        }
    }

    mutex_destroy(&elevator.lock);
}

module_init(elevator_init);
module_exit(elevator_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Elevator Kernel Module");
