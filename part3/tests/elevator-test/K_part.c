#include <linux/sched.h>   // For schedule()
#include <linux/delay.h>   // For ssleep()

static bool has_requests_above(int floor) {
    Pet *pet;
    int f;
  
    list_for_each_entry(pet, &elevator_pets, list) {
        if (pet->dest_floor > floor)
            return true;
    }
    
    for (f = floor + 1; f <= 5; f++) {
        if (!list_empty(&floor_queues[f]))
            return true;
    }
    
    return false;
}

static bool has_requests_below(int floor) {
    Pet *pet;
    int f;
    

    list_for_each_entry(pet, &elevator_pets, list) {
        if (pet->dest_floor < floor)
            return true;
    }
    
 
    for (f = 1; f < floor; f++) {
        if (!list_empty(&floor_queues[f]))
            return true;
    }
    
    return false;
}
static bool any_waiting_pets(void) {
    int f;
    for (f = 1; f <= 5; f++) {
        if (!list_empty(&floor_queues[f]))
            return true;
    }
    return false;
}

static void unload_pets(void) {
    Pet *pet, *tmp;
    
    list_for_each_entry_safe(pet, tmp, &elevator_pets, list) {
        if (pet->dest_floor == current_floor) {
            state = LOADING;
            list_del(&pet->list);
            current_weight -= pet->weight;
            num_pets_in_elevator--;
            pets_serviced++;
            
            // Unlock mutex before sleeping
            mutex_unlock(&elevator_mutex);
            ssleep(1); // 1 second to unload
            
            // Re-acquire lock
            if (mutex_lock_interruptible(&elevator_mutex))
                return;
            
            kfree(pet);
        }
    }
}

// Load pets at current floor (FIFO with weight/capacity constraints)
static void load_pets(void) {
    Pet *pet, *tmp;
    
    list_for_each_entry_safe(pet, tmp, &floor_queues[current_floor], list) {
        // Check capacity constraints
        if (num_pets_in_elevator >= 5)
            break;
        if (current_weight + pet->weight > 50)
            break;
        
        // Load pet (FIFO order maintained by list)
        state = LOADING;
        list_del(&pet->list);
        list_add_tail(&pet->list, &elevator_pets);
        current_weight += pet->weight;
        num_pets_in_elevator++;
        
        // Unlock mutex before sleeping
        mutex_unlock(&elevator_mutex);
        ssleep(1); // 1 second to load
        
        // Re-acquire lock
        if (mutex_lock_interruptible(&elevator_mutex))
            return;
    }
}

// Main elevator scheduling thread (LOOK Algorithm)
static int elevator_thread(void *data) {
    while (!kthread_should_stop()) {
        
        // Use schedule() to let scheduler decide when to wake
        // Prevents blocking in middle of operations
        schedule();
        
        // Lock shared data with interruptible lock
        if (mutex_lock_interruptible(&elevator_mutex)) {
            // Interrupted by signal, continue
            continue;
        }
        
        // Step 1: Unload pets at current floor
        unload_pets();
        
        // Step 2: Load pets at current floor
        load_pets();
        
        // Step 3: Check if should stop
        if (stop_requested && num_pets_in_elevator == 0 && !any_waiting_pets()) {
            state = OFFLINE;
            mutex_unlock(&elevator_mutex);
            break;
        }
        
        // Step 4: Check if idle (no pets in elevator, no pets waiting)
        if (num_pets_in_elevator == 0 && !any_waiting_pets()) {
            state = IDLE;
            mutex_unlock(&elevator_mutex);
            
            // Block thread when not doing anything useful
            ssleep(1); // Sleep for 1 second when idle
            continue;
        }
        
        // Step 5: LOOK Algorithm - Move in current direction
        if (direction == DIR_UP) {
            if (has_requests_above(current_floor)) {
                // Continue moving up
                state = UP;
                
                // Unlock before sleeping (don't hold lock during delay)
                mutex_unlock(&elevator_mutex);
                ssleep(2); // 2 seconds to move between floors
            
                if (mutex_lock_interruptible(&elevator_mutex))
                    continue;
                
                current_floor++;
                mutex_unlock(&elevator_mutex);
                
            } else if (has_requests_below(current_floor)) {
                // Reverse direction to down
                direction = DIR_DOWN;
                mutex_unlock(&elevator_mutex);
                
            } else {
                // No requests anywhere - go idle
                state = IDLE;
                mutex_unlock(&elevator_mutex);
            }
            
        } else { // direction == DIR_DOWN
            if (has_requests_below(current_floor)) {
                // Continue moving down
                state = DOWN;
                
                // Unlock before sleeping
                mutex_unlock(&elevator_mutex);
                ssleep(2); // 2 seconds to move between floors
                
                // Re-acquire lock to update floor
                if (mutex_lock_interruptible(&elevator_mutex))
                    continue;
                
                current_floor--;
                mutex_unlock(&elevator_mutex);
                
            } else if (has_requests_above(current_floor)) {
                // Reverse direction to up
                direction = DIR_UP;
                mutex_unlock(&elevator_mutex);
                
            } else {
                // No requests anywhere - go idle
                state = IDLE;
                mutex_unlock(&elevator_mutex);
            }
        }
    }
    
    return 0;
}
