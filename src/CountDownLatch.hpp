#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>

class CountDownLatch
{
public:
    explicit CountDownLatch(int count) {
        assert(pthread_mutex_init(&mutex, NULL) == 0);
        assert(pthread_cond_init(&condition, NULL) == 0);
        this->count = count;
    }

    void wait() {
        //fprintf(stdout, "enter %ld %s\n", pthread_self(), __FUNCTION__);
        pthread_mutex_lock(&mutex);
        while (count > 0) {
            fprintf(stdout, "main(%ld) task wait~\n", pthread_self());
            pthread_cond_wait(&condition, &mutex);
            fprintf(stdout, "main(%ld) task wake up~\n", pthread_self());
        }
        pthread_mutex_unlock(&mutex);
        //fprintf(stdout, "leave %ld %s\n", pthread_self(), __FUNCTION__);
    }

    void countDown() {
        //fprintf(stdout, "enter %ld %s\n", pthread_self(), __FUNCTION__);
        pthread_mutex_lock(&mutex);
        fprintf(stdout, "task thread(%ld) done(%d)~\n", pthread_self(), count);
        if (!--count) {
            pthread_cond_signal(&condition);
        }
        pthread_mutex_unlock(&mutex);
        //fprintf(stdout, "leave %ld %s\n", pthread_self(), __FUNCTION__);
    }

    int getCount() const {
        return count;
    }

    ~CountDownLatch() {
        assert(pthread_mutex_destroy(&mutex) == 0);
        assert(pthread_cond_destroy(&condition) == 0);
    }

    //
    CountDownLatch() = delete;
    CountDownLatch(const CountDownLatch&) = delete;
    void operator=(const CountDownLatch&) = delete;

private:
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int count;
};
