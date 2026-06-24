#include "uobject.h"
#include "uclass.h"
#include "../system/utility.hpp"

namespace SDK {
    TUObjectArray *UObject::GObjects = nullptr;  
    std::string UObject::GetName() const {
        std::string name(Name.GetName());
        if (Name.Number > 0)
            name += '_' + std::to_string(Name.Number);
        auto pos = name.rfind('/');
        if (pos == std::string::npos)
            return name;
        return name.substr(pos + 1);
    }

    std::string UObject::GetFullName() const {
        std::string name;
        if (Class != nullptr) {
            std::string temp;
            for (auto p = Outer; p; p = p->Outer) {
                temp = p->GetName() + "." + temp;
            }
            name = Class->GetName();
            name += " ";
            name += temp;
            name += GetName();
        }
        return name;
    }

    UClass *UObject::FindClass(const std::string &name) {
        return FindObject<UClass>(name);
    }

    bool UObject::IsA(UClass *cmp) const {
        for (auto super = Class; super; super = static_cast<UClass *>(super->SuperField)) {
            if (super == cmp)
                return true;
        }

        return false;
    }

    void UObject::ProcessEvent(class UFunction *function, void *parms) {
        GetVFunction<void (*)(UObject *, class UFunction *, void *)>(this, 0x44)(this, function, parms);
    }
}
