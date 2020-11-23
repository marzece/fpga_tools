#include "data_buffer_v2.h"
#include "task_handler.h"
#include "queue.h"
#include "fake_lwip.h"
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>

struct itimerval it_val;
int loop = 1;
pid_t CHILD =0;

double global_timer = 0;
int load_factor = 1;

#define PULSER_RATE 30. // Hz
#define TIMER_PER_LOAD_INCREASE 300 // Seconds per load increase
float current_pulser_rate = PULSER_RATE;

static void launch_reciever() {
    CHILD = fork();
   if(CHILD == 0) {
       // If here I AM the child process
       printf("Launching Event Builder\n");
       char* args[2] = {"./fake_data_builder", NULL};
       execvp(args[0], args);
       exit(0);
   }
}

int delayed_trigger_tasks = 0;
void sig_timer(int signum) {
    if(semaphore) {
        delayed_trigger_tasks +=1;
    } else {
        Task* task = task_pool_alloc();
        if(!task) {
            printf("Trigger arrive and we cannot create a task!\n");
            exit(1);
        }
        task->id = READOUT_TRIGGER;

        push_to_queue(&task_list.trigger_readout_queue, task);
    }

    //global_timer += it_val.it_interval.tv_sec + it_val.it_interval.tv_usec/1e6;
    //if(global_timer > TIMER_PER_LOAD_INCREASE*load_factor) {
    //    load_factor += 1;
    //    it_val.it_interval.tv_usec *= 0.8; // Assuming the the second field is zero (TODO!)
    //    current_pulser_rate *= 1.25;
    //    printf("Increasing load to %0.1f Hz\n", current_pulser_rate);
    //}

    // Reset the timer
    it_val.it_value = it_val.it_interval;
    setitimer(ITIMER_REAL, &it_val, NULL);
}

void clean_up(int signum) {
    if(CHILD) {
        kill(CHILD, SIGTERM);
    }
    printf("Cleaning up\n");
    clean_up_fds();
    loop =0;
}

int main() {
    uint32_t nread;

    initialize_pool();
    initialize_task_list();
    initialize_data_buffer();

    float timer_per_pulse = 1./(float)PULSER_RATE;
    int secs = timer_per_pulse;
    int usecs = (timer_per_pulse - secs)*1e6;

    it_val.it_value.tv_sec = 5;
    it_val.it_value.tv_usec = usecs;

    it_val.it_interval.tv_sec = secs;
    it_val.it_interval.tv_usec = usecs;
    
    signal(SIGALRM, sig_timer);
    signal(SIGINT, clean_up);
    signal(SIGSEGV, clean_up);
    signal(SIGTERM, clean_up);
    setitimer(ITIMER_REAL, &it_val, NULL);

    //Task* task = task_pool_alloc();
    //task->id = READOUT_TRIGGER;
    //push_to_queue(&task_list.trigger_readout_queue, task);

    launch_reciever();
    initialize_lwip();
    data_client = (struct tcp_pcb*) 1; // stupid hack to make it so the client isn't NULL

    printf("\"Readout\" loop starting with %0.1f Hz event rate\n", PULSER_RATE);
    while(loop) {
        nread = check_netif();
        if(nread) {
            send_ack(NULL, NULL, nread);
        }
        if(delayed_trigger_tasks) {
            Task* task = task_pool_alloc();
            if(!task) {
                printf("Trigger arrive and we cannot create a task!\n");
                exit(1);
            }
            task->id = READOUT_TRIGGER;
            push_to_queue(&task_list.trigger_readout_queue, task);
            delayed_trigger_tasks -=1;
        }
        main_loop();
    }

    return 0;
}
