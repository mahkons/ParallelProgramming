#include <iostream>
#include <sstream>
#include <vector>
#include <memory>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstring>

#include <pthread.h>

int get_tid();

class CountDownLatch {
public:
    CountDownLatch() {
        pthread_mutex_init(&mutex_, nullptr);
        pthread_cond_init(&zero_, nullptr);
    }

    ~CountDownLatch() {
        pthread_cond_destroy(&zero_);
        pthread_mutex_destroy(&mutex_);
    }

    void init(int count) {
        count_ = count;
    }

    void await() {
        pthread_mutex_lock(&mutex_);
        while (count_) {
            pthread_cond_wait(&zero_, &mutex_);
        }
        pthread_mutex_unlock(&mutex_);
    }

    void count_down() {
        pthread_mutex_lock(&mutex_);
        count_--;
        if (!count_) {
            pthread_cond_broadcast(&zero_);
        }
        pthread_mutex_unlock(&mutex_);
    }

private:
    int count_;
    pthread_mutex_t mutex_;
    pthread_cond_t zero_;
};

int n_consumer;
int max_sleep_ms;
bool debug = false;

pthread_mutex_t storage_mutex;
pthread_cond_t storage_write;
pthread_cond_t storage_read;
bool storage_empty = true;
bool producer_running = true;
CountDownLatch latch;


void* producer_routine(void* arg) {
    latch.await();

    int* value_storage = static_cast<int*>(arg);

    std::string input;
    getline(std::cin, input);
    std::stringstream ss(input);

    int input_value;
    while (ss >> input_value) {
        pthread_mutex_lock(&storage_mutex);
        *value_storage = input_value;
        storage_empty = false;

        pthread_cond_broadcast(&storage_write);
        while (!storage_empty) {
            pthread_cond_wait(&storage_read, &storage_mutex);
        }
        pthread_mutex_unlock(&storage_mutex);
    }

    pthread_mutex_lock(&storage_mutex);
    producer_running = false;
    pthread_cond_broadcast(&storage_write);
    pthread_mutex_unlock(&storage_mutex);

    return nullptr;
}

void* consumer_routine(void* arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
    latch.count_down();

    int* sum = new int(0);
    int* value_storage = static_cast<int*>(arg);
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, max_sleep_ms);

    while (producer_running) {
        pthread_mutex_lock(&storage_mutex);
        while (producer_running && storage_empty) {
            pthread_cond_wait(&storage_write, &storage_mutex);
        }

        bool read_done = false;
        if (!storage_empty) {
            *sum += *value_storage;
            storage_empty = true;
            read_done = true;
            pthread_cond_broadcast(&storage_read);
            if (debug) {
                std::cout << get_tid() << ' ' << *sum << '\n';
            }
        }

        pthread_mutex_unlock(&storage_mutex);

        if (read_done) { // sleep only after summing
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen)));
        }
    }

    return sum;
}

void* consumer_interruptor_routine(void* arg) {
    latch.await();

    std::vector<pthread_t>& consumers = *static_cast<std::vector<pthread_t>*>(arg);
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, n_consumer - 1);

    while (producer_running) {
        pthread_cancel(consumers[dist(gen)]);
    }

    return nullptr;
}

int run_threads() {
    latch.init(n_consumer);
    pthread_mutex_init(&storage_mutex, nullptr);
    pthread_cond_init(&storage_write, nullptr);
    pthread_cond_init(&storage_read, nullptr);

    pthread_t producer, interruptor;
    std::vector<pthread_t> consumers(n_consumer);
    std::unique_ptr<int> value_storage(new int);

    pthread_create(&producer, nullptr, producer_routine, value_storage.get());
    pthread_create(&interruptor, nullptr, consumer_interruptor_routine, &consumers);
    for (int i = 0; i < n_consumer; i++) {
        pthread_create(&consumers[i], nullptr, consumer_routine, value_storage.get());
    }

    pthread_join(producer, nullptr);
    pthread_join(interruptor, nullptr);

    int sum = 0;
    for (int i = 0; i < n_consumer; i++) {
        int* ret_value;
        pthread_join(consumers[i], reinterpret_cast<void**>(&ret_value));
        sum += *ret_value;
        delete ret_value;
    }

    pthread_cond_destroy(&storage_read);
    pthread_cond_destroy(&storage_write);
    pthread_mutex_destroy(&storage_mutex);
    return sum;
}

int get_tid() {
    static std::atomic_int next_id(1);
    static thread_local std::unique_ptr<int> tid(nullptr); // We are explicitly asked to save tid in heap

    if (!tid) {
        tid = std::unique_ptr<int>(new int(next_id.fetch_add(1)));
    }

    return *tid;
}


int main(int argc, char** argv) {
    assert(3 <= argc && argc <= 4);
    n_consumer = std::atoi(argv[1]);
    max_sleep_ms = std::atoi(argv[2]);

    // Assume that --debug flag (if specified) is last argument
    if (argc == 4) {
        std::cerr << argv[3] << std::endl;
        assert(!strcmp(argv[3], "--debug"));
        debug = true;
    }

    std::cout << run_threads() << std::endl;
    return 0;
}
