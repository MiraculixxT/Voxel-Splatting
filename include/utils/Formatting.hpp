#pragma once
#include <string>

namespace VSUtils {
    static std::string NumberFormatThousands(const std::size_t& number) {
        std::string numStr = std::to_string(number);
        int insertPosition = static_cast<int>(numStr.length()) - 3;
        while (insertPosition > 0) {
            numStr.insert(insertPosition, ".");
            insertPosition -= 3;
        }
        return numStr;
    }
}
