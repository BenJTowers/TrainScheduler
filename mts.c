#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

// Define the train structure
typedef struct {
    int number;        // Unique identifier for the train
    int direction;     // Direction: 0 for Eastbound, 1 for Westbound
    int priority;      // Priority: 0 for low, 1 for high
    int loading_time;  // Loading time in tenths of a second
    int crossing_time; // Crossing time in tenths of a second
} Train;

#define MAX_TRAINS 100

struct timeval global_start_time;
pthread_barrier_t start_barrier; // Barrier to synchronize train threads

// Mutexes for queue access
pthread_mutex_t eastbound_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t westbound_mutex = PTHREAD_MUTEX_INITIALIZER;
// Mutex for main track access
pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;
// Mutex for output file access
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

// Condition variable for track availability
pthread_cond_t track_available = PTHREAD_COND_INITIALIZER;

// Queues for Eastbound and Westbound trains
Train eastbound_queue[MAX_TRAINS];
Train westbound_queue[MAX_TRAINS];
int eastbound_count = 0;
int westbound_count = 0;
int total_crossed = 0;

// Output file pointer
FILE *outputFile;

// Comparison function to determine the order of trains
int comes_before(Train *a, Train *b) {
    if (a->priority > b->priority) return 1;
    if (a->priority < b->priority) return 0;
    // Priorities are equal
    if (a->loading_time < b->loading_time) return 1;
    if (a->loading_time > b->loading_time) return 0;
    // Loading times are equal
    if (a->number < b->number) return 1;
    else return 0;
}

// Enqueue function to add a train to the appropriate queue
void enqueue(Train train) {
    Train *queue;
    int *count;
    pthread_mutex_t *mutex;
    if (train.direction == 0) { // Eastbound
        queue = eastbound_queue;
        count = &eastbound_count;
        mutex = &eastbound_mutex;
    } else { // Westbound
        queue = westbound_queue;
        count = &westbound_count;
        mutex = &westbound_mutex;
    }
    pthread_mutex_lock(mutex);
    int pos = (*count)++;
    // Insert train at the correct position
    while (pos > 0 && comes_before(&train, &queue[pos - 1])) {
        queue[pos] = queue[pos - 1];
        pos--;
    }
    queue[pos] = train;
    pthread_mutex_unlock(mutex);
}

// Dequeue function to remove a train from the front of the queue
Train dequeue(int direction) {
    Train train;
    if (direction == 0 && eastbound_count > 0) { // Eastbound
        pthread_mutex_lock(&eastbound_mutex);
        train = eastbound_queue[0];
        for (int i = 1; i < eastbound_count; i++) {
            eastbound_queue[i - 1] = eastbound_queue[i];
        }
        eastbound_count--;
        pthread_mutex_unlock(&eastbound_mutex);
    } else if (direction == 1 && westbound_count > 0) { // Westbound
        pthread_mutex_lock(&westbound_mutex);
        train = westbound_queue[0];
        for (int i = 1; i < westbound_count; i++) {
            westbound_queue[i - 1] = westbound_queue[i];
        }
        westbound_count--;
        pthread_mutex_unlock(&westbound_mutex);
    }
    return train;
}

void load_trains(FILE *file, Train trains[], int *train_count) {
    char direction;
    int loading_time, crossing_time;

    while (fscanf(file, " %c %d %d", &direction, &loading_time, &crossing_time) == 3) {
        if (*train_count >= MAX_TRAINS) {
            fprintf(stderr, "Error: Too many trains in input file (max %d)\n", MAX_TRAINS);
            break;
        }

        Train train;
        train.number = *train_count;
        train.direction = (direction == 'e' || direction == 'E') ? 0 : 1;
        train.priority = (direction == 'E' || direction == 'W') ? 1 : 0;
        train.loading_time = loading_time;
        train.crossing_time = crossing_time;

        trains[(*train_count)++] = train;
    }
}

void get_elapsed_time_str(struct timeval start_time, char *buffer, size_t buffer_size) {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    long seconds = current_time.tv_sec - start_time.tv_sec;
    long microseconds = current_time.tv_usec - start_time.tv_usec;
    if (microseconds < 0) {
        seconds -= 1;
        microseconds += 1000000;
    }
    long total_tenths = (seconds * 10) + ((microseconds + 50000) / 100000); // Add 50000 for rounding
    long hours = total_tenths / 36000;
    long minutes = (total_tenths % 36000) / 600;
    long secs = (total_tenths % 600) / 10;
    long tenths = total_tenths % 10;
    snprintf(buffer, buffer_size, "%02ld:%02ld:%02ld.%1ld", hours, minutes, secs, tenths);
}

void *train_thread(void *arg) {
    Train *train = (Train *)arg;
    // Wait for all train threads to be created
    pthread_barrier_wait(&start_barrier);
    // Simulate loading phase by sleeping for the loading time
    usleep(train->loading_time * 100000); // Sleep for loading time in tenths of a second

    // Proceed to enqueue and signal the controller
    char timestamp[20];
    get_elapsed_time_str(global_start_time, timestamp, sizeof(timestamp));
    pthread_mutex_lock(&output_mutex);
    fprintf(outputFile, "%s Train %2d is ready to go %4s\n", timestamp, train->number,
            train->direction == 0 ? "East" : "West");
    pthread_mutex_unlock(&output_mutex);

    enqueue(*train); // Add the train to the appropriate queue after loading

    // Short sleep to ensure ordering
    usleep(2000);

    // Notify the controller that a train is ready
    pthread_cond_signal(&track_available);

    return NULL;
}

void *controller_thread(void *arg) {
    int last_direction = -1; // -1 means no train has crossed yet
    int train_count = *((int *)arg);
    int consecutive_same_direction = 0; // Track consecutive trains from the same direction
    while (1) {
        pthread_mutex_lock(&track_mutex);

        // Wait until there is a train ready to cross
        while (eastbound_count == 0 && westbound_count == 0 && total_crossed < train_count) {
            pthread_cond_wait(&track_available, &track_mutex);
        }

        // Terminate if all trains have crossed
        if (total_crossed >= train_count) {
            pthread_mutex_unlock(&track_mutex);
            break;
        }

        // Determine which train should cross
        Train train_to_cross;
        if (eastbound_count > 0 && westbound_count > 0) {
            // If two trains have crossed in the same direction consecutively, give the other direction a chance
            if (consecutive_same_direction >= 2) {
                if (last_direction == 0 && westbound_count > 0) {
                    train_to_cross = dequeue(1);
                    last_direction = 1;
                    consecutive_same_direction = 1; // Reset count
                } else if (last_direction == 1 && eastbound_count > 0) {
                    train_to_cross = dequeue(0);
                    last_direction = 0;
                    consecutive_same_direction = 1; // Reset count
                } else {
                    // If the opposite direction has no trains, continue with the current direction
                    if (last_direction == 0) {
                        train_to_cross = dequeue(0);
                    } else {
                        train_to_cross = dequeue(1);
                    }
                    consecutive_same_direction++;
                }
            } else {
                // Normal priority logic
                if (eastbound_queue[0].priority > westbound_queue[0].priority) {
                    train_to_cross = dequeue(0);
                    last_direction = 0;
                    consecutive_same_direction = (last_direction == 0) ? consecutive_same_direction + 1 : 1;
                } else if (westbound_queue[0].priority > eastbound_queue[0].priority) {
                    train_to_cross = dequeue(1);
                    last_direction = 1;
                    consecutive_same_direction = (last_direction == 1) ? consecutive_same_direction + 1 : 1;
                } else { // Equal priority
                    // Compare loading times from input
                    if (eastbound_queue[0].loading_time < westbound_queue[0].loading_time) {
                        train_to_cross = dequeue(0);
                        last_direction = 0;
                    } else if (eastbound_queue[0].loading_time > westbound_queue[0].loading_time) {
                        train_to_cross = dequeue(1);
                        last_direction = 1;
                    } else {
                        // Loading times are equal, use train number
                        if (eastbound_queue[0].number < westbound_queue[0].number) {
                            train_to_cross = dequeue(0);
                            last_direction = 0;
                        } else {
                            train_to_cross = dequeue(1);
                            last_direction = 1;
                        }
                    }
                    consecutive_same_direction = 1;
                }
            }
        } else if (eastbound_count > 0) {
            train_to_cross = dequeue(0);
            last_direction = 0;
            consecutive_same_direction = (last_direction == 0) ? consecutive_same_direction + 1 : 1;
        } else {
            train_to_cross = dequeue(1);
            last_direction = 1;
            consecutive_same_direction = (last_direction == 1) ? consecutive_same_direction + 1 : 1;
        }

        total_crossed++;
        pthread_mutex_unlock(&track_mutex);

        // Simulate train crossing
        char timestamp[20];
        get_elapsed_time_str(global_start_time, timestamp, sizeof(timestamp));
        pthread_mutex_lock(&output_mutex);
        fprintf(outputFile, "%s Train %2d is ON the main track going %4s\n", timestamp, train_to_cross.number,
                train_to_cross.direction == 0 ? "East" : "West");
        pthread_mutex_unlock(&output_mutex);
        usleep(train_to_cross.crossing_time * 100000); // Sleep for crossing time in tenths of a second
        get_elapsed_time_str(global_start_time, timestamp, sizeof(timestamp));
        pthread_mutex_lock(&output_mutex);
        fprintf(outputFile, "%s Train %2d is OFF the main track after going %4s\n", timestamp, train_to_cross.number,
                train_to_cross.direction == 0 ? "East" : "West");
        pthread_mutex_unlock(&output_mutex);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    outputFile = fopen("output.txt", "w");
    if (outputFile == NULL) {
        perror("Error opening output file");
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        perror("Error opening input file");
        fclose(outputFile);
        return 1;
    }

    Train trains[MAX_TRAINS];
    int train_count = 0;

    load_trains(file, trains, &train_count);

    fclose(file);

    // Initialize the barrier for train threads
    pthread_barrier_init(&start_barrier, NULL, train_count);

    gettimeofday(&global_start_time, NULL);

    // Create train threads
    pthread_t train_threads[MAX_TRAINS];
    for (int i = 0; i < train_count; i++) {
        if (pthread_create(&train_threads[i], NULL, train_thread, &trains[i]) != 0) {
            perror("Error creating train thread");
            fclose(outputFile);
            return 1;
        }
    }

    // Create controller thread
    pthread_t controller;
    if (pthread_create(&controller, NULL, controller_thread, &train_count) != 0) {
        perror("Error creating controller thread");
        fclose(outputFile);
        return 1;
    }

    // Join train threads
    for (int i = 0; i < train_count; i++) {
        if (pthread_join(train_threads[i], NULL) != 0) {
            perror("Error joining train thread");
            fclose(outputFile);
            return 1;
        }
    }

    // Join controller thread
    if (pthread_join(controller, NULL) != 0) {
        perror("Error joining controller thread");
        fclose(outputFile);
        return 1;
    }

    // Destroy the barrier
    pthread_barrier_destroy(&start_barrier);

    // Destroy mutexes
    pthread_mutex_destroy(&eastbound_mutex);
    pthread_mutex_destroy(&westbound_mutex);
    pthread_mutex_destroy(&track_mutex);
    pthread_mutex_destroy(&output_mutex);

    // Destroy condition variable
    pthread_cond_destroy(&track_available);

    fclose(outputFile);

    return 0;
}
