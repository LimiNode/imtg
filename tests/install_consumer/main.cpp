#include <imtg/actions/actions.hpp>
#include <imtg/core/core.hpp>
#include <imtg/runtime/runtime.hpp>
#include <imtg/ui/ui.hpp>

int main()
{
    const imtg::BotId bot{1};
    return bot.value == 1 ? 0 : 1;
}
