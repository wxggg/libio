#include "lock.hh"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace wxg {

using Task = std::function<void()>;

class thread_pool {
   private:
    std::vector<std::unique_ptr<std::thread>> threads;
    std::vector<std::shared_ptr<std::atomic<bool>>> flags;  // true then close i

    lock_queue<Task *> taskQue;

    std::atomic<int> nWaiting;
    std::atomic<int> isStop;
    std::atomic<int> isDone;

    std::mutex mutex;
    std::condition_variable cv;

   public:
    thread_pool() : nWaiting(0), isStop(false), isDone(false) { resize(4); }
    thread_pool(int nThreads) : nWaiting(0), isStop(false), isDone(false) {
        resize(nThreads);
    }
    ~thread_pool() { stop(true); }

    int size() const { return threads.size(); }
    int idle_size() const { return nWaiting; }
    std::thread &get_thread(int i) const { return *threads[i]; }

    void clear_task_queue() {
        Task *t;
        while (taskQue.pop(t)) delete t;
    }

    void resize(int nThreads) {
        if (!isStop && !isDone) {
            int size = this->size();
            if (size <= nThreads) {
                threads.resize(nThreads);
                flags.resize(nThreads);

                for (int i = size; i < nThreads; i++) {
                    flags[i] = std::make_shared<std::atomic<bool>>(false);
                    set_thread(i);
                }
            } else {
                for (int i = nThreads; i < size; i++) {
                    *flags[i] = true;
                    threads[i]->detach();
                }

                {  // stop detached threads that were waiting
                    Lock lock(mutex);
                    cv.notify_all();
                }

                threads.resize(nThreads);
                flags.resize(nThreads);
            }
        }
    }

    template <typename F, typename... Args>
    decltype(auto) push(F &&f, Args &&... args) {
        auto tsk = std::make_shared<std::packaged_task<decltype(f(args...))()>>(
            [f, args...]() { f(args...); });

        taskQue.push(new Task([tsk]() { (*tsk)(); }));

        Lock lock(mutex);
        cv.notify_one();

        return tsk->get_future();
    }

    Task pop() {
        Task *t = nullptr;
        taskQue.pop(t);
        if (t) return *t;
        return nullptr;
    }

    void stop(bool isWait) {
        if (!isWait) {
            if (isStop) return;
            isStop = true;
            for (int i = 0; i < size(); i++) *flags[i] = true;
            clear_task_queue();
        } else {
            if (isDone || isStop) return;
            isDone = true;
        }

        {
            Lock lock(mutex);
            cv.notify_all();
        }

        for (int i = 0; i < size(); i++)
            if (threads[i]->joinable()) threads[i]->join();

        clear_task_queue();
        threads.clear();
        flags.clear();
    }

   private:
    void set_thread(int i) {
        threads[i].reset(new std::thread([this, i]() {
            Task *t;
            bool isPop = this->taskQue.pop(t);
            while (true) {
                while (isPop) {
                    std::unique_ptr<Task> t_(t);  // t will be deleted at return
                    (*t)();
                    if (*this->flags[i])
                        return;
                    else
                        isPop = this->taskQue.pop(t);
                }

                // here queue is empty
                Lock lock(this->mutex);
                ++this->nWaiting;
                this->cv.wait(lock, [this, &t, &isPop, i]() {
                    isPop = this->taskQue.pop(t);
                    return isPop || this->isDone || *this->flags[i];
                });
                --this->nWaiting;
                if (!isPop) return;  // isDone or flagi==true
            }
        }));
    }
};

}  // namespace wxg
