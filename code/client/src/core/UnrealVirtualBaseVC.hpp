#ifndef RC_UNREAL_UNREAL_VIRTUAL_BASE_VC_HPP
#define RC_UNREAL_UNREAL_VIRTUAL_BASE_VC_HPP

#define PARAMS(...) __VA_ARGS__
#define ARGS(...)  __VA_ARGS__

#define IMPLEMENT_UNREAL_VIRTUAL_WRAPPER_NO_PARAMS(class_name, function_name, return_type) \
auto it = VTableLayoutMap.find(STR(#function_name)); \
if (it == VTableLayoutMap.end())                                                         \
{                                                                                              \
    throw std::runtime_error{"Virtual " #class_name "::" #function_name " is unavailable, possibly unsupported in engine version"}; \
}                                                                                           \
std::byte* vtable = std::bit_cast<std::byte*>(*std::bit_cast<std::byte**>(this)); \
auto func = std::bit_cast<return_type(class_name::*)() const>(*std::bit_cast<void**>(vtable + it->second));\
if (!func) \
{ \
throw std::runtime_error{"Function '" #function_name "' not available"}; \
}                                                                          \
return (this->*func)();

#define IMPLEMENT_UNREAL_VIRTUAL_WRAPPER(class_name, function_name, return_type, params, args) \
auto it = VTableLayoutMap.find(STR(#function_name)); \
if (it == VTableLayoutMap.end())                                                         \
{                                                                                              \
    throw std::runtime_error{"Virtual " #class_name "::" #function_name " is unavailable, possibly unsupported in engine version"}; \
}\
std::byte* vtable = std::bit_cast<std::byte*>(*std::bit_cast<std::byte**>(this)); \
auto func = std::bit_cast<return_type(class_name::*)(params) const>(*std::bit_cast<void**>(vtable + it->second)); \
if (!func) \
{ \
throw std::runtime_error{"Function '" #function_name "' not available"}; \
}                                                                          \
return (this->*func)(args);

#define GET_ADDRESS_OF_UNREAL_VIRTUAL(class_name, function_name, instance) \
[&instance]() -> void* {                                                   \
    auto it = class_name::VTableLayoutMap.find(STR(#function_name));                        \
    if (it == class_name::VTableLayoutMap.end())                                       \
    {                                                                      \
        throw std::runtime_error{"Virtual " #class_name "::" #function_name " is unavailable, possibly unsupported in engine version"}; \
    }\
    std::byte* vtable = std::bit_cast<std::byte*>(*std::bit_cast<std::byte**>(instance)); \
    return *std::bit_cast<void**>(vtable + it->second);\
}()

namespace RC::Unreal
{
    class UnrealVirtualBaseVC
    {
    public:
        virtual ~UnrealVirtualBaseVC() = default;

    public:
        virtual auto set_virtual_offsets() -> void = 0;
    };
}

#endif //RC_UNREAL_UNREAL_VIRTUAL_BASE_VC_HPP
