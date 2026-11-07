#include <imtg/commerce/payments.hpp>

int main()
{
    const auto money = imtg::commerce::Money::from_fiat_minor_units("RUB", 100);
    return money ? 0 : 1;
}
