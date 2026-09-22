#include "LogPolicy.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {
    int failures = 0;

    void Check(bool condition, std::string_view message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    void TestValidation() {
        using EA::LogPolicy::ValidateMaxLogFiles;
        Check(ValidateMaxLogFiles(std::nullopt) == 10, "missing retention uses default");
        Check(ValidateMaxLogFiles(0) == 0, "zero means unlimited retention");
        Check(ValidateMaxLogFiles(1000) == 1000, "upper boundary is accepted");
        Check(ValidateMaxLogFiles(-1) == 10, "negative retention is rejected");
        Check(ValidateMaxLogFiles(1001) == 10, "excessive retention is rejected");
    }

    void TestSelection() {
        using namespace EA::LogPolicy;
        const std::vector<std::string> names{
            "SimpleAlternateLevelling_2026-07-15_10-00-03.log",
            "SimpleAlternateLevelling_2026-07-15_10-00-01.log",
            "unrelated.log",
            "SimpleAlternateLevelling_bad.log",
            "SimpleAlternateLevelling_2026-07-15_10-00-02.log"
        };

        Check(IsSessionLogName(names[0]), "valid timestamped filename accepted");
        Check(!IsSessionLogName(names[2]), "unrelated filename rejected");
        Check(!IsSessionLogName(names[3]), "malformed plugin filename rejected");

        const auto removed = SelectLogsToDelete(names, 2);
        Check(removed.size() == 1, "only excess matching logs selected");
        Check(!removed.empty() && removed[0] == "SimpleAlternateLevelling_2026-07-15_10-00-01.log",
            "oldest matching log selected first");
        Check(SelectLogsToDelete(names, 0).empty(), "zero retention limit deletes nothing");
        Check(SelectLogsToDelete(names, 3).empty(), "unrelated files do not affect retention");
    }
}

int main() {
    TestValidation();
    TestSelection();
    if (failures == 0) {
        std::cout << "All log-policy tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
