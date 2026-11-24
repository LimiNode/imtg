#include <imtg/actions.hpp>
#include "test_support.hpp"
#include <cstdlib>
#include <limits>
#include <new>

namespace { thread_local bool forbid_allocation = false; }
void* operator new(std::size_t size)
{
    if (forbid_allocation) throw std::bad_alloc();
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    using namespace std::chrono;
    using imtg::detail::checked_duration_cast;
    using TinyMillis = duration<std::int8_t, std::milli>;
    CHECK(checked_duration_cast<TinyMillis>(milliseconds{127})->count() == 127);
    CHECK(checked_duration_cast<TinyMillis>(milliseconds{-128})->count() == -128);
    CHECK(!checked_duration_cast<TinyMillis>(milliseconds{128}));
    CHECK(!checked_duration_cast<TinyMillis>(milliseconds{-129}));
    CHECK(checked_duration_cast<microseconds>(milliseconds{-123})->count() == -123000);
    CHECK(checked_duration_cast<seconds>(milliseconds{-1999})->count() == -1);
    CHECK(checked_duration_cast<milliseconds>(seconds{123})->count() == 123000);
    CHECK(!checked_duration_cast<milliseconds>((seconds::max)()));
    CHECK(!checked_duration_cast<milliseconds>((seconds::min)()));
    CHECK(checked_duration_cast<milliseconds>((nanoseconds::max)()));
    CHECK(checked_duration_cast<milliseconds>((nanoseconds::min)()));
    CHECK(checked_duration_cast<nanoseconds>(milliseconds{9223372036854LL}));
    CHECK(!checked_duration_cast<nanoseconds>(milliseconds{9223372036855LL}));
    CHECK(checked_duration_cast<nanoseconds>(milliseconds{-9223372036854LL}));
    CHECK(!checked_duration_cast<nanoseconds>(milliseconds{-9223372036855LL}));

    imtg::ManualClock clock((imtg::ClockTime::max)());
    bool rejected = false;
    try { clock.advance(seconds{1}); } catch (const std::overflow_error&) { rejected = true; }
    CHECK(rejected && clock.now() == (imtg::ClockTime::max)());
    clock.set((imtg::ClockTime::min)());
    rejected = false;
    try { clock.advance(seconds{-1}); } catch (const std::overflow_error&) { rejected = true; }
    CHECK(rejected && clock.now() == (imtg::ClockTime::min)());
    clock.set(imtg::ClockTime{});
    rejected = false;
    try { clock.advance((milliseconds::max)()); } catch (const std::overflow_error&) { rejected = true; }
    CHECK(rejected && clock.now() == imtg::ClockTime{});
    clock.advance(milliseconds{-1});
    CHECK(clock.now() == imtg::ClockTime{milliseconds{-1}});

    imtg::PlainTokenCodec codec;
    CHECK(!codec.decode("v1|4:a.v1|1|0:|9223372036854775807"));
    CHECK(!codec.decode("v1|4:a.v1|1|0:|-9223372036854775808"));
    CHECK(!codec.decode("v1|4:a.v1|1|0:|-2"));
    const auto absent = codec.decode("v1|4:a.v1|1|0:|-1");
    CHECK(absent && !absent.value().expires_at);
    const auto epoch = codec.decode("v1|4:a.v1|1|0:|0");
    CHECK(epoch && epoch.value().expires_at == imtg::ClockTime{});
    const auto millis = checked_duration_cast<milliseconds>((imtg::ClockTime::max)().time_since_epoch());
    CHECK(millis);
    const auto edge = codec.decode("v1|4:a.v1|1|0:|" + std::to_string(millis->count()));
    CHECK(edge && edge.value().expires_at);
    CHECK(!codec.decode("v1|4:a.v1|1|0:|" + std::to_string(millis->count() + 1)));
    CHECK(!codec.encode({imtg::ActionId{"a.v1"}, 1, {}, imtg::ClockTime{milliseconds{-1}}}));
    const auto bytes = codec.encode({imtg::ActionId{"a.v1"}, 1, {}, imtg::ClockTime{milliseconds{123}}});
    CHECK(bytes && bytes.value() == "v1|4:a.v1|1|0:|123");

    const imtg::ConversationKey ordered[] = {
        {imtg::BotId{2}, imtg::ChatId{-100}, imtg::Chat{}},
        {imtg::BotId{2}, imtg::ChatId{-10}, imtg::Chat{}},
        {imtg::BotId{2}, imtg::ChatId{2}, imtg::Chat{}},
        {imtg::BotId{2}, imtg::ChatId{2}, imtg::UserInChat{imtg::UserId{2}}},
        {imtg::BotId{2}, imtg::ChatId{2}, imtg::UserInChat{imtg::UserId{10}}},
        {imtg::BotId{2}, imtg::ChatId{10}, imtg::Chat{}},
        {imtg::BotId{10}, imtg::ChatId{-100}, imtg::Chat{}},
        {imtg::BotId{(std::numeric_limits<std::uint64_t>::max)()},
         imtg::ChatId{(std::numeric_limits<std::int64_t>::max)()},
         imtg::UserInChat{imtg::UserId{(std::numeric_limits<std::uint64_t>::max)()}}}
    };
    static_assert(noexcept(ordered[0] < ordered[1]), "key ordering must not throw");
    bool correct = true;
    forbid_allocation = true;
    for (std::size_t i = 0; i != 8; ++i) {
        for (std::size_t j = 0; j != 8; ++j) {
            correct = correct && ((ordered[i] < ordered[j]) == (i < j));
        }
    }
    forbid_allocation = false;
    CHECK(correct);
    CHECK(ordered[3].encode() == "v1|b=2|c=2|s=user:2");
}
