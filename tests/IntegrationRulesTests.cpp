#include "IntegrationRules.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
    int failures = 0;
    std::uint32_t lastCategory = 0xFFFFFFFFu;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    float Double(std::uint32_t category)
    {
        lastCategory = category;
        return 2.0f;
    }

    float Other(std::uint32_t) { return 3.0f; }

    float Throws(std::uint32_t) { throw std::runtime_error("provider failure"); }

    void TestXPMultiplierSlot()
    {
        using namespace EA::IntegrationRules;
        CallbackSlot<XPMultiplierProvider> slot;
        bool threw = true;
        Check(!slot.Has(), "slot starts empty");
        Check(QueryXPMultiplier(slot, 1, threw) == 1.0f && !threw, "no provider registered -> 1.0");

        Check(slot.Register(nullptr) == RegisterResult::kNull, "null registration rejected");
        Check(!slot.Has(), "null registration leaves slot empty");

        Check(slot.Register(Double) == RegisterResult::kAccepted, "first registration accepted");
        Check(slot.Register(Other) == RegisterResult::kDuplicate, "second registration rejected");
        Check(slot.Register(Double) == RegisterResult::kDuplicate, "repeat registration rejected");
        Check(slot.Get() == Double, "first registrant kept");

        Check(QueryXPMultiplier(slot, 4, threw) == 2.0f && !threw, "provider value returned");
        Check(lastCategory == 4, "source category passed through");
    }

    void TestThrowingProvider()
    {
        using namespace EA::IntegrationRules;
        CallbackSlot<XPMultiplierProvider> slot;
        bool threw = false;
        Check(slot.Register(Throws) == RegisterResult::kAccepted, "throwing provider registers");
        Check(std::isnan(QueryXPMultiplier(slot, 0, threw)) && threw, "throwing provider reports NaN");
    }
}

int main()
{
    TestXPMultiplierSlot();
    TestThrowingProvider();
    if (failures == 0) {
        std::cout << "All integration-rule tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
