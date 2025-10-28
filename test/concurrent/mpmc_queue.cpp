#include <boost/ut.hpp>
#include "concurrent/mpmc_queue.h"
#include <atomic>
#include <thread>
#include <vector>
namespace {

using namespace boost::ut;
using namespace concurrent;

// A helper class to track object lifetimes
struct CounterObj {
    static std::atomic<int> ctor;
    static std::atomic<int> dtor;
    static std::atomic<int> move_ctor;
    static std::atomic<int> copy_ctor;
    int value;
    CounterObj(int v): value(v) { ++ctor; }
    CounterObj(const CounterObj& o): value(o.value) { ++copy_ctor; }
    CounterObj(CounterObj&& o) noexcept: value(o.value) { ++move_ctor; }
    ~CounterObj(){ ++dtor; }
};
std::atomic<int> CounterObj::ctor{0};
std::atomic<int> CounterObj::dtor{0};
std::atomic<int> CounterObj::move_ctor{0};
std::atomic<int> CounterObj::copy_ctor{0};

suite<"chunk optimized michael & scott's queue"> _ = [] {
    "empty pop"_test = [] {
        mpmc_queue<int, 4> q;
        expect(!q.pop_front().has_value());
    };

    "single thread fifo"_test = [] {
        mpmc_queue<int, 4> q;
        for (int i = 0; i < 10; ++i) q.push_back(i);
        for (int i = 0; i < 10; ++i) {
            auto v = q.pop_front();
            expect(v.has_value());
            expect(*v == i);
        }
        expect(!q.pop_front().has_value());
    };

    "multi chunk growth"_test = [] {
        mpmc_queue<int, 4> q;
        for (int i = 0; i < 20; ++i) q.emplace_back(i);
        for (int i = 0; i < 20; ++i) {
            auto v = q.pop_front();
            expect(v.has_value());
            expect(*v == i);
        }
        expect(!q.pop_front().has_value());
    };

    "object lifetime correctness"_test = [] {
        CounterObj::ctor.store(0);
        CounterObj::dtor.store(0);
        CounterObj::move_ctor.store(0);
        CounterObj::copy_ctor.store(0);
        {
            mpmc_queue<CounterObj, 4> q;
            for (int i = 0; i < 10; ++i) q.emplace_back(i);
            CounterObj c(42);
            q.push_back(c);
            q.push_back(CounterObj(99));
            int count = 0;
            while (q.pop_front()) ++count;
            expect(count == 12);
        }
        expect(CounterObj::ctor.load() + CounterObj::copy_ctor.load() + CounterObj::move_ctor.load() == CounterObj::dtor.load());
    };

    struct Item { int producer; int seq; };

    "mpmc correctness"_test = [] {
        mpmc_queue<Item, 64> q;
        constexpr int producers = 4;
        constexpr int consumers = 4;
        constexpr int per_prod = 2000;
        const int total = producers * per_prod;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};
        std::vector<std::atomic<bool>> seen(total);
        for (auto& b: seen) b.store(false);
        std::atomic<bool> duplicate_ok{true};
        std::vector<std::thread> threads;
        for (int p=0;p<producers;++p){
            threads.emplace_back([p,&q,&produced]{
                for (int i=0;i<per_prod;++i){
                    q.emplace_back(Item{p,i});
                    ++produced;
                }
            });
        }
        for (int c=0;c<consumers;++c){
            threads.emplace_back([&]{
                while (true){
                    if (consumed.load(std::memory_order_relaxed) >= total) break;
                    if (auto v = q.pop_front()){
                        int id = v->producer * per_prod + v->seq;
                        bool expected_false = false;
                        if (!seen[id].compare_exchange_strong(expected_false, true)) duplicate_ok.store(false, std::memory_order_relaxed);
                        ++consumed;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (auto& th: threads) th.join();
        expect(produced.load() == total);
        expect(consumed.load() == total);
        expect(duplicate_ok.load());
        bool all_seen = true; for (int i=0;i<total;++i) if (!seen[i].load()) { all_seen = false; break; }
        expect(all_seen);
    };

    "stress mpmc"_test = [] {
        mpmc_queue<int, 64> q;
        constexpr int producers = 8;
        constexpr int consumers = 8;
        constexpr int per_prod = 1'000'000;
        const int total = producers * per_prod;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};
        std::vector<std::thread> threads;
        for (int p=0;p<producers;++p){
            threads.emplace_back([p,&q,&produced]{
                for (int i=0;i<per_prod;++i){
                    q.emplace_back(p*per_prod + i);
                    ++produced;
                }
            });
        }
        for (int c=0;c<consumers;++c){
            threads.emplace_back([&]{
                while (true){
                    if (consumed.load() >= total) break;
                    if (q.pop_front()) ++consumed; else std::this_thread::yield();
                }
            });
        }
        for (auto& th: threads) th.join();
        expect(produced.load() == total);
        expect(consumed.load() == total);
    };
};

}