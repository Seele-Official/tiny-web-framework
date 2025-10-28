#include <boost/ut.hpp>
#include "concurrent/mpsc_ringbuffer.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace {

using namespace boost::ut;
using namespace concurrent;

struct CounterObj {
    static std::atomic<int> ctor;
    static std::atomic<int> dtor;
    static std::atomic<int> move_ctor;
    static std::atomic<int> copy_ctor;
    int value;
    explicit CounterObj(int v) : value(v) { ++ctor; }
    CounterObj(const CounterObj& other) : value(other.value) { ++copy_ctor; }
    CounterObj(CounterObj&& other) noexcept : value(other.value) { ++move_ctor; }
    ~CounterObj() { ++dtor; }
};

std::atomic<int> CounterObj::ctor{0};
std::atomic<int> CounterObj::dtor{0};
std::atomic<int> CounterObj::move_ctor{0};
std::atomic<int> CounterObj::copy_ctor{0};

suite<"mpsc ringbuffer"> _ = [] {
    "empty pop"_test = [] {
        mpsc_ringbuffer<int, 8> q;
        expect(!q.pop_front().has_value());
    };

    "single producer fifo"_test = [] {
        mpsc_ringbuffer<int, 64> q;
        for (int i = 0; i < 32; ++i) {
            expect(q.emplace_back(i)) << "emplace should succeed";
        }
        for (int i = 0; i < 32; ++i) {
            auto value = q.pop_front();
            expect(value.has_value());
            expect(*value == i);
        }
        expect(!q.pop_front().has_value());
    };

    "capacity bound"_test = [] {
        mpsc_ringbuffer<int, 4> q;
        for (int i = 0; i < 4; ++i) {
            expect(q.emplace_back(i));
        }
        expect(!q.emplace_back(4));
        for (int i = 0; i < 4; ++i) {
            auto value = q.pop_front();
            expect(value.has_value());
            expect(*value == i);
        }
        expect(!q.pop_front().has_value());
    };

    "object lifetime"_test = [] {
        CounterObj::ctor.store(0);
        CounterObj::dtor.store(0);
        CounterObj::move_ctor.store(0);
        CounterObj::copy_ctor.store(0);
        {
            mpsc_ringbuffer<CounterObj, 16> q;
            for (int i = 0; i < 8; ++i) {
                expect(q.emplace_back(i));
            }
            CounterObj obj(42);
            expect(q.emplace_back(obj));
            expect(q.emplace_back(CounterObj(99)));
            int consumed = 0;
            while (auto value = q.pop_front()) {
                (void)value;
                ++consumed;
            }
            expect(consumed == 10);
        }
        expect(CounterObj::ctor.load() + CounterObj::copy_ctor.load() + CounterObj::move_ctor.load() == CounterObj::dtor.load());
    };

    struct Item {
        int producer;
        int seq;
    };

    "multi producer single consumer"_test = [] {
        constexpr int producers = 4;
        constexpr int per_producer = 2000;
        constexpr int total = producers * per_producer;
        mpsc_ringbuffer<Item, 4096> q;
        std::atomic<int> produced{0};
        std::vector<std::thread> threads;
        threads.reserve(producers);
        for (int p = 0; p < producers; ++p) {
            threads.emplace_back([p, &q, &produced] {
                for (int i = 0; i < per_producer; ++i) {
                    while (!q.emplace_back(Item{p, i})) {
                        std::this_thread::yield();
                    }
                    ++produced;
                }
            });
        }

        std::vector<bool> seen(total, false);
        int consumed = 0;
        while (consumed < total) {
            if (auto value = q.pop_front()) {
                const int id = value->producer * per_producer + value->seq;
                expect(id >= 0 && id < total);
                expect(!seen[id]) << "duplicate entry detected";
                seen[id] = true;
                ++consumed;
            } else {
                std::this_thread::yield();
            }
        }

        for (auto& th : threads) {
            th.join();
        }

        expect(produced.load() == total);
        expect(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
    };
};

} // namespace
