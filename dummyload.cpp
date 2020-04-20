/* g++ -O2 dummyload.cpp -lpthread */
#include <iostream>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>

#include <vector>
#include <memory>
using namespace std;

class noncopyable {
protected:
    noncopyable () { }
private:
    noncopyable (const noncopyable&) = delete;
    noncopyable& operator= (const noncopyable&) = delete;
};

template<typename T>
class AtomicIntegerT : noncopyable{
public:
    AtomicIntegerT()
        : value_(0) { }
    
    T get() {
        return __sync_val_compare_and_swap(&value_, 0, 0);
    }

    T getAndAdd(T x) {
        return __sync_fetch_and_add(&value_, x);
    }

    T addAndGet(T x) {
        return getAndAdd(x) + x;
    }

    T incrementAndGet() {
        return addAndGet(1);
    }

    T decrementAndGet() {
        return addAndGet(-1);
    }

    void add(T x) {
        getAndAdd(x);
    }

    void increment() {
        incrementAndGet();
    }

    void decrement() {
        decrementAndGet();
    }

    T getAndSet(T newValue) {
        return __sync_lock_test_and_set(&value_, newValue);
    }

private:
    volatile T value_;
};
typedef AtomicIntegerT<int32_t> AtomicInt32;

class MutexLock {
public:
    MutexLock() {
        pthread_mutex_init(&mutex_, NULL);
    }

    ~MutexLock() {
        pthread_mutex_destroy(&mutex_);
    }

    void lock() {
        pthread_mutex_lock(&mutex_);
    }

    void unlock() {
        pthread_mutex_unlock(&mutex_);
    }

    pthread_mutex_t* getPthreadMutex() {
        return &mutex_;
    }
private:
    friend class Condition;
    pthread_mutex_t mutex_;
};

class MutexLockGuard {
public:
    MutexLockGuard(MutexLock& mutex) : mutex_(mutex) {
        mutex_.lock();
    }
    ~MutexLockGuard() {
        mutex_.unlock();
    }
private:
    MutexLock& mutex_;
};

class Condition : noncopyable {
public:
    explicit Condition(MutexLock& mutex) : mutex_(mutex) {
        pthread_cond_init(&pcond_, NULL);
    }

    ~Condition() {
        pthread_cond_destroy(&pcond_);
    }

    void wait() {
        pthread_cond_wait(&pcond_, mutex_.getPthreadMutex());
    }

    void notify() {
        pthread_cond_signal(&pcond_);
    }

    void notifyAll() {
        pthread_cond_broadcast(&pcond_);
    }
private:
    MutexLock& mutex_;
    pthread_cond_t pcond_;
};

int g_cycles = 0;
int g_percent = 82;
AtomicInt32 g_done;
bool g_busy = false;
MutexLock g_mutex;
Condition g_cond(g_mutex);

double now() {
    struct timeval tv = {0, 0};
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

double busy(int cycles) {
    double result = 0;
    // 通过不断的浮点运算占用CPU的使用
    for (int i = 0; i < cycles; ++i) {
        result += sqrt(i) * sqrt(i+1);
    }
    return result;
}

double getSeconds(int cycles) {
    double start = now();
    busy(cycles);
    return now() - start;
}

void findCycles() {
    g_cycles = 1000;
    while (getSeconds(g_cycles) < 0.001) {
        g_cycles = g_cycles + g_cycles / 4;     // * 1.25
    }
    printf("cycles: %d; seconds: %lf\n", g_cycles, getSeconds(g_cycles));
}

void* threadFunc(void *argv) {
    while (g_done.get() == 0) {
        {
        MutexLockGuard guard(g_mutex);
        while (!g_busy)
            g_cond.wait();
        }
        busy(g_cycles);
    }
    printf("thread exit\n");
}

// this is open-loop control
void load(int percent) {
    percent = std::max(0, percent);
    percent = std::min(percent, 100);

    // https://blog.csdn.net/sinat_41104353/article/details/82858375
    // 画一条横坐标从0到100，纵坐标从0到percent的直线
    // Bresenham's line algorithm
    int err = 2*percent - 100;
    int count = 0;

    for (int i = 0; i < 100; ++i) {
        bool busy = false;
        if (err > 0) {
            busy = true;
            err += 2*(percent - 100);
            // printf("%d %d\n", i, count);
            ++count;
        } else {
            err += 2*percent;
        }

        {
        MutexLockGuard guard(g_mutex);
        g_busy = busy;
        g_cond.notifyAll();
        }

        usleep(10000);  // 10 ms
    }
    assert(count == percent);
}

void fixed() {
    while (true) {
        load(g_percent);
    }
}

void cosine() {
    while (true) {
        for (int i = 0; i < 10; ++i) {
            // 先加上1.0是把曲线平移到x轴以上，毕竟没有负数的CPU使用率
            // (i * 3.14159 / 100) 就是2*pai的一个周期
            // / 2 是因为它是[0,2]的，*g_percent转化为对应的使用率，+0.5四舍五入
            int percent = static_cast<int>((1.0 + cos(i * 3.14159 / 5)) / 2 * g_percent + 0.5);
            load(percent);
        }
    }
}

void sawtooth() {
    while (true) {
        for (int i = 0; i <= 30; ++i) {
            int percent = static_cast<int>(i / 30.0 * g_percent);
            load(percent);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s [fctsz] [percent] [num_threads]\n", argv[0]);
        return 0;
    }

    printf("pid %d\n", getpid());
    findCycles();

    g_percent = argc > 2 ? atoi(argv[2]) : 43;
    int numThreads = argc > 3 ? atoi(argv[3]) : 1;
    
    pthread_t threads[numThreads];
    for (int i = 0; i < numThreads; ++i) {
        if (pthread_create(&threads[i], NULL, threadFunc, NULL) != 0) {
            printf("thread creation failed\n");
            exit(1);
        }
    }

    switch (argv[1][0])
    {
    case 'f':
        fixed();
        break;

    case 'c':
        cosine();
        break;
    
    case 'z':
        sawtooth();
        break;
    default:
        break;
    }

    g_done.getAndSet(1);
    {
    MutexLockGuard guard(g_mutex);
    g_busy = true;
    g_cond.notifyAll();
    }
    for (int i = 0; i < numThreads; ++i) {
        pthread_join(threads[i], NULL);
    }
}