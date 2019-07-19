#pragma once

#include <sys/time.h>

#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace wxg {

using Callback = std::function<void()>;

struct timer {
    int id;
    struct timeval tv;
    struct timeval timeout;
    bool persistent;
    Callback callback;
};
using Compare = std::function<bool(timer *, timer *)>;

class time {
   public:
    std::set<timer *, Compare> timers =
        std::set<timer *, Compare>([](timer *t1, timer *t2) {
            return timercmp(&t1->timeout, &t2->timeout, <);
        });
    std::map<int, timer *> idtimer;
    int __maxid = 0;

    time() {}

   public:
    bool empty() const { return timers.empty(); }
    int size() const { return timers.size(); }

    int shortest_time() {
        process();
        if (empty()) return -1;

        struct timeval now;
        gettimeofday(&now, nullptr);

        auto ti = *timers.begin();

        int timeout = (ti->timeout.tv_sec - now.tv_sec) + 1;
        return timeout > 0 ? timeout : 0;
    }

    static std::string get_date() {
        char date[50];
        struct tm cur, *cur_p;
        time_t t = std::time(nullptr);
        gmtime_r(&t, &cur);
        cur_p = &cur;
        if (strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", cur_p) !=
            0)
            return std::string(date);
        return "";
    }

    void set_persistent(int id) {
        if (!idtimer.count(id)) return;
        idtimer[id]->persistent = true;
    }

    template <typename F, typename... Args>
    int set_timer(int sec, F &&f, Args &&... args) {
        return set_timer(sec, 0, false, f, args...);
    }

    template <typename F, typename... Args>
    int set_timer(int sec, int usec, F &&f, Args &&... args) {
        return set_timer(sec, usec, false, f, args...);
    }

    template <typename F, typename... Args>
    int set_timer(int sec, int usec, bool persistent, F &&f, Args &&... args) {
        int id = __get_id();
        auto ti = new timer;
        idtimer[id] = ti;

        timerclear(&ti->tv);
        ti->tv.tv_sec = sec;
        ti->tv.tv_usec = usec;

        struct timeval now;
        gettimeofday(&now, nullptr);

        timeradd(&now, &ti->tv, &ti->timeout);

        ti->persistent = persistent;
        ti->callback = [f, args...]() { f(args...); };

        timers.insert(ti);

        return id;
    }

    void remove(int id) {
        if (!idtimer.count(id)) return;
        auto ti = idtimer[id];
        timers.erase(ti);
        idtimer.erase(id);
        delete ti;
    }

    void process() {
        if (empty()) return;
        struct timeval now;
        gettimeofday(&now, nullptr);

        auto i = timers.begin();
        while (i != timers.end()) {
            auto ti = *i;
            if (timercmp(&ti->timeout, &now, >)) break;
            i = timers.erase(i);

            ti->callback();

            if (!ti->persistent) {
                idtimer.erase(ti->id);
                delete ti;
            } else {
                timeradd(&now, &ti->tv, &ti->timeout);
                timers.insert(ti);
            }
        }
    }

   private:
    int __get_id() {
        if (idtimer.empty()) return 0;
        if ((*idtimer.begin()).first > 0) return (*idtimer.begin()).first - 1;
        return ++__maxid;
    }
};

}  // namespace wxg
