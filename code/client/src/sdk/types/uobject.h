#pragma once

#include <vector>
#include <string>

#include "../containers/fname.h"
#include "../containers/tuobjectarray.h"

namespace SDK {
    class UClass;
    class UObject {
      public:
        static TUObjectArray *GObjects;
        void *VTable;
        int32_t Flags;
        int32_t InternalIndex;
        UClass *Class;
        FName Name;
        UObject *Outer;

      public:
        static TUObjectArray &UObject::GetGlobalObjects() {
            return *GObjects;
        }
        std::string GetName() const;
        std::string GetFullName() const;
        template <typename T>
        static T *FindObject(const std::string &name) {
            for (int32_t i = 0; i < UObject::GetGlobalObjects().Count(); ++i) {
                auto object = UObject::GetGlobalObjects().GetByIndex(i);

                if (!object)
                    continue;

                if (object->GetFullName() == name)
                    return static_cast<T *>(object);
            }
            return nullptr;
        }

        template <typename T>
        static T *FindObject() {
            auto v = T::StaticClass();
            for (int32_t i = 0; i < UObject::GetGlobalObjects().Count(); ++i) {
                auto object = UObject::GetGlobalObjects().GetByIndex(i);

                if (!object)
                    continue;

                if (object->IsA(v))
                    return static_cast<T *>(object);
            }
            return nullptr;
        }

        template <typename T>
        static std::vector<T *> FindObjects(const std::string &name) {
            std::vector<T *> ret;
            for (int32_t i = 0; i < UObject::GetGlobalObjects().Count(); ++i) {
                auto object = UObject::GetGlobalObjects().GetByIndex(i);

                if (!object)
                    continue;

                if (object->GetFullName() == name)
                    ret.push_back(static_cast<T *>(object));
            }
            return ret;
        }

        template <typename T>
        static std::vector<T *> FindObjects() {
            std::vector<T *> ret;
            auto v = T::StaticClass();
            for (int i = 0; i < UObject::GetGlobalObjects().Count(); ++i) {
                auto object = UObject::GetGlobalObjects().GetByIndex(i);

                if (!object)
                    continue;

                if (object->IsA(v))
                    ret.push_back(static_cast<T *>(object));
            }
            return ret;
        }

        static UClass *FindClass(const std::string &name);
        template <typename T>
        static T *GetObjectCasted(size_t index) {
            return static_cast<T *>(UObject::GetGlobalObjects().GetByIndex(index));
        }

        bool IsA(UClass *cmp) const;
        void ExecuteUbergraph(int32_t EntryPoint);
        void ProcessEvent(class UFunction *function, void *parms);
        static UClass *StaticClass();
    };
}
