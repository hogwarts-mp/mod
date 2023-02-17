#pragma once

namespace SDK {
    template <typename Fn>
    Fn GetVFunction(const void *instance, size_t index) {
        auto vtable = *static_cast<const void ***>(const_cast<void *>(instance));
        return reinterpret_cast<Fn>(const_cast<void(*)>(vtable[index]));
    }
}
