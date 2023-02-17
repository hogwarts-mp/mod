#pragma once

#include <string>

#include "tarray.h"

namespace SDK {
    class FString: public TArray<wchar_t> {
      public:
        FString();
        FString(const wchar_t *other);
        const wchar_t *wc_str() const;
        const char *c_str() const;
        bool IsValid() const;
        std::string ToString() const;
        std::wstring ToStringW() const;
    };

    struct FStringAssetReference {
        FString AssetLongPathname;
    };
}
