#include "compat/a32_jni.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace liba32android::compat {
namespace {

constexpr std::size_t kJavaVmBytes = 4U;
constexpr std::size_t kInvokeTableWords = 8U;
constexpr std::size_t kInvokeTableBytes = kInvokeTableWords * 4U;
constexpr std::size_t kAttachCurrentThreadSlot = 4U;
constexpr std::size_t kDetachCurrentThreadSlot = 5U;
constexpr std::size_t kGetEnvSlot = 6U;
constexpr std::size_t kJniEnvBytes = 4U;
constexpr std::size_t kNativeTableWords = 216U;
constexpr std::size_t kNativeTableBytes = kNativeTableWords * 4U;
constexpr std::size_t kFindClassSlot = 6U;
constexpr std::size_t kThrowNewSlot = 14U;
constexpr std::size_t kExceptionOccurredSlot = 15U;
constexpr std::size_t kExceptionClearSlot = 17U;
constexpr std::size_t kNewGlobalRefSlot = 21U;
constexpr std::size_t kDeleteGlobalRefSlot = 22U;
constexpr std::size_t kDeleteLocalRefSlot = 23U;
constexpr std::size_t kNewObjectVSlot = 29U;
constexpr std::size_t kGetMethodIdSlot = 33U;
constexpr std::size_t kCallVoidMethodSlot = 61U;
constexpr std::size_t kCallVoidMethodVSlot = 62U;
constexpr std::size_t kGetFieldIdSlot = 94U;
constexpr std::size_t kGetIntFieldSlot = 100U;
constexpr std::size_t kGetLongFieldSlot = 101U;
constexpr std::size_t kGetStaticMethodIdSlot = 113U;
constexpr std::size_t kSetLongFieldSlot = 110U;
constexpr std::size_t kGetStaticFieldIdSlot = 144U;
constexpr std::size_t kGetStaticIntFieldSlot = 150U;
constexpr std::size_t kNewStringUtfSlot = 167U;
constexpr std::size_t kGetStringUtfCharsSlot = 169U;
constexpr std::size_t kReleaseStringUtfCharsSlot = 170U;
constexpr std::size_t kGetArrayLengthSlot = 171U;
constexpr std::size_t kNewObjectArraySlot = 172U;
constexpr std::size_t kGetObjectArrayElementSlot = 173U;
constexpr std::size_t kSetObjectArrayElementSlot = 174U;
constexpr std::size_t kNewLongArraySlot = 180U;
constexpr std::size_t kGetByteArrayElementsSlot = 184U;
constexpr std::size_t kGetLongArrayElementsSlot = 188U;
constexpr std::size_t kReleaseByteArrayElementsSlot = 192U;
constexpr std::size_t kReleaseLongArrayElementsSlot = 196U;
constexpr std::size_t kSetLongArrayRegionSlot = 212U;
constexpr std::size_t kRegisterNativesSlot = 215U;
constexpr std::size_t kJniNativeMethodBytes = 12U;
constexpr std::size_t kServiceStubBytes = 8U;
constexpr std::size_t kMaxInstallRegionBytes = kNativeTableBytes;

struct AddressRange {
    std::uint32_t address{};
    std::size_t size{};
};

[[nodiscard]] bool valid_range(
    const AddressRange& range) noexcept {
    if (range.address == 0U ||
        (range.address & 3U) != 0U ||
        range.size == 0U) {
        return false;
    }
    return static_cast<std::uint64_t>(range.address) +
               static_cast<std::uint64_t>(range.size) <=
           (std::uint64_t{1} << 32U);
}

[[nodiscard]] bool overlaps(
    const AddressRange& left,
    const AddressRange& right) noexcept {
    const std::uint64_t left_begin = left.address;
    const std::uint64_t left_end = left_begin + left.size;
    const std::uint64_t right_begin = right.address;
    const std::uint64_t right_end = right_begin + right.size;
    return left_begin < right_end && right_begin < left_end;
}

void write_u32(
    std::span<std::uint8_t> output,
    std::size_t offset,
    std::uint32_t value) noexcept {
    output[offset + 0U] = static_cast<std::uint8_t>(value);
    output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    output[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

[[nodiscard]] std::array<std::uint8_t, 4> u32_bytes(
    std::uint32_t value) noexcept {
    return {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
}

[[nodiscard]] std::uint32_t jint_bits(
    std::int32_t value) noexcept {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] bool read_guest_u32(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    if (static_cast<std::uint64_t>(address) + 4U >
        (std::uint64_t{1} << 32U)) {
        return false;
    }
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

[[nodiscard]] bool read_guest_i64_values(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::size_t count,
    std::vector<std::int64_t>& output) {
    output.clear();
    if (count > kA32JniHardMaxLongArrayElements) {
        return false;
    }
    if (count == 0U) {
        return true;
    }
    if (address == 0U ||
        count > std::numeric_limits<std::size_t>::max() / 8U) {
        return false;
    }
    const std::size_t byte_count = count * 8U;
    if (static_cast<std::uint64_t>(address) +
            static_cast<std::uint64_t>(byte_count) >
        (std::uint64_t{1} << 32U)) {
        return false;
    }

    std::vector<std::uint8_t> bytes(byte_count);
    if (!memory.read(address, bytes)) {
        return false;
    }
    output.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::size_t offset = index * 8U;
        std::uint64_t bits{};
        for (std::size_t byte = 0U; byte < 8U; ++byte) {
            bits |= static_cast<std::uint64_t>(
                        bytes[offset + byte])
                    << (byte * 8U);
        }
        output.push_back(static_cast<std::int64_t>(bits));
    }
    return true;
}

[[nodiscard]] bool write_guest_i64_values(
    memory::GuestMemory& memory,
    std::uint32_t address,
    const std::vector<std::int64_t>& values) {
    if (values.size() > kA32JniHardMaxLongArrayElements) {
        return false;
    }
    if (values.empty()) {
        return true;
    }
    if (address == 0U ||
        values.size() >
            std::numeric_limits<std::size_t>::max() / 8U) {
        return false;
    }
    const std::size_t byte_count = values.size() * 8U;
    if (static_cast<std::uint64_t>(address) +
            static_cast<std::uint64_t>(byte_count) >
        (std::uint64_t{1} << 32U)) {
        return false;
    }

    std::vector<std::uint8_t> bytes(byte_count, 0U);
    for (std::size_t index = 0U;
         index < values.size();
         ++index) {
        const std::uint64_t bits =
            static_cast<std::uint64_t>(values[index]);
        const std::size_t offset = index * 8U;
        for (std::size_t byte = 0U; byte < 8U; ++byte) {
            bytes[offset + byte] =
                static_cast<std::uint8_t>(
                    bits >> (byte * 8U));
        }
    }
    return memory.write(address, bytes);
}

[[nodiscard]] bool read_guest_c_string(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::size_t max_payload_bytes,
    std::string& output) {
    output.clear();
    if (address == 0U ||
        max_payload_bytes == 0U ||
        max_payload_bytes > kA32JniHardMaxStringBytes) {
        return false;
    }

    output.reserve(std::min<std::size_t>(max_payload_bytes, 64U));
    for (std::size_t offset = 0U;
         offset <= max_payload_bytes;
         ++offset) {
        const std::uint64_t guest =
            static_cast<std::uint64_t>(address) + offset;
        if (guest > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        std::uint8_t byte{};
        if (!memory.read(
                static_cast<std::uint32_t>(guest),
                std::span<std::uint8_t>{&byte, 1U})) {
            return false;
        }
        if (byte == 0U) {
            return true;
        }
        if (offset == max_payload_bytes) {
            return false;
        }
        output.push_back(static_cast<char>(byte));
    }
    return false;
}

void write_service_stub(
    std::span<std::uint8_t> output,
    std::uint32_t svc_immediate) noexcept {
    output[0] = static_cast<std::uint8_t>(svc_immediate);
    output[1] = static_cast<std::uint8_t>(svc_immediate >> 8U);
    output[2] = static_cast<std::uint8_t>(svc_immediate >> 16U);
    output[3] = 0xEFU;
    output[4] = 0x1EU;
    output[5] = 0xFFU;
    output[6] = 0x2FU;
    output[7] = 0xE1U;
}

[[nodiscard]] bool is_zero_argument_signature(
    std::string_view signature) noexcept {
    return signature.size() >= 3U &&
           signature[0] == '(' &&
           signature[1] == ')';
}

struct InstallRegion {
    std::uint32_t address{};
    std::size_t size{};
    std::array<std::uint8_t, kMaxInstallRegionBytes> desired{};
    std::array<std::uint8_t, kMaxInstallRegionBytes> original{};
};

class JniExecutionContextScope final {
public:
    JniExecutionContextScope(
        elf::Elf32LifecycleExecutionContext* context,
        std::size_t object_index) noexcept
        : context_(context) {
        if (context_ != nullptr) {
            previous_ = context_->object_index;
            context_->object_index = object_index;
        }
    }

    ~JniExecutionContextScope() {
        if (context_ != nullptr) {
            context_->object_index = previous_;
        }
    }

    JniExecutionContextScope(
        const JniExecutionContextScope&) = delete;
    JniExecutionContextScope& operator=(
        const JniExecutionContextScope&) = delete;

private:
    elf::Elf32LifecycleExecutionContext* context_{};
    std::optional<std::size_t> previous_;
};

[[nodiscard]] A32JniOnLoadResult onload_failure(
    A32JniOnLoadError error) {
    A32JniOnLoadResult result;
    result.error = error;
    return result;
}

template <typename ReadWord, typename ReadDoubleWord>
[[nodiscard]] bool decode_a32_jni_arguments(
    std::string_view signature,
    std::size_t max_arguments,
    ReadWord& read_word,
    ReadDoubleWord& read_double_word,
    std::vector<A32JniValue>& output) {
    output.clear();
    if (signature.size() < 3U ||
        signature.front() != '(' ||
        max_arguments == 0U ||
        max_arguments > kA32JniHardMaxMethodArguments) {
        return false;
    }

    std::size_t descriptor = 1U;
    while (descriptor < signature.size() &&
           signature[descriptor] != ')') {
        if (output.size() >= max_arguments) {
            return false;
        }

        A32JniValue value;
        const char type = signature[descriptor++];
        if (type == 'L' || type == '[') {
            if (type == 'L') {
                const std::size_t name_begin = descriptor;
                while (descriptor < signature.size() &&
                       signature[descriptor] != ';') {
                    ++descriptor;
                }
                if (descriptor == name_begin ||
                    descriptor >= signature.size()) {
                    return false;
                }
                ++descriptor;
            } else {
                while (descriptor < signature.size() &&
                       signature[descriptor] == '[') {
                    ++descriptor;
                }
                if (descriptor >= signature.size()) {
                    return false;
                }
                if (signature[descriptor] == 'L') {
                    ++descriptor;
                    const std::size_t name_begin = descriptor;
                    while (descriptor < signature.size() &&
                           signature[descriptor] != ';') {
                        ++descriptor;
                    }
                    if (descriptor == name_begin ||
                        descriptor >= signature.size()) {
                        return false;
                    }
                    ++descriptor;
                } else {
                    const char component = signature[descriptor++];
                    if (component != 'Z' && component != 'B' &&
                        component != 'C' && component != 'S' &&
                        component != 'I' && component != 'J' &&
                        component != 'F' && component != 'D') {
                        return false;
                    }
                }
            }
            std::uint32_t reference{};
            if (!read_word(reference)) {
                return false;
            }
            value.kind = A32JniValueKind::Reference;
            value.bits = reference;
        } else if (
            type == 'Z' || type == 'B' || type == 'C' ||
            type == 'S' || type == 'I') {
            std::uint32_t word{};
            if (!read_word(word)) {
                return false;
            }
            switch (type) {
                case 'Z':
                    value.kind = A32JniValueKind::Boolean;
                    break;
                case 'B':
                    value.kind = A32JniValueKind::Byte;
                    break;
                case 'C':
                    value.kind = A32JniValueKind::Char;
                    break;
                case 'S':
                    value.kind = A32JniValueKind::Short;
                    break;
                default:
                    value.kind = A32JniValueKind::Int;
                    break;
            }
            value.bits = word;
        } else if (type == 'J' || type == 'D' || type == 'F') {
            std::uint64_t wide{};
            if (!read_double_word(wide)) {
                return false;
            }
            if (type == 'J') {
                value.kind = A32JniValueKind::Long;
                value.bits = wide;
            } else if (type == 'D') {
                value.kind = A32JniValueKind::Double;
                value.bits = wide;
            } else {
                static_assert(sizeof(double) == sizeof(std::uint64_t));
                static_assert(sizeof(float) == sizeof(std::uint32_t));
                double promoted{};
                std::memcpy(&promoted, &wide, sizeof(promoted));
                const float narrowed = static_cast<float>(promoted);
                std::uint32_t float_bits{};
                std::memcpy(
                    &float_bits,
                    &narrowed,
                    sizeof(float_bits));
                value.kind = A32JniValueKind::Float;
                value.bits = float_bits;
            }
        } else {
            return false;
        }
        output.push_back(value);
    }

    if (descriptor >= signature.size() ||
        signature[descriptor] != ')') {
        return false;
    }
    ++descriptor;
    return descriptor + 1U == signature.size() &&
           signature[descriptor] == 'V';
}

[[nodiscard]] bool decode_a32_jni_va_arguments(
    const memory::GuestMemory& memory,
    std::string_view signature,
    std::uint32_t va_list_address,
    std::size_t max_arguments,
    std::vector<A32JniValue>& output) {
    std::uint64_t cursor = va_list_address;
    const auto read_word = [&](std::uint32_t& value) {
        if (cursor >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) - 3U) {
            return false;
        }
        if (!read_guest_u32(
                memory,
                static_cast<std::uint32_t>(cursor),
                value)) {
            return false;
        }
        cursor += 4U;
        return true;
    };
    const auto read_double_word = [&](std::uint64_t& value) {
        cursor = (cursor + 7U) & ~std::uint64_t{7U};
        if (cursor >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) - 7U) {
            return false;
        }
        std::uint32_t low{};
        std::uint32_t high{};
        if (!read_guest_u32(
                memory,
                static_cast<std::uint32_t>(cursor),
                low) ||
            !read_guest_u32(
                memory,
                static_cast<std::uint32_t>(cursor + 4U),
                high)) {
            return false;
        }
        cursor += 8U;
        value = static_cast<std::uint64_t>(low) |
                (static_cast<std::uint64_t>(high) << 32U);
        return true;
    };
    return decode_a32_jni_arguments(
        signature,
        max_arguments,
        read_word,
        read_double_word,
        output);
}

[[nodiscard]] bool decode_a32_jni_raw_arguments(
    const memory::GuestMemory& memory,
    std::string_view signature,
    std::uint32_t first_variadic_word,
    std::uint32_t stack_address,
    std::size_t max_arguments,
    std::vector<A32JniValue>& output) {
    bool core_word_available = true;
    std::uint64_t cursor = stack_address;
    const auto read_word = [&](std::uint32_t& value) {
        if (core_word_available) {
            value = first_variadic_word;
            core_word_available = false;
            return true;
        }
        if (cursor >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) - 3U) {
            return false;
        }
        if (!read_guest_u32(
                memory,
                static_cast<std::uint32_t>(cursor),
                value)) {
            return false;
        }
        cursor += 4U;
        return true;
    };
    const auto read_double_word = [&](std::uint64_t& value) {
        // r3 is an odd core register. AAPCS32 cannot split an aligned
        // double-word variadic value across r3 and the stack.
        core_word_available = false;
        cursor = (cursor + 7U) & ~std::uint64_t{7U};
        if (cursor >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) - 7U) {
            return false;
        }
        std::uint32_t low{};
        std::uint32_t high{};
        if (!read_guest_u32(
                memory,
                static_cast<std::uint32_t>(cursor),
                low) ||
            !read_guest_u32(
                memory,
                static_cast<std::uint32_t>(cursor + 4U),
                high)) {
            return false;
        }
        cursor += 8U;
        value = static_cast<std::uint64_t>(low) |
                (static_cast<std::uint64_t>(high) << 32U);
        return true;
    };
    return decode_a32_jni_arguments(
        signature,
        max_arguments,
        read_word,
        read_double_word,
        output);
}

[[nodiscard]] A32JniNativeInvokeResult native_invoke_failure(
    A32JniNativeInvokeError error) {
    A32JniNativeInvokeResult result;
    result.error = error;
    return result;
}

}  // namespace

bool A32JniClassRegistry::valid() const noexcept {
    return limits_.max_classes > 0U &&
           limits_.max_classes <= kA32JniHardMaxClasses &&
           limits_.max_registered_methods > 0U &&
           limits_.max_registered_methods <=
               kA32JniHardMaxRegisteredMethods &&
           limits_.max_methods_per_registration > 0U &&
           limits_.max_methods_per_registration <=
               kA32JniHardMaxMethodsPerRegistration &&
           limits_.max_methods_per_registration <=
               limits_.max_registered_methods &&
           limits_.max_member_ids > 0U &&
           limits_.max_member_ids <= kA32JniHardMaxMemberIds &&
           limits_.max_reference_handles > 0U &&
           limits_.max_reference_handles <=
                kA32JniHardMaxReferenceHandles &&
           limits_.max_reference_count_per_handle > 0U &&
           limits_.max_reference_count_per_handle <=
                kA32JniHardMaxReferenceCountPerHandle &&
           limits_.max_arrays > 0U &&
           limits_.max_arrays <= kA32JniHardMaxArrays &&
           limits_.max_long_array_elements > 0U &&
           limits_.max_long_array_elements <=
                kA32JniHardMaxLongArrayElements &&
           limits_.max_byte_array_elements > 0U &&
           limits_.max_byte_array_elements <=
                kA32JniHardMaxByteArrayElements &&
           limits_.max_object_array_elements > 0U &&
           limits_.max_object_array_elements <=
                kA32JniHardMaxObjectArrayElements &&
           limits_.dynamic_array_handle_base != 0U &&
           limits_.dynamic_array_handle_stride != 0U &&
           static_cast<std::uint64_t>(
               limits_.dynamic_array_handle_base) +
                   static_cast<std::uint64_t>(
                       limits_.max_arrays +
                       limits_.max_reference_handles - 1U) *
                       limits_.dynamic_array_handle_stride <=
               std::numeric_limits<std::uint32_t>::max() &&
           limits_.max_strings > 0U &&
           limits_.max_strings <= kA32JniHardMaxStrings &&
           limits_.max_modified_utf8_bytes > 0U &&
           limits_.max_modified_utf8_bytes <= kA32JniHardMaxStringBytes &&
           limits_.max_exception_message_bytes > 0U &&
           limits_.max_exception_message_bytes <=
                kA32JniHardMaxStringBytes &&
           limits_.dynamic_string_handle_base != 0U &&
           limits_.dynamic_string_handle_stride != 0U &&
           static_cast<std::uint64_t>(
               limits_.dynamic_string_handle_base) +
                   static_cast<std::uint64_t>(
                       limits_.max_strings +
                       limits_.max_reference_handles - 1U) *
                       limits_.dynamic_string_handle_stride <=
               std::numeric_limits<std::uint32_t>::max() &&
           limits_.dynamic_exception_handle_base != 0U &&
           limits_.dynamic_exception_handle_stride != 0U &&
           static_cast<std::uint64_t>(
               limits_.dynamic_exception_handle_base) +
                   static_cast<std::uint64_t>(
                       limits_.max_reference_handles - 1U) *
                       limits_.dynamic_exception_handle_stride <=
               std::numeric_limits<std::uint32_t>::max() &&
           limits_.max_class_name_bytes > 0U &&
           limits_.max_class_name_bytes <=
               kA32JniHardMaxStringBytes &&
           limits_.max_method_name_bytes > 0U &&
           limits_.max_method_name_bytes <=
               kA32JniHardMaxStringBytes &&
           limits_.max_signature_bytes > 0U &&
           limits_.max_signature_bytes <=
               kA32JniHardMaxStringBytes &&
           limits_.max_method_arguments > 0U &&
           limits_.max_method_arguments <=
               kA32JniHardMaxMethodArguments;
}

A32JniRegistryError A32JniClassRegistry::add_class(
    std::uint32_t handle,
    std::string_view name) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (handle == 0U) {
        return A32JniRegistryError::InvalidClassHandle;
    }
    if (name.empty() ||
        name.size() > limits_.max_class_name_bytes) {
        return A32JniRegistryError::InvalidName;
    }
    if (classes_.size() >= limits_.max_classes) {
        return A32JniRegistryError::ClassLimitExceeded;
    }
    for (const ClassEntry& entry : classes_) {
        if (entry.handle == handle) {
            return A32JniRegistryError::DuplicateClassHandle;
        }
        if (entry.name == name) {
            return A32JniRegistryError::DuplicateClassName;
        }
    }

    if (find_reference_entry(handle) == nullptr) {
        if (references_.size() >= limits_.max_reference_handles) {
            return A32JniRegistryError::ReferenceLimitExceeded;
        }
        references_.push_back(ReferenceEntry{.handle = handle});
    }

    ClassEntry entry;
    entry.handle = handle;
    entry.name.assign(name);
    classes_.push_back(std::move(entry));
    return A32JniRegistryError::None;
}

std::optional<std::uint32_t> A32JniClassRegistry::find_class(
    std::string_view name) const noexcept {
    for (const ClassEntry& entry : classes_) {
        if (entry.name == name) {
            return entry.handle;
        }
    }
    return std::nullopt;
}

bool A32JniClassRegistry::contains_class_handle(
    std::uint32_t handle) const noexcept {
    return find_class_entry(handle) != nullptr;
}

A32JniRegistryError A32JniClassRegistry::add_reference_identity(
    std::uint32_t handle) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (handle == 0U) {
        return A32JniRegistryError::InvalidReferenceHandle;
    }
    if (find_reference_entry(handle) != nullptr) {
        return A32JniRegistryError::DuplicateReferenceHandle;
    }
    if (references_.size() >= limits_.max_reference_handles) {
        return A32JniRegistryError::ReferenceLimitExceeded;
    }
    references_.push_back(ReferenceEntry{.handle = handle});
    return A32JniRegistryError::None;
}

bool A32JniClassRegistry::retain_local_reference(
    std::uint32_t handle) noexcept {
    ReferenceEntry* entry = find_reference_entry(handle);
    if (entry == nullptr ||
        entry->local_count >=
            limits_.max_reference_count_per_handle) {
        return false;
    }
    ++entry->local_count;
    return true;
}

std::uint32_t A32JniClassRegistry::new_global_reference(
    std::uint32_t handle) noexcept {
    if (handle == 0U) {
        return 0U;
    }
    ReferenceEntry* entry = find_reference_entry(handle);
    if (entry == nullptr ||
        (entry->local_count == 0U && entry->global_count == 0U) ||
        entry->global_count >=
            limits_.max_reference_count_per_handle) {
        return 0U;
    }
    ++entry->global_count;
    return handle;
}

bool A32JniClassRegistry::delete_local_reference(
    std::uint32_t handle) noexcept {
    if (handle == 0U) {
        return true;
    }
    ReferenceEntry* entry = find_reference_entry(handle);
    if (entry == nullptr || entry->local_count == 0U) {
        return false;
    }
    --entry->local_count;
    return true;
}

bool A32JniClassRegistry::delete_global_reference(
    std::uint32_t handle) noexcept {
    if (handle == 0U) {
        return true;
    }
    ReferenceEntry* entry = find_reference_entry(handle);
    if (entry == nullptr || entry->global_count == 0U) {
        return false;
    }
    --entry->global_count;
    return true;
}

std::optional<A32JniReferenceCounts>
A32JniClassRegistry::reference_counts(
    std::uint32_t handle) const noexcept {
    const ReferenceEntry* entry = find_reference_entry(handle);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return A32JniReferenceCounts{
        .local = entry->local_count,
        .global = entry->global_count,
    };
}

A32JniRegistryError A32JniClassRegistry::add_array(
    std::uint32_t handle,
    std::uint32_t length) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (handle == 0U) {
        return A32JniRegistryError::InvalidArrayHandle;
    }
    if (length >
        static_cast<std::uint32_t>(
            std::numeric_limits<std::int32_t>::max())) {
        return A32JniRegistryError::InvalidArrayLength;
    }
    for (const A32JniArrayInfo& array : arrays_) {
        if (array.handle == handle) {
            return A32JniRegistryError::DuplicateArrayHandle;
        }
    }
    if (arrays_.size() >= limits_.max_arrays) {
        return A32JniRegistryError::ArrayLimitExceeded;
    }

    bool inserted_reference = false;
    if (find_reference_entry(handle) == nullptr) {
        if (references_.size() >= limits_.max_reference_handles) {
            return A32JniRegistryError::ReferenceLimitExceeded;
        }
        references_.push_back(ReferenceEntry{.handle = handle});
        inserted_reference = true;
    }
    if (!retain_local_reference(handle)) {
        if (inserted_reference) {
            references_.pop_back();
        }
        return A32JniRegistryError::ReferenceCountExceeded;
    }

    arrays_.push_back(A32JniArrayInfo{
        .handle = handle,
        .length = length,
    });
    return A32JniRegistryError::None;
}

std::optional<std::uint32_t> A32JniClassRegistry::array_length(
    std::uint32_t handle) const noexcept {
    for (const A32JniArrayInfo& array : arrays_) {
        if (array.handle == handle) {
            return array.length;
        }
    }
    return std::nullopt;
}

A32JniRegistryError A32JniClassRegistry::create_long_array(
    std::int32_t length,
    std::uint32_t& handle) {
    handle = 0U;
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (length < 0 ||
        static_cast<std::size_t>(length) >
            limits_.max_long_array_elements) {
        return A32JniRegistryError::InvalidArrayLength;
    }
    if (arrays_.size() >= limits_.max_arrays) {
        return A32JniRegistryError::ArrayLimitExceeded;
    }
    if (references_.size() >= limits_.max_reference_handles) {
        return A32JniRegistryError::ReferenceLimitExceeded;
    }

    const std::size_t candidate_count =
        limits_.max_arrays +
        limits_.max_reference_handles;
    for (std::size_t index = 0U;
         index < candidate_count;
         ++index) {
        const std::uint64_t candidate64 =
            static_cast<std::uint64_t>(
                limits_.dynamic_array_handle_base) +
            static_cast<std::uint64_t>(index) *
                limits_.dynamic_array_handle_stride;
        if (candidate64 >
            std::numeric_limits<std::uint32_t>::max()) {
            break;
        }
        const auto candidate =
            static_cast<std::uint32_t>(candidate64);
        if (candidate == 0U ||
            find_reference_entry(candidate) != nullptr) {
            continue;
        }

        A32JniLongArrayInfo long_array;
        long_array.handle = candidate;
        long_array.elements.assign(
            static_cast<std::size_t>(length),
            std::int64_t{0});

        references_.push_back(ReferenceEntry{
            .handle = candidate,
            .local_count = 1U,
            .global_count = 0U,
        });
        arrays_.push_back(A32JniArrayInfo{
            .handle = candidate,
            .length = static_cast<std::uint32_t>(length),
        });
        long_arrays_.push_back(std::move(long_array));
        handle = candidate;
        return A32JniRegistryError::None;
    }
    return A32JniRegistryError::ArrayHandleExhausted;
}

const A32JniLongArrayInfo* A32JniClassRegistry::find_long_array(
    std::uint32_t handle) const noexcept {
    for (const A32JniLongArrayInfo& array : long_arrays_) {
        if (array.handle == handle) {
            return &array;
        }
    }
    return nullptr;
}

A32JniLongArrayInfo* A32JniClassRegistry::find_long_array(
    std::uint32_t handle) noexcept {
    for (A32JniLongArrayInfo& array : long_arrays_) {
        if (array.handle == handle) {
            return &array;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::add_byte_array(
    std::uint32_t handle,
    std::span<const std::uint8_t> elements) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (elements.size() > limits_.max_byte_array_elements) {
        return A32JniRegistryError::InvalidArrayLength;
    }
    const A32JniRegistryError added =
        add_array(handle, static_cast<std::uint32_t>(elements.size()));
    if (added != A32JniRegistryError::None) {
        return added;
    }

    A32JniByteArrayInfo array;
    array.handle = handle;
    array.elements.assign(elements.begin(), elements.end());
    byte_arrays_.push_back(std::move(array));
    return A32JniRegistryError::None;
}

const A32JniByteArrayInfo* A32JniClassRegistry::find_byte_array(
    std::uint32_t handle) const noexcept {
    for (const A32JniByteArrayInfo& array : byte_arrays_) {
        if (array.handle == handle) {
            return &array;
        }
    }
    return nullptr;
}

A32JniByteArrayInfo* A32JniClassRegistry::find_byte_array(
    std::uint32_t handle) noexcept {
    for (A32JniByteArrayInfo& array : byte_arrays_) {
        if (array.handle == handle) {
            return &array;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::create_object_array(
    std::int32_t length,
    std::uint32_t element_class_handle,
    std::uint32_t initial_element,
    std::uint32_t& handle) {
    handle = 0U;
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (!contains_class_handle(element_class_handle)) {
        return A32JniRegistryError::UnknownClass;
    }
    if (length < 0 ||
        static_cast<std::size_t>(length) >
            limits_.max_object_array_elements) {
        return A32JniRegistryError::InvalidArrayLength;
    }
    if (initial_element != 0U) {
        const auto counts = reference_counts(initial_element);
        if (!counts.has_value() ||
            (counts->local == 0U && counts->global == 0U)) {
            return A32JniRegistryError::InvalidReferenceHandle;
        }
    }
    if (arrays_.size() >= limits_.max_arrays) {
        return A32JniRegistryError::ArrayLimitExceeded;
    }
    if (references_.size() >= limits_.max_reference_handles) {
        return A32JniRegistryError::ReferenceLimitExceeded;
    }

    const std::size_t candidate_count =
        limits_.max_arrays +
        limits_.max_reference_handles;
    for (std::size_t index = 0U;
         index < candidate_count;
         ++index) {
        const std::uint64_t candidate64 =
            static_cast<std::uint64_t>(
                limits_.dynamic_array_handle_base) +
            static_cast<std::uint64_t>(index) *
                limits_.dynamic_array_handle_stride;
        if (candidate64 >
            std::numeric_limits<std::uint32_t>::max()) {
            break;
        }
        const auto candidate =
            static_cast<std::uint32_t>(candidate64);
        if (candidate == 0U ||
            find_reference_entry(candidate) != nullptr) {
            continue;
        }

        A32JniObjectArrayInfo object_array;
        object_array.handle = candidate;
        object_array.element_class_handle = element_class_handle;
        object_array.elements.assign(
            static_cast<std::size_t>(length),
            initial_element);

        references_.push_back(ReferenceEntry{
            .handle = candidate,
            .local_count = 1U,
            .global_count = 0U,
        });
        arrays_.push_back(A32JniArrayInfo{
            .handle = candidate,
            .length = static_cast<std::uint32_t>(length),
        });
        object_arrays_.push_back(std::move(object_array));
        handle = candidate;
        return A32JniRegistryError::None;
    }
    return A32JniRegistryError::ArrayHandleExhausted;
}

const A32JniObjectArrayInfo* A32JniClassRegistry::find_object_array(
    std::uint32_t handle) const noexcept {
    for (const A32JniObjectArrayInfo& array : object_arrays_) {
        if (array.handle == handle) {
            return &array;
        }
    }
    return nullptr;
}

A32JniObjectArrayInfo* A32JniClassRegistry::find_object_array(
    std::uint32_t handle) noexcept {
    for (A32JniObjectArrayInfo& array : object_arrays_) {
        if (array.handle == handle) {
            return &array;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::create_modified_utf8_string(
    std::string_view value,
    std::uint32_t& handle) {
    handle = 0U;
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (value.size() > limits_.max_modified_utf8_bytes) {
        return A32JniRegistryError::InvalidStringValue;
    }
    if (strings_.size() >= limits_.max_strings) {
        return A32JniRegistryError::StringLimitExceeded;
    }
    if (references_.size() >= limits_.max_reference_handles) {
        return A32JniRegistryError::ReferenceLimitExceeded;
    }

    const std::size_t candidate_count =
        limits_.max_strings +
        limits_.max_reference_handles;
    for (std::size_t index = 0U;
         index < candidate_count;
         ++index) {
        const std::uint64_t candidate64 =
            static_cast<std::uint64_t>(
                limits_.dynamic_string_handle_base) +
            static_cast<std::uint64_t>(index) *
                limits_.dynamic_string_handle_stride;
        if (candidate64 >
            std::numeric_limits<std::uint32_t>::max()) {
            break;
        }
        const auto candidate =
            static_cast<std::uint32_t>(candidate64);
        if (candidate == 0U ||
            find_reference_entry(candidate) != nullptr) {
            continue;
        }

        references_.push_back(ReferenceEntry{
            .handle = candidate,
            .local_count = 1U,
            .global_count = 0U,
        });
        A32JniStringInfo string;
        string.handle = candidate;
        string.modified_utf8.assign(value);
        strings_.push_back(std::move(string));
        handle = candidate;
        return A32JniRegistryError::None;
    }
    return A32JniRegistryError::StringHandleExhausted;
}

const A32JniStringInfo*
A32JniClassRegistry::find_modified_utf8_string(
    std::uint32_t handle) const noexcept {
    for (const A32JniStringInfo& string : strings_) {
        if (string.handle == handle) {
            return &string;
        }
    }
    return nullptr;
}

const A32JniClassRegistry::ReferenceEntry*
A32JniClassRegistry::find_reference_entry(
    std::uint32_t handle) const noexcept {
    for (const ReferenceEntry& entry : references_) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

A32JniClassRegistry::ReferenceEntry*
A32JniClassRegistry::find_reference_entry(
    std::uint32_t handle) noexcept {
    for (ReferenceEntry& entry : references_) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

const A32JniClassRegistry::ClassEntry*
A32JniClassRegistry::find_class_entry(
    std::uint32_t handle) const noexcept {
    for (const ClassEntry& entry : classes_) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::add_member(
    std::uint32_t class_handle,
    A32JniMemberKind kind,
    std::uint32_t handle,
    std::string_view name,
    std::string_view signature) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    const ClassEntry* class_entry = find_class_entry(class_handle);
    if (class_entry == nullptr) {
        return A32JniRegistryError::UnknownClass;
    }
    if (handle == 0U ||
        handle == std::numeric_limits<std::uint32_t>::max()) {
        return A32JniRegistryError::InvalidMemberHandle;
    }
    if (kind != A32JniMemberKind::InstanceMethod &&
        kind != A32JniMemberKind::InstanceField &&
        kind != A32JniMemberKind::StaticField &&
        kind != A32JniMemberKind::StaticMethod) {
        return A32JniRegistryError::InvalidMemberKind;
    }
    if (name.empty() ||
        name.size() > limits_.max_method_name_bytes ||
        signature.empty() ||
        signature.size() > limits_.max_signature_bytes) {
        return A32JniRegistryError::InvalidMethod;
    }
    for (const A32JniMemberId& member : members_) {
        if (member.handle == handle) {
            return A32JniRegistryError::DuplicateMemberHandle;
        }
        if (member.class_handle == class_handle &&
            member.kind == kind &&
            member.name == name &&
            member.signature == signature) {
            return A32JniRegistryError::DuplicateMember;
        }
    }
    if (members_.size() >= limits_.max_member_ids) {
        return A32JniRegistryError::MemberLimitExceeded;
    }

    A32JniMemberId member;
    member.class_handle = class_handle;
    member.handle = handle;
    member.kind = kind;
    member.class_name = class_entry->name;
    member.name.assign(name);
    member.signature.assign(signature);
    members_.push_back(std::move(member));
    return A32JniRegistryError::None;
}

const A32JniMemberId* A32JniClassRegistry::find_member(
    std::uint32_t class_handle,
    A32JniMemberKind kind,
    std::string_view name,
    std::string_view signature) const noexcept {
    for (const A32JniMemberId& member : members_) {
        if (member.class_handle == class_handle &&
            member.kind == kind &&
            member.name == name &&
            member.signature == signature) {
            return &member;
        }
    }
    return nullptr;
}

const A32JniMemberId* A32JniClassRegistry::find_member_by_handle(
    std::uint32_t handle) const noexcept {
    for (const A32JniMemberId& member : members_) {
        if (member.handle == handle) {
            return &member;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::set_static_int_field_value(
    std::uint32_t field_handle,
    std::int32_t value) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    const A32JniMemberId* member = find_member_by_handle(field_handle);
    if (member == nullptr) {
        return A32JniRegistryError::InvalidMemberHandle;
    }
    if (member->kind != A32JniMemberKind::StaticField) {
        return A32JniRegistryError::InvalidMemberKind;
    }
    for (A32JniStaticIntFieldValue& current : static_int_fields_) {
        if (current.field_handle == field_handle) {
            current.value = value;
            return A32JniRegistryError::None;
        }
    }
    if (static_int_fields_.size() >= limits_.max_member_ids) {
        return A32JniRegistryError::MemberLimitExceeded;
    }
    static_int_fields_.push_back(A32JniStaticIntFieldValue{
        .field_handle = field_handle,
        .value = value,
    });
    return A32JniRegistryError::None;
}

std::optional<std::int32_t> A32JniClassRegistry::static_int_field_value(
    std::uint32_t field_handle) const noexcept {
    for (const A32JniStaticIntFieldValue& current : static_int_fields_) {
        if (current.field_handle == field_handle) {
            return current.value;
        }
    }
    return std::nullopt;
}

A32JniRegistryError A32JniClassRegistry::set_instance_int_field_value(
    std::uint32_t object_handle,
    std::uint32_t field_handle,
    std::int32_t value) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (find_reference_entry(object_handle) == nullptr) {
        return A32JniRegistryError::InvalidReferenceHandle;
    }
    const A32JniMemberId* member = find_member_by_handle(field_handle);
    if (member == nullptr) {
        return A32JniRegistryError::InvalidMemberHandle;
    }
    if (member->kind != A32JniMemberKind::InstanceField) {
        return A32JniRegistryError::InvalidMemberKind;
    }
    for (A32JniInstanceIntFieldValue& current : instance_int_fields_) {
        if (current.object_handle == object_handle &&
            current.field_handle == field_handle) {
            current.value = value;
            return A32JniRegistryError::None;
        }
    }
    if (instance_int_fields_.size() >= limits_.max_member_ids) {
        return A32JniRegistryError::MemberLimitExceeded;
    }
    instance_int_fields_.push_back(A32JniInstanceIntFieldValue{
        .object_handle = object_handle,
        .field_handle = field_handle,
        .value = value,
    });
    return A32JniRegistryError::None;
}

std::optional<std::int32_t>
A32JniClassRegistry::instance_int_field_value(
    std::uint32_t object_handle,
    std::uint32_t field_handle) const noexcept {
    for (const A32JniInstanceIntFieldValue& current :
         instance_int_fields_) {
        if (current.object_handle == object_handle &&
            current.field_handle == field_handle) {
            return current.value;
        }
    }
    return std::nullopt;
}

A32JniRegistryError A32JniClassRegistry::set_instance_long_field_value(
    std::uint32_t object_handle,
    std::uint32_t field_handle,
    std::int64_t value) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    if (find_reference_entry(object_handle) == nullptr) {
        return A32JniRegistryError::InvalidReferenceHandle;
    }
    const A32JniMemberId* member = find_member_by_handle(field_handle);
    if (member == nullptr) {
        return A32JniRegistryError::InvalidMemberHandle;
    }
    if (member->kind != A32JniMemberKind::InstanceField) {
        return A32JniRegistryError::InvalidMemberKind;
    }
    for (A32JniInstanceLongFieldValue& current : instance_long_fields_) {
        if (current.object_handle == object_handle &&
            current.field_handle == field_handle) {
            current.value = value;
            return A32JniRegistryError::None;
        }
    }
    if (instance_long_fields_.size() >= limits_.max_member_ids) {
        return A32JniRegistryError::MemberLimitExceeded;
    }
    instance_long_fields_.push_back(A32JniInstanceLongFieldValue{
        .object_handle = object_handle,
        .field_handle = field_handle,
        .value = value,
    });
    return A32JniRegistryError::None;
}

std::optional<std::int64_t>
A32JniClassRegistry::instance_long_field_value(
    std::uint32_t object_handle,
    std::uint32_t field_handle) const noexcept {
    for (const A32JniInstanceLongFieldValue& current :
         instance_long_fields_) {
        if (current.object_handle == object_handle &&
            current.field_handle == field_handle) {
            return current.value;
        }
    }
    return std::nullopt;
}

A32JniRegistryError A32JniClassRegistry::set_pending_exception(
    std::uint32_t class_handle,
    std::string_view message) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    const ClassEntry* class_entry = find_class_entry(class_handle);
    if (class_entry == nullptr) {
        return A32JniRegistryError::UnknownClass;
    }
    if (message.size() > limits_.max_exception_message_bytes) {
        return A32JniRegistryError::InvalidStringValue;
    }
    if (pending_exception_.has_value()) {
        return A32JniRegistryError::ExceptionPending;
    }
    if (references_.size() >= limits_.max_reference_handles) {
        return A32JniRegistryError::ReferenceLimitExceeded;
    }

    std::uint32_t exception_handle{};
    for (std::size_t index = 0U;
         index < limits_.max_reference_handles;
         ++index) {
        const std::uint64_t candidate64 =
            static_cast<std::uint64_t>(
                limits_.dynamic_exception_handle_base) +
            static_cast<std::uint64_t>(index) *
                limits_.dynamic_exception_handle_stride;
        if (candidate64 >
            std::numeric_limits<std::uint32_t>::max()) {
            break;
        }
        const auto candidate =
            static_cast<std::uint32_t>(candidate64);
        if (candidate == 0U ||
            find_reference_entry(candidate) != nullptr) {
            continue;
        }
        exception_handle = candidate;
        break;
    }
    if (exception_handle == 0U) {
        return A32JniRegistryError::ReferenceLimitExceeded;
    }

    references_.push_back(ReferenceEntry{
        .handle = exception_handle,
    });

    A32JniPendingException pending;
    pending.handle = exception_handle;
    pending.class_handle = class_handle;
    pending.class_name = class_entry->name;
    pending.message.assign(message);
    pending_exception_ = std::move(pending);
    return A32JniRegistryError::None;
}

void A32JniClassRegistry::clear_pending_exception() noexcept {
    if (!pending_exception_.has_value()) {
        return;
    }
    const std::uint32_t handle = pending_exception_->handle;
    pending_exception_.reset();

    const auto found = std::find_if(
        references_.begin(),
        references_.end(),
        [handle](const ReferenceEntry& entry) {
            return entry.handle == handle;
        });
    if (found != references_.end() &&
        found->local_count == 0U &&
        found->global_count == 0U) {
        references_.erase(found);
    }
}

const A32JniRegisteredNative* A32JniClassRegistry::find_native(
    std::uint32_t class_handle,
    std::string_view name,
    std::string_view signature) const noexcept {
    for (const A32JniRegisteredNative& method : natives_) {
        if (method.class_handle == class_handle &&
            method.name == name &&
            method.signature == signature) {
            return &method;
        }
    }
    return nullptr;
}

A32JniRegistryError A32JniClassRegistry::register_natives(
    std::uint32_t class_handle,
    std::vector<A32JniRegisteredNative> pending) {
    if (!valid()) {
        return A32JniRegistryError::InvalidLimits;
    }
    const ClassEntry* class_entry = find_class_entry(class_handle);
    if (class_entry == nullptr) {
        return A32JniRegistryError::UnknownClass;
    }
    if (pending.size() > limits_.max_methods_per_registration) {
        return A32JniRegistryError::MethodLimitExceeded;
    }

    for (std::size_t left = 0U; left < pending.size(); ++left) {
        A32JniRegisteredNative& method = pending[left];
        if (method.class_handle != class_handle ||
            method.function == 0U ||
            method.function ==
                std::numeric_limits<std::uint32_t>::max() ||
            method.name.empty() ||
            method.name.size() > limits_.max_method_name_bytes ||
            method.signature.empty() ||
            method.signature.size() > limits_.max_signature_bytes) {
            return A32JniRegistryError::InvalidMethod;
        }
        for (std::size_t right = left + 1U;
             right < pending.size();
             ++right) {
            if (pending[right].class_handle == method.class_handle &&
                pending[right].name == method.name &&
                pending[right].signature == method.signature) {
                return A32JniRegistryError::DuplicateMethod;
            }
        }
        method.class_name = class_entry->name;
    }

    std::size_t additions = 0U;
    for (const A32JniRegisteredNative& method : pending) {
        if (find_native(
                method.class_handle,
                method.name,
                method.signature) == nullptr) {
            ++additions;
        }
    }
    if (additions >
            limits_.max_registered_methods - natives_.size()) {
        return A32JniRegistryError::MethodLimitExceeded;
    }

    std::vector<A32JniRegisteredNative> updated = natives_;
    updated.reserve(updated.size() + additions);
    for (A32JniRegisteredNative& method : pending) {
        auto found = std::find_if(
            updated.begin(),
            updated.end(),
            [&](const A32JniRegisteredNative& current) {
                return current.class_handle == method.class_handle &&
                       current.name == method.name &&
                       current.signature == method.signature;
            });
        if (found == updated.end()) {
            updated.push_back(std::move(method));
        } else {
            *found = std::move(method);
        }
    }
    natives_.swap(updated);
    return A32JniRegistryError::None;
}

bool A32JniVmService::layout_valid() const noexcept {
    if (layout_.string_utf_scratch_bytes == 0U ||
        layout_.string_utf_scratch_bytes >
            kA32JniHardMaxStringBytes + 1U ||
        layout_.long_array_scratch_bytes == 0U ||
        (layout_.long_array_scratch_address & 7U) != 0U ||
        (layout_.long_array_scratch_bytes & 7U) != 0U ||
        layout_.long_array_scratch_bytes >
            kA32JniHardMaxLongArrayElements * 8U ||
        layout_.byte_array_scratch_bytes == 0U ||
        layout_.byte_array_scratch_bytes >
            kA32JniHardMaxByteArrayElements) {
        return false;
    }
    const std::array<AddressRange, 42> ranges{{
        {layout_.java_vm_address, kJavaVmBytes},
        {layout_.invoke_table_address, kInvokeTableBytes},
        {layout_.jni_env_address, kJniEnvBytes},
        {layout_.native_table_address, kNativeTableBytes},
        {layout_.get_env_stub_address, kServiceStubBytes},
        {layout_.find_class_stub_address, kServiceStubBytes},
        {layout_.register_natives_stub_address, kServiceStubBytes},
        {layout_.get_method_id_stub_address, kServiceStubBytes},
        {layout_.get_field_id_stub_address, kServiceStubBytes},
        {layout_.get_static_field_id_stub_address, kServiceStubBytes},
        {layout_.attach_current_thread_stub_address, kServiceStubBytes},
        {layout_.detach_current_thread_stub_address, kServiceStubBytes},
        {layout_.new_global_ref_stub_address, kServiceStubBytes},
        {layout_.delete_global_ref_stub_address, kServiceStubBytes},
        {layout_.delete_local_ref_stub_address, kServiceStubBytes},
        {layout_.get_array_length_stub_address, kServiceStubBytes},
        {layout_.get_static_int_field_stub_address, kServiceStubBytes},
        {layout_.new_string_utf_stub_address, kServiceStubBytes},
        {layout_.get_string_utf_chars_stub_address, kServiceStubBytes},
        {layout_.release_string_utf_chars_stub_address, kServiceStubBytes},
        {layout_.string_utf_scratch_address,
         layout_.string_utf_scratch_bytes},
        {layout_.new_long_array_stub_address, kServiceStubBytes},
        {layout_.get_long_array_elements_stub_address, kServiceStubBytes},
        {layout_.release_long_array_elements_stub_address, kServiceStubBytes},
        {layout_.set_long_array_region_stub_address, kServiceStubBytes},
        {layout_.long_array_scratch_address,
         layout_.long_array_scratch_bytes},
        {layout_.new_object_array_stub_address, kServiceStubBytes},
        {layout_.get_object_array_element_stub_address, kServiceStubBytes},
        {layout_.set_object_array_element_stub_address, kServiceStubBytes},
        {layout_.get_long_field_stub_address, kServiceStubBytes},
        {layout_.set_long_field_stub_address, kServiceStubBytes},
        {layout_.throw_new_stub_address, kServiceStubBytes},
        {layout_.call_void_method_v_stub_address, kServiceStubBytes},
        {layout_.get_byte_array_elements_stub_address, kServiceStubBytes},
        {layout_.release_byte_array_elements_stub_address, kServiceStubBytes},
        {layout_.byte_array_scratch_address,
         layout_.byte_array_scratch_bytes},
        {layout_.call_void_method_stub_address, kServiceStubBytes},
        {layout_.get_int_field_stub_address, kServiceStubBytes},
        {layout_.exception_occurred_stub_address, kServiceStubBytes},
        {layout_.exception_clear_stub_address, kServiceStubBytes},
        {layout_.new_object_v_stub_address, kServiceStubBytes},
        {layout_.get_static_method_id_stub_address, kServiceStubBytes},
    }};

    for (const AddressRange& range : ranges) {
        if (!valid_range(range)) {
            return false;
        }
    }
    for (std::size_t left = 0U; left < ranges.size(); ++left) {
        for (std::size_t right = left + 1U;
             right < ranges.size();
             ++right) {
            if (overlaps(ranges[left], ranges[right])) {
                return false;
            }
        }
    }
    return true;
}

A32JniVmInstallResult A32JniVmService::install(
    memory::GuestMemory& memory) {
    installed_ = false;
    if (!layout_valid()) {
        return {.error = A32JniVmInstallError::InvalidLayout};
    }

    std::array<InstallRegion, 39> regions{{
        {.address = layout_.java_vm_address, .size = kJavaVmBytes},
        {.address = layout_.invoke_table_address,
         .size = kInvokeTableBytes},
        {.address = layout_.jni_env_address, .size = kJniEnvBytes},
        {.address = layout_.native_table_address,
         .size = kNativeTableBytes},
        {.address = layout_.get_env_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.find_class_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.register_natives_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_method_id_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_field_id_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_static_field_id_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.attach_current_thread_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.detach_current_thread_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.new_global_ref_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.delete_global_ref_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.delete_local_ref_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_array_length_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_static_int_field_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.new_string_utf_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_string_utf_chars_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.release_string_utf_chars_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.new_long_array_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_long_array_elements_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.release_long_array_elements_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.set_long_array_region_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.new_object_array_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_object_array_element_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.set_object_array_element_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_long_field_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.set_long_field_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.throw_new_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.call_void_method_v_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_byte_array_elements_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.release_byte_array_elements_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.call_void_method_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_int_field_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.exception_occurred_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.exception_clear_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.new_object_v_stub_address,
         .size = kServiceStubBytes},
        {.address = layout_.get_static_method_id_stub_address,
         .size = kServiceStubBytes},
    }};

    write_u32(
        regions[0].desired,
        0U,
        layout_.invoke_table_address);
    write_u32(
        regions[1].desired,
        kAttachCurrentThreadSlot * 4U,
        layout_.attach_current_thread_stub_address);
    write_u32(
        regions[1].desired,
        kDetachCurrentThreadSlot * 4U,
        layout_.detach_current_thread_stub_address);
    write_u32(
        regions[1].desired,
        kGetEnvSlot * 4U,
        layout_.get_env_stub_address);
    write_u32(
        regions[2].desired,
        0U,
        layout_.native_table_address);
    write_u32(
        regions[3].desired,
        kFindClassSlot * 4U,
        layout_.find_class_stub_address);
    write_u32(
        regions[3].desired,
        kThrowNewSlot * 4U,
        layout_.throw_new_stub_address);
    write_u32(
        regions[3].desired,
        kExceptionOccurredSlot * 4U,
        layout_.exception_occurred_stub_address);
    write_u32(
        regions[3].desired,
        kExceptionClearSlot * 4U,
        layout_.exception_clear_stub_address);
    write_u32(
        regions[3].desired,
        kNewGlobalRefSlot * 4U,
        layout_.new_global_ref_stub_address);
    write_u32(
        regions[3].desired,
        kDeleteGlobalRefSlot * 4U,
        layout_.delete_global_ref_stub_address);
    write_u32(
        regions[3].desired,
        kDeleteLocalRefSlot * 4U,
        layout_.delete_local_ref_stub_address);
    write_u32(
        regions[3].desired,
        kNewObjectVSlot * 4U,
        layout_.new_object_v_stub_address);
    write_u32(
        regions[3].desired,
        kGetMethodIdSlot * 4U,
        layout_.get_method_id_stub_address);
    write_u32(
        regions[3].desired,
        kCallVoidMethodSlot * 4U,
        layout_.call_void_method_stub_address);
    write_u32(
        regions[3].desired,
        kCallVoidMethodVSlot * 4U,
        layout_.call_void_method_v_stub_address);
    write_u32(
        regions[3].desired,
        kGetFieldIdSlot * 4U,
        layout_.get_field_id_stub_address);
    write_u32(
        regions[3].desired,
        kGetIntFieldSlot * 4U,
        layout_.get_int_field_stub_address);
    write_u32(
        regions[3].desired,
        kGetLongFieldSlot * 4U,
        layout_.get_long_field_stub_address);
    write_u32(
        regions[3].desired,
        kGetStaticMethodIdSlot * 4U,
        layout_.get_static_method_id_stub_address);
    write_u32(
        regions[3].desired,
        kSetLongFieldSlot * 4U,
        layout_.set_long_field_stub_address);
    write_u32(
        regions[3].desired,
        kGetStaticFieldIdSlot * 4U,
        layout_.get_static_field_id_stub_address);
    write_u32(
        regions[3].desired,
        kGetStaticIntFieldSlot * 4U,
        layout_.get_static_int_field_stub_address);
    write_u32(
        regions[3].desired,
        kNewStringUtfSlot * 4U,
        layout_.new_string_utf_stub_address);
    write_u32(
        regions[3].desired,
        kGetStringUtfCharsSlot * 4U,
        layout_.get_string_utf_chars_stub_address);
    write_u32(
        regions[3].desired,
        kReleaseStringUtfCharsSlot * 4U,
        layout_.release_string_utf_chars_stub_address);
    write_u32(
        regions[3].desired,
        kGetArrayLengthSlot * 4U,
        layout_.get_array_length_stub_address);
    write_u32(
        regions[3].desired,
        kNewObjectArraySlot * 4U,
        layout_.new_object_array_stub_address);
    write_u32(
        regions[3].desired,
        kGetObjectArrayElementSlot * 4U,
        layout_.get_object_array_element_stub_address);
    write_u32(
        regions[3].desired,
        kSetObjectArrayElementSlot * 4U,
        layout_.set_object_array_element_stub_address);
    write_u32(
        regions[3].desired,
        kNewLongArraySlot * 4U,
        layout_.new_long_array_stub_address);
    write_u32(
        regions[3].desired,
        kGetByteArrayElementsSlot * 4U,
        layout_.get_byte_array_elements_stub_address);
    write_u32(
        regions[3].desired,
        kGetLongArrayElementsSlot * 4U,
        layout_.get_long_array_elements_stub_address);
    write_u32(
        regions[3].desired,
        kReleaseByteArrayElementsSlot * 4U,
        layout_.release_byte_array_elements_stub_address);
    write_u32(
        regions[3].desired,
        kReleaseLongArrayElementsSlot * 4U,
        layout_.release_long_array_elements_stub_address);
    write_u32(
        regions[3].desired,
        kSetLongArrayRegionSlot * 4U,
        layout_.set_long_array_region_stub_address);
    write_u32(
        regions[3].desired,
        kRegisterNativesSlot * 4U,
        layout_.register_natives_stub_address);

    write_service_stub(
        regions[4].desired,
        kA32JniGetEnvSvcImmediate);
    write_service_stub(
        regions[5].desired,
        kA32JniFindClassSvcImmediate);
    write_service_stub(
        regions[6].desired,
        kA32JniRegisterNativesSvcImmediate);
    write_service_stub(
        regions[7].desired,
        kA32JniGetMethodIdSvcImmediate);
    write_service_stub(
        regions[8].desired,
        kA32JniGetFieldIdSvcImmediate);
    write_service_stub(
        regions[9].desired,
        kA32JniGetStaticFieldIdSvcImmediate);
    write_service_stub(
        regions[10].desired,
        kA32JniAttachCurrentThreadSvcImmediate);
    write_service_stub(
        regions[11].desired,
        kA32JniDetachCurrentThreadSvcImmediate);
    write_service_stub(
        regions[12].desired,
        kA32JniNewGlobalRefSvcImmediate);
    write_service_stub(
        regions[13].desired,
        kA32JniDeleteGlobalRefSvcImmediate);
    write_service_stub(
        regions[14].desired,
        kA32JniDeleteLocalRefSvcImmediate);
    write_service_stub(
        regions[15].desired,
        kA32JniGetArrayLengthSvcImmediate);
    write_service_stub(
        regions[16].desired,
        kA32JniGetStaticIntFieldSvcImmediate);
    write_service_stub(
        regions[17].desired,
        kA32JniNewStringUtfSvcImmediate);
    write_service_stub(
        regions[18].desired,
        kA32JniGetStringUtfCharsSvcImmediate);
    write_service_stub(
        regions[19].desired,
        kA32JniReleaseStringUtfCharsSvcImmediate);
    write_service_stub(
        regions[20].desired,
        kA32JniNewLongArraySvcImmediate);
    write_service_stub(
        regions[21].desired,
        kA32JniGetLongArrayElementsSvcImmediate);
    write_service_stub(
        regions[22].desired,
        kA32JniReleaseLongArrayElementsSvcImmediate);
    write_service_stub(
        regions[23].desired,
        kA32JniSetLongArrayRegionSvcImmediate);
    write_service_stub(
        regions[24].desired,
        kA32JniNewObjectArraySvcImmediate);
    write_service_stub(
        regions[25].desired,
        kA32JniGetObjectArrayElementSvcImmediate);
    write_service_stub(
        regions[26].desired,
        kA32JniSetObjectArrayElementSvcImmediate);
    write_service_stub(
        regions[27].desired,
        kA32JniGetLongFieldSvcImmediate);
    write_service_stub(
        regions[28].desired,
        kA32JniSetLongFieldSvcImmediate);
    write_service_stub(
        regions[29].desired,
        kA32JniThrowNewSvcImmediate);
    write_service_stub(
        regions[30].desired,
        kA32JniCallVoidMethodVSvcImmediate);
    write_service_stub(
        regions[31].desired,
        kA32JniGetByteArrayElementsSvcImmediate);
    write_service_stub(
        regions[32].desired,
        kA32JniReleaseByteArrayElementsSvcImmediate);
    write_service_stub(
        regions[33].desired,
        kA32JniCallVoidMethodSvcImmediate);
    write_service_stub(
        regions[34].desired,
        kA32JniGetIntFieldSvcImmediate);
    write_service_stub(
        regions[35].desired,
        kA32JniExceptionOccurredSvcImmediate);
    write_service_stub(
        regions[36].desired,
        kA32JniExceptionClearSvcImmediate);
    write_service_stub(
        regions[37].desired,
        kA32JniNewObjectVSvcImmediate);
    write_service_stub(
        regions[38].desired,
        kA32JniGetStaticMethodIdSvcImmediate);

    for (InstallRegion& region : regions) {
        if (!memory.read(
                region.address,
                std::span<std::uint8_t>{
                    region.original.data(),
                    region.size})) {
            return {.error = A32JniVmInstallError::SnapshotFailed};
        }
    }

    std::size_t written = 0U;
    for (; written < regions.size(); ++written) {
        const InstallRegion& region = regions[written];
        if (memory.write(
                region.address,
                std::span<const std::uint8_t>{
                    region.desired.data(),
                    region.size})) {
            continue;
        }

        bool rollback_ok = true;
        for (std::size_t index = 0U; index < written; ++index) {
            const InstallRegion& previous = regions[index];
            if (!memory.write(
                    previous.address,
                    std::span<const std::uint8_t>{
                        previous.original.data(),
                        previous.size})) {
                rollback_ok = false;
            }
        }
        return {
            .error = rollback_ok
                ? A32JniVmInstallError::WriteFailed
                : A32JniVmInstallError::RollbackFailed,
        };
    }

    installed_ = true;
    attached_ = true;
    active_utf_chars_string_.reset();
    active_long_array_.reset();
    active_byte_array_.reset();
    return {};
}

runtime::A32HostServiceDisposition A32JniVmService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    const bool known_service =
        svc_immediate == kA32JniGetEnvSvcImmediate ||
        svc_immediate == kA32JniFindClassSvcImmediate ||
        svc_immediate == kA32JniRegisterNativesSvcImmediate ||
        svc_immediate == kA32JniGetMethodIdSvcImmediate ||
        svc_immediate == kA32JniGetFieldIdSvcImmediate ||
        svc_immediate == kA32JniGetStaticFieldIdSvcImmediate ||
        svc_immediate == kA32JniAttachCurrentThreadSvcImmediate ||
        svc_immediate == kA32JniDetachCurrentThreadSvcImmediate ||
        svc_immediate == kA32JniNewGlobalRefSvcImmediate ||
        svc_immediate == kA32JniDeleteGlobalRefSvcImmediate ||
        svc_immediate == kA32JniDeleteLocalRefSvcImmediate ||
        svc_immediate == kA32JniGetArrayLengthSvcImmediate ||
        svc_immediate == kA32JniGetStaticIntFieldSvcImmediate ||
        svc_immediate == kA32JniNewStringUtfSvcImmediate ||
        svc_immediate == kA32JniGetStringUtfCharsSvcImmediate ||
        svc_immediate == kA32JniReleaseStringUtfCharsSvcImmediate ||
        svc_immediate == kA32JniNewLongArraySvcImmediate ||
        svc_immediate == kA32JniGetLongArrayElementsSvcImmediate ||
        svc_immediate == kA32JniReleaseLongArrayElementsSvcImmediate ||
        svc_immediate == kA32JniSetLongArrayRegionSvcImmediate ||
        svc_immediate == kA32JniNewObjectArraySvcImmediate ||
        svc_immediate == kA32JniGetObjectArrayElementSvcImmediate ||
        svc_immediate == kA32JniSetObjectArrayElementSvcImmediate ||
        svc_immediate == kA32JniGetLongFieldSvcImmediate ||
        svc_immediate == kA32JniSetLongFieldSvcImmediate ||
        svc_immediate == kA32JniThrowNewSvcImmediate ||
        svc_immediate == kA32JniCallVoidMethodVSvcImmediate ||
        svc_immediate == kA32JniGetByteArrayElementsSvcImmediate ||
        svc_immediate == kA32JniReleaseByteArrayElementsSvcImmediate ||
        svc_immediate == kA32JniCallVoidMethodSvcImmediate ||
        svc_immediate == kA32JniGetIntFieldSvcImmediate ||
        svc_immediate == kA32JniExceptionOccurredSvcImmediate ||
        svc_immediate == kA32JniExceptionClearSvcImmediate ||
        svc_immediate == kA32JniNewObjectVSvcImmediate ||
        svc_immediate == kA32JniGetStaticMethodIdSvcImmediate;
    if (!known_service) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }
    if (!installed_) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    const bool vm_service =
        svc_immediate == kA32JniGetEnvSvcImmediate ||
        svc_immediate == kA32JniAttachCurrentThreadSvcImmediate ||
        svc_immediate == kA32JniDetachCurrentThreadSvcImmediate;
    if (vm_service) {
        if (regs[0] != layout_.java_vm_address) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        if (svc_immediate == kA32JniDetachCurrentThreadSvcImmediate) {
            regs[0] = jint_bits(attached_ ? kA32JniOk : kA32JniErr);
            attached_ = false;
            return runtime::A32HostServiceDisposition::Handled;
        }

        if (svc_immediate == kA32JniGetEnvSvcImmediate) {
            // Dalvik validates the version before touching *env. Preserve the
            // caller's output slot on JNI_EVERSION/JNI_EDETACHED.
            if (!is_a32_supported_jni_getenv_version(regs[2])) {
                regs[0] = jint_bits(kA32JniEversion);
                return runtime::A32HostServiceDisposition::Handled;
            }
            if (!attached_) {
                regs[0] = jint_bits(kA32JniEdetached);
                return runtime::A32HostServiceDisposition::Handled;
            }
        }

        if (regs[1] == 0U) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto env_bytes = u32_bytes(layout_.jni_env_address);
        if (!memory.write(regs[1], env_bytes)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        attached_ = true;
        regs[0] = jint_bits(kA32JniOk);
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (!attached_) {
        return runtime::A32HostServiceDisposition::Failed;
    }
    if (regs[0] != layout_.jni_env_address) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    if (svc_immediate == kA32JniFindClassSvcImmediate) {
        if (registry_ == nullptr) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (!registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        std::string name;
        if (!read_guest_c_string(
                memory,
                regs[1],
                registry_->limits().max_class_name_bytes,
                name)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto found = registry_->find_class(name);
        if (!found.has_value()) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (!registry_->retain_local_reference(*found)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = *found;
        return runtime::A32HostServiceDisposition::Handled;
    }

    const bool reference_service =
        svc_immediate == kA32JniNewGlobalRefSvcImmediate ||
        svc_immediate == kA32JniDeleteGlobalRefSvcImmediate ||
        svc_immediate == kA32JniDeleteLocalRefSvcImmediate;
    if (reference_service) {
        if (registry_ == nullptr) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (!registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::uint32_t reference = regs[1];
        if (svc_immediate == kA32JniNewGlobalRefSvcImmediate) {
            regs[0] = registry_->new_global_reference(reference);
            return runtime::A32HostServiceDisposition::Handled;
        }
        const bool deleted =
            svc_immediate == kA32JniDeleteGlobalRefSvcImmediate
                ? registry_->delete_global_reference(reference)
                : registry_->delete_local_reference(reference);
        if (!deleted) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniNewStringUtfSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::string value;
        if (!read_guest_c_string(
                memory,
                regs[1],
                registry_->limits().max_modified_utf8_bytes,
                value)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::uint32_t handle{};
        const A32JniRegistryError created =
            registry_->create_modified_utf8_string(
                value,
                handle);
        if (created == A32JniRegistryError::InvalidLimits) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] =
            created == A32JniRegistryError::None
                ? handle
                : 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniGetStringUtfCharsSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (regs[1] == 0U) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        const A32JniStringInfo* string =
            registry_->find_modified_utf8_string(regs[1]);
        const auto counts =
            registry_->reference_counts(regs[1]);
        if (string == nullptr ||
            !counts.has_value() ||
            (counts->local == 0U &&
             counts->global == 0U)) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (active_utf_chars_string_.has_value()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (string->modified_utf8.size() + 1U >
            layout_.string_utf_scratch_bytes) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve(string->modified_utf8.size() + 1U);
        for (const unsigned char byte : string->modified_utf8) {
            bytes.push_back(byte);
        }
        bytes.push_back(0U);
        if (!memory.write(
                layout_.string_utf_scratch_address,
                std::span<const std::uint8_t>{
                    bytes.data(),
                    bytes.size()})) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (regs[2] != 0U) {
            constexpr std::array<std::uint8_t, 1> kIsCopy{{1U}};
            if (!memory.write(regs[2], kIsCopy)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
        }
        active_utf_chars_string_ = regs[1];
        regs[0] = layout_.string_utf_scratch_address;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniReleaseStringUtfCharsSvcImmediate) {
        if (!active_utf_chars_string_.has_value() ||
            regs[1] != *active_utf_chars_string_ ||
            regs[2] != layout_.string_utf_scratch_address) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        active_utf_chars_string_.reset();
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniNewObjectArraySvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::uint32_t handle{};
        const A32JniRegistryError created =
            registry_->create_object_array(
                static_cast<std::int32_t>(regs[1]),
                regs[2],
                regs[3],
                handle);
        if (created == A32JniRegistryError::InvalidLimits) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] =
            created == A32JniRegistryError::None
                ? handle
                : 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniGetObjectArrayElementSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        A32JniObjectArrayInfo* array =
            registry_->find_object_array(regs[1]);
        const auto array_counts =
            registry_->reference_counts(regs[1]);
        const std::int32_t index =
            static_cast<std::int32_t>(regs[2]);
        if (array == nullptr ||
            !array_counts.has_value() ||
            (array_counts->local == 0U &&
             array_counts->global == 0U) ||
            index < 0 ||
            static_cast<std::size_t>(index) >=
                array->elements.size()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::uint32_t element =
            array->elements[static_cast<std::size_t>(index)];
        if (element != 0U &&
            !registry_->retain_local_reference(element)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = element;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniSetObjectArrayElementSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        A32JniObjectArrayInfo* array =
            registry_->find_object_array(regs[1]);
        const auto array_counts =
            registry_->reference_counts(regs[1]);
        const std::int32_t index =
            static_cast<std::int32_t>(regs[2]);
        if (array == nullptr ||
            !array_counts.has_value() ||
            (array_counts->local == 0U &&
             array_counts->global == 0U) ||
            index < 0 ||
            static_cast<std::size_t>(index) >=
                array->elements.size()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::uint32_t value = regs[3];
        if (value != 0U) {
            const auto value_counts =
                registry_->reference_counts(value);
            if (!value_counts.has_value() ||
                (value_counts->local == 0U &&
                 value_counts->global == 0U)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
        }
        array->elements[static_cast<std::size_t>(index)] = value;
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniNewLongArraySvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::uint32_t handle{};
        const A32JniRegistryError created =
            registry_->create_long_array(
                static_cast<std::int32_t>(regs[1]),
                handle);
        if (created == A32JniRegistryError::InvalidLimits) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] =
            created == A32JniRegistryError::None
                ? handle
                : 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniGetLongArrayElementsSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniLongArrayInfo* array =
            registry_->find_long_array(regs[1]);
        const auto counts =
            registry_->reference_counts(regs[1]);
        if (array == nullptr ||
            !counts.has_value() ||
            (counts->local == 0U &&
             counts->global == 0U)) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (active_long_array_.has_value() ||
            array->elements.size() >
                layout_.long_array_scratch_bytes / 8U) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (!write_guest_i64_values(
                memory,
                layout_.long_array_scratch_address,
                array->elements)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (regs[2] != 0U) {
            constexpr std::array<std::uint8_t, 1> kIsCopy{{1U}};
            if (!memory.write(regs[2], kIsCopy)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
        }
        active_long_array_ = regs[1];
        regs[0] = layout_.long_array_scratch_address;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniReleaseLongArrayElementsSvcImmediate) {
        if (registry_ == nullptr ||
            !active_long_array_.has_value() ||
            regs[1] != *active_long_array_ ||
            regs[2] != layout_.long_array_scratch_address) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        A32JniLongArrayInfo* array =
            registry_->find_long_array(regs[1]);
        if (array == nullptr) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::int32_t mode =
            static_cast<std::int32_t>(regs[3]);
        if (mode != 0 && mode != 1 && mode != 2) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (mode != 2) {
            std::vector<std::int64_t> values;
            if (!read_guest_i64_values(
                    memory,
                    layout_.long_array_scratch_address,
                    array->elements.size(),
                    values)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
            array->elements = std::move(values);
        }
        if (mode != 1) {
            active_long_array_.reset();
        }
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniGetByteArrayElementsSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniByteArrayInfo* array =
            registry_->find_byte_array(regs[1]);
        const auto counts =
            registry_->reference_counts(regs[1]);
        if (array == nullptr ||
            !counts.has_value() ||
            (counts->local == 0U &&
             counts->global == 0U)) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (active_byte_array_.has_value() ||
            array->elements.size() >
                layout_.byte_array_scratch_bytes) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (!memory.write(
                layout_.byte_array_scratch_address,
                std::span<const std::uint8_t>{
                    array->elements.data(),
                    array->elements.size()})) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (regs[2] != 0U) {
            constexpr std::array<std::uint8_t, 1> kIsCopy{{1U}};
            if (!memory.write(regs[2], kIsCopy)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
        }
        active_byte_array_ = regs[1];
        regs[0] = layout_.byte_array_scratch_address;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate ==
        kA32JniReleaseByteArrayElementsSvcImmediate) {
        if (registry_ == nullptr ||
            !active_byte_array_.has_value() ||
            regs[1] != *active_byte_array_ ||
            regs[2] != layout_.byte_array_scratch_address) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        A32JniByteArrayInfo* array =
            registry_->find_byte_array(regs[1]);
        if (array == nullptr) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::int32_t mode =
            static_cast<std::int32_t>(regs[3]);
        if (mode != 0 && mode != 1 && mode != 2) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (mode != 2) {
            std::vector<std::uint8_t> values(array->elements.size());
            if (!memory.read(
                    layout_.byte_array_scratch_address,
                    std::span<std::uint8_t>{
                        values.data(),
                        values.size()})) {
                return runtime::A32HostServiceDisposition::Failed;
            }
            array->elements = std::move(values);
        }
        if (mode != 1) {
            active_byte_array_.reset();
        }
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniSetLongArrayRegionSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        A32JniLongArrayInfo* array =
            registry_->find_long_array(regs[1]);
        if (array == nullptr) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::int32_t start =
            static_cast<std::int32_t>(regs[2]);
        const std::int32_t length =
            static_cast<std::int32_t>(regs[3]);
        if (start < 0 || length < 0) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::size_t start_index =
            static_cast<std::size_t>(start);
        const std::size_t count =
            static_cast<std::size_t>(length);
        if (start_index > array->elements.size() ||
            count > array->elements.size() - start_index) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        std::uint32_t source{};
        if (!read_guest_u32(memory, regs[13], source)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::vector<std::int64_t> values;
        if (!read_guest_i64_values(
                memory,
                source,
                count,
                values)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::copy(
            values.begin(),
            values.end(),
            array->elements.begin() +
                static_cast<std::ptrdiff_t>(start_index));
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniGetArrayLengthSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto length = registry_->array_length(regs[1]);
        if (!length.has_value()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = *length;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniThrowNewSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (!registry_->contains_class_handle(regs[1])) {
            regs[0] = jint_bits(kA32JniErr);
            return runtime::A32HostServiceDisposition::Handled;
        }
        const auto class_counts =
            registry_->reference_counts(regs[1]);
        if (!class_counts.has_value() ||
            (class_counts->local == 0U &&
             class_counts->global == 0U)) {
            regs[0] = jint_bits(kA32JniErr);
            return runtime::A32HostServiceDisposition::Handled;
        }

        std::string message;
        if (!read_guest_c_string(
                memory,
                regs[2],
                registry_->limits().max_exception_message_bytes,
                message)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniRegistryError thrown =
            registry_->set_pending_exception(
                regs[1],
                message);
        regs[0] = jint_bits(
            thrown == A32JniRegistryError::None
                ? kA32JniOk
                : kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniExceptionOccurredSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniPendingException* pending =
            registry_->pending_exception();
        if (pending == nullptr) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (!registry_->retain_local_reference(pending->handle)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = pending->handle;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniExceptionClearSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        registry_->clear_pending_exception();
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniNewObjectVSvcImmediate) {
        if (registry_ == nullptr ||
            method_call_bridge_ == nullptr ||
            !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (!registry_->contains_class_handle(regs[1])) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto class_counts =
            registry_->reference_counts(regs[1]);
        if (!class_counts.has_value() ||
            (class_counts->local == 0U &&
             class_counts->global == 0U)) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        const A32JniMemberId* found =
            registry_->find_member_by_handle(regs[2]);
        if (found == nullptr ||
            found->kind != A32JniMemberKind::InstanceMethod ||
            found->class_handle != regs[1] ||
            found->name != "<init>") {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniMemberId constructor = *found;

        std::vector<A32JniValue> arguments;
        if (!decode_a32_jni_va_arguments(
                memory,
                constructor.signature,
                regs[3],
                registry_->limits().max_method_arguments,
                arguments)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        for (const A32JniValue& argument : arguments) {
            if (argument.kind != A32JniValueKind::Reference ||
                argument.bits == 0U) {
                continue;
            }
            const auto counts = registry_->reference_counts(
                static_cast<std::uint32_t>(argument.bits));
            if (!counts.has_value() ||
                (counts->local == 0U &&
                 counts->global == 0U)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
        }

        std::uint32_t object_handle{};
        if (!method_call_bridge_->new_object(
                regs[1],
                constructor,
                std::span<const A32JniValue>{arguments},
                object_handle) ||
            object_handle == 0U) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (registry_->add_reference_identity(object_handle) !=
                A32JniRegistryError::None ||
            !registry_->retain_local_reference(object_handle)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = object_handle;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniCallVoidMethodVSvcImmediate ||
        svc_immediate == kA32JniCallVoidMethodSvcImmediate) {
        if (registry_ == nullptr ||
            method_call_bridge_ == nullptr ||
            !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        const auto receiver_counts =
            registry_->reference_counts(regs[1]);
        if (!receiver_counts.has_value() ||
            (receiver_counts->local == 0U &&
             receiver_counts->global == 0U)) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        const A32JniMemberId* found =
            registry_->find_member_by_handle(regs[2]);
        if (found == nullptr ||
            found->kind != A32JniMemberKind::InstanceMethod) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniMemberId method = *found;

        std::vector<A32JniValue> arguments;
        const bool decoded =
            svc_immediate == kA32JniCallVoidMethodVSvcImmediate
                ? decode_a32_jni_va_arguments(
                      memory,
                      method.signature,
                      regs[3],
                      registry_->limits().max_method_arguments,
                      arguments)
                : decode_a32_jni_raw_arguments(
                      memory,
                      method.signature,
                      regs[3],
                      regs[13],
                      registry_->limits().max_method_arguments,
                      arguments);
        if (!decoded) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        for (const A32JniValue& argument : arguments) {
            if (argument.kind != A32JniValueKind::Reference ||
                argument.bits == 0U) {
                continue;
            }
            const auto counts = registry_->reference_counts(
                static_cast<std::uint32_t>(argument.bits));
            if (!counts.has_value() ||
                (counts->local == 0U &&
                 counts->global == 0U)) {
                return runtime::A32HostServiceDisposition::Failed;
            }
        }

        if (!method_call_bridge_->call_void_method(
                regs[1],
                method,
                std::span<const A32JniValue>{arguments})) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniGetStaticIntFieldSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid() ||
            !registry_->contains_class_handle(regs[1])) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniMemberId* member =
            registry_->find_member_by_handle(regs[2]);
        if (member == nullptr ||
            member->class_handle != regs[1] ||
            member->kind != A32JniMemberKind::StaticField) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto value =
            registry_->static_int_field_value(member->handle);
        if (!value.has_value()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = static_cast<std::uint32_t>(*value);
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (svc_immediate == kA32JniGetIntFieldSvcImmediate) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto object_counts =
            registry_->reference_counts(regs[1]);
        if (!object_counts.has_value() ||
            (object_counts->local == 0U &&
             object_counts->global == 0U)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniMemberId* member =
            registry_->find_member_by_handle(regs[2]);
        if (member == nullptr ||
            member->kind != A32JniMemberKind::InstanceField) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto value =
            registry_->instance_int_field_value(
                regs[1],
                member->handle);
        if (!value.has_value()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = static_cast<std::uint32_t>(*value);
        return runtime::A32HostServiceDisposition::Handled;
    }

    const bool instance_long_field_service =
        svc_immediate == kA32JniGetLongFieldSvcImmediate ||
        svc_immediate == kA32JniSetLongFieldSvcImmediate;
    if (instance_long_field_service) {
        if (registry_ == nullptr || !registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const auto object_counts =
            registry_->reference_counts(regs[1]);
        if (!object_counts.has_value() ||
            (object_counts->local == 0U &&
             object_counts->global == 0U)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const A32JniMemberId* member =
            registry_->find_member_by_handle(regs[2]);
        if (member == nullptr ||
            member->kind != A32JniMemberKind::InstanceField) {
            return runtime::A32HostServiceDisposition::Failed;
        }

        if (svc_immediate == kA32JniGetLongFieldSvcImmediate) {
            const auto value =
                registry_->instance_long_field_value(
                    regs[1],
                    member->handle);
            if (!value.has_value()) {
                return runtime::A32HostServiceDisposition::Failed;
            }
            const std::uint64_t bits =
                static_cast<std::uint64_t>(*value);
            regs[0] = static_cast<std::uint32_t>(bits);
            regs[1] = static_cast<std::uint32_t>(bits >> 32U);
            return runtime::A32HostServiceDisposition::Handled;
        }

        if (regs[13] >
            std::numeric_limits<std::uint32_t>::max() - 4U) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        std::uint32_t low{};
        std::uint32_t high{};
        if (!read_guest_u32(memory, regs[13], low) ||
            !read_guest_u32(memory, regs[13] + 4U, high)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        const std::uint64_t bits =
            static_cast<std::uint64_t>(low) |
            (static_cast<std::uint64_t>(high) << 32U);
        const auto updated =
            registry_->set_instance_long_field_value(
                regs[1],
                member->handle,
                static_cast<std::int64_t>(bits));
        if (updated != A32JniRegistryError::None) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        regs[0] = 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    const bool member_lookup =
        svc_immediate == kA32JniGetMethodIdSvcImmediate ||
        svc_immediate == kA32JniGetStaticMethodIdSvcImmediate ||
        svc_immediate == kA32JniGetFieldIdSvcImmediate ||
        svc_immediate == kA32JniGetStaticFieldIdSvcImmediate;
    if (member_lookup) {
        if (registry_ == nullptr) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }
        if (!registry_->valid()) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (!registry_->contains_class_handle(regs[1])) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }

        const A32JniRegistryLimits limits = registry_->limits();
        std::string name;
        std::string signature;
        if (!read_guest_c_string(
                memory,
                regs[2],
                limits.max_method_name_bytes,
                name) ||
            !read_guest_c_string(
                memory,
                regs[3],
                limits.max_signature_bytes,
                signature)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (name.empty() || signature.empty()) {
            regs[0] = 0U;
            return runtime::A32HostServiceDisposition::Handled;
        }

        A32JniMemberKind kind = A32JniMemberKind::InstanceMethod;
        if (svc_immediate == kA32JniGetStaticMethodIdSvcImmediate) {
            kind = A32JniMemberKind::StaticMethod;
        } else if (svc_immediate == kA32JniGetFieldIdSvcImmediate) {
            kind = A32JniMemberKind::InstanceField;
        } else if (
            svc_immediate ==
            kA32JniGetStaticFieldIdSvcImmediate) {
            kind = A32JniMemberKind::StaticField;
        }
        const A32JniMemberId* member =
            registry_->find_member(
                regs[1],
                kind,
                name,
                signature);
        regs[0] = member != nullptr ? member->handle : 0U;
        return runtime::A32HostServiceDisposition::Handled;
    }

    if (registry_ == nullptr) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }
    if (!registry_->valid()) {
        return runtime::A32HostServiceDisposition::Failed;
    }
    if (!registry_->contains_class_handle(regs[1])) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    const std::int32_t signed_count =
        static_cast<std::int32_t>(regs[3]);
    if (signed_count < 0) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }
    const std::size_t count =
        static_cast<std::size_t>(signed_count);
    if (count == 0U) {
        regs[0] = jint_bits(kA32JniOk);
        return runtime::A32HostServiceDisposition::Handled;
    }
    const A32JniRegistryLimits limits = registry_->limits();
    if (count > limits.max_methods_per_registration ||
        regs[2] == 0U) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    const std::uint64_t array_end =
        static_cast<std::uint64_t>(regs[2]) +
        static_cast<std::uint64_t>(count) *
            kJniNativeMethodBytes;
    if (array_end > (std::uint64_t{1} << 32U)) {
        regs[0] = jint_bits(kA32JniErr);
        return runtime::A32HostServiceDisposition::Handled;
    }

    std::vector<A32JniRegisteredNative> pending;
    pending.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint64_t record64 =
            static_cast<std::uint64_t>(regs[2]) +
            index * kJniNativeMethodBytes;
        const auto record =
            static_cast<std::uint32_t>(record64);

        std::uint32_t name_pointer{};
        std::uint32_t signature_pointer{};
        std::uint32_t function{};
        if (!read_guest_u32(
                memory, record, name_pointer) ||
            !read_guest_u32(
                memory, record + 4U, signature_pointer) ||
            !read_guest_u32(
                memory, record + 8U, function)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (name_pointer == 0U ||
            signature_pointer == 0U ||
            function == 0U ||
            function ==
                std::numeric_limits<std::uint32_t>::max()) {
            regs[0] = jint_bits(kA32JniErr);
            return runtime::A32HostServiceDisposition::Handled;
        }

        A32JniRegisteredNative method;
        method.class_handle = regs[1];
        method.function = function;
        if (!read_guest_c_string(
                memory,
                name_pointer,
                limits.max_method_name_bytes,
                method.name) ||
            !read_guest_c_string(
                memory,
                signature_pointer,
                limits.max_signature_bytes,
                method.signature)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
        if (method.name.empty() ||
            method.signature.empty()) {
            regs[0] = jint_bits(kA32JniErr);
            return runtime::A32HostServiceDisposition::Handled;
        }
        pending.push_back(std::move(method));
    }

    const A32JniRegistryError registered =
        registry_->register_natives(
            regs[1],
            std::move(pending));
    regs[0] = jint_bits(
        registered == A32JniRegistryError::None
            ? kA32JniOk
            : kA32JniErr);
    return runtime::A32HostServiceDisposition::Handled;
}

A32JniOnLoadResult invoke_a32_jni_on_load(
    memory::GuestMemory& memory,
    const elf::Elf32DependencyGraph& graph,
    std::size_t object_index,
    std::uint32_t java_vm_address,
    runtime::A32HostServiceHandler& service_handler,
    const A32JniOnLoadOptions& options) {
    if (java_vm_address == 0U ||
        (java_vm_address & 3U) != 0U ||
        options.stack_top == 0U ||
        (options.stack_top & 7U) != 0U ||
        (options.return_pc & 3U) != 0U ||
        options.max_instructions == 0U ||
        options.max_service_calls == 0U) {
        return onload_failure(
            A32JniOnLoadError::InvalidOptions);
    }
    if (object_index >= graph.objects.size()) {
        return onload_failure(
            A32JniOnLoadError::InvalidObject);
    }

    const auto& object = graph.objects[object_index];
    const auto index = elf::build_elf32_symbol_index(
        memory,
        object.linker_metadata,
        options.symbols);
    if (!index) {
        auto result = onload_failure(
            A32JniOnLoadError::SymbolIndexFailed);
        result.index_error = index.error;
        return result;
    }

    const auto lookup = elf::lookup_elf32_symbol(
        memory,
        object.load.load_bias,
        object.linker_metadata,
        index.index,
        "JNI_OnLoad",
        options.symbols);
    if (!lookup) {
        auto result = onload_failure(
            A32JniOnLoadError::SymbolLookupFailed);
        result.lookup_error = lookup.error;
        result.string_error = lookup.string_error;
        return result;
    }

    constexpr std::uint8_t kSttFunc = 2U;
    const std::uint32_t function = lookup.symbol.guest_value;
    const bool thumb = (function & 1U) != 0U;
    const std::uint32_t entry_pc = function & ~1U;
    if (lookup.symbol.symbol.type != kSttFunc ||
        function == 0U ||
        function == std::numeric_limits<std::uint32_t>::max() ||
        entry_pc == options.return_pc ||
        (!thumb && (entry_pc & 3U) != 0U)) {
        return onload_failure(
            A32JniOnLoadError::InvalidFunctionAddress);
    }

    cpu::ExecutionRequest request{};
    request.instruction_set =
        thumb ? cpu::InstructionSet::Thumb
              : cpu::InstructionSet::Arm;
    request.entry_pc = entry_pc;
    request.regs[0] = java_vm_address;
    request.regs[1] = 0U;
    request.regs[13] = options.stack_top;
    request.regs[14] =
        options.return_pc | (thumb ? 1U : 0U);
    request.instruction_count = options.max_instructions;
    request.stop_pc = options.return_pc;

    JniExecutionContextScope context_scope{
        options.execution_context,
        object_index};

    auto execution = runtime::execute_a32_with_services(
        memory,
        request,
        service_handler,
        options.max_service_calls);

    A32JniOnLoadResult result;
    result.execution = execution;
    if (execution.service_suspended) {
        result.error = A32JniOnLoadError::ServiceSuspended;
        result.failing_svc_immediate =
            execution.suspended_svc_immediate;
        return result;
    }

    switch (execution.error) {
    case runtime::A32ServiceDispatchError::None:
        break;
    case runtime::A32ServiceDispatchError::MemoryFault:
        result.error = A32JniOnLoadError::MemoryFault;
        break;
    case runtime::A32ServiceDispatchError::CpuException:
        result.error = A32JniOnLoadError::CpuException;
        break;
    case runtime::A32ServiceDispatchError::ServiceLimitExceeded:
        result.error = A32JniOnLoadError::ServiceLimitExceeded;
        break;
    case runtime::A32ServiceDispatchError::ServiceUnhandled:
        result.error = A32JniOnLoadError::ServiceUnhandled;
        break;
    case runtime::A32ServiceDispatchError::ServiceFailed:
        result.error = A32JniOnLoadError::ServiceFailed;
        break;
    case runtime::A32ServiceDispatchError::InstructionLimitExceeded:
        result.error = A32JniOnLoadError::InstructionLimitExceeded;
        break;
    }
    if (result.error != A32JniOnLoadError::None) {
        result.failing_svc_immediate =
            execution.failing_svc_immediate;
        return result;
    }
    if (!execution.stop_pc_reached) {
        result.error =
            A32JniOnLoadError::InstructionLimitExceeded;
        return result;
    }

    result.returned_version = execution.regs[0];
    if (!is_a32_supported_jni_onload_version(
            result.returned_version)) {
        result.error =
            A32JniOnLoadError::UnsupportedVersion;
    }
    return result;
}

A32JniNativeInvokeResult
invoke_a32_registered_native_noargs(
    memory::GuestMemory& memory,
    const A32JniClassRegistry& registry,
    std::uint32_t class_handle,
    std::string_view name,
    std::string_view signature,
    std::uint32_t jni_env_address,
    std::uint32_t receiver_or_class,
    runtime::A32HostServiceHandler& service_handler,
    const A32JniNativeInvokeOptions& options) {
    if (!registry.valid() ||
        class_handle == 0U ||
        jni_env_address == 0U ||
        (jni_env_address & 3U) != 0U ||
        receiver_or_class == 0U ||
        options.stack_top == 0U ||
        (options.stack_top & 7U) != 0U ||
        (options.return_pc & 3U) != 0U ||
        options.max_instructions == 0U ||
        options.max_service_calls == 0U) {
        return native_invoke_failure(
            A32JniNativeInvokeError::InvalidOptions);
    }
    if (!registry.contains_class_handle(class_handle)) {
        return native_invoke_failure(
            A32JniNativeInvokeError::ClassNotFound);
    }

    const A32JniRegisteredNative* method =
        registry.find_native(
            class_handle,
            name,
            signature);
    if (method == nullptr) {
        return native_invoke_failure(
            A32JniNativeInvokeError::NativeNotFound);
    }
    if (!is_zero_argument_signature(method->signature)) {
        return native_invoke_failure(
            A32JniNativeInvokeError::NonZeroArgumentSignature);
    }

    const std::uint32_t function = method->function;
    const bool thumb = (function & 1U) != 0U;
    const std::uint32_t entry_pc = function & ~1U;
    if (function == 0U ||
        function == std::numeric_limits<std::uint32_t>::max() ||
        entry_pc == options.return_pc ||
        (!thumb && (entry_pc & 3U) != 0U)) {
        return native_invoke_failure(
            A32JniNativeInvokeError::InvalidFunctionAddress);
    }

    cpu::ExecutionRequest request{};
    request.instruction_set =
        thumb ? cpu::InstructionSet::Thumb
              : cpu::InstructionSet::Arm;
    request.entry_pc = entry_pc;
    request.regs[0] = jni_env_address;
    request.regs[1] = receiver_or_class;
    request.regs[13] = options.stack_top;
    request.regs[14] =
        options.return_pc | (thumb ? 1U : 0U);
    request.instruction_count = options.max_instructions;
    request.stop_pc = options.return_pc;

    auto execution = runtime::execute_a32_with_services(
        memory,
        request,
        service_handler,
        options.max_service_calls);

    A32JniNativeInvokeResult result;
    result.function = function;
    result.execution = execution;
    if (execution.service_suspended) {
        result.error =
            A32JniNativeInvokeError::ServiceSuspended;
        result.failing_svc_immediate =
            execution.suspended_svc_immediate;
        return result;
    }

    switch (execution.error) {
    case runtime::A32ServiceDispatchError::None:
        break;
    case runtime::A32ServiceDispatchError::MemoryFault:
        result.error = A32JniNativeInvokeError::MemoryFault;
        break;
    case runtime::A32ServiceDispatchError::CpuException:
        result.error = A32JniNativeInvokeError::CpuException;
        break;
    case runtime::A32ServiceDispatchError::ServiceLimitExceeded:
        result.error =
            A32JniNativeInvokeError::ServiceLimitExceeded;
        break;
    case runtime::A32ServiceDispatchError::ServiceUnhandled:
        result.error =
            A32JniNativeInvokeError::ServiceUnhandled;
        break;
    case runtime::A32ServiceDispatchError::ServiceFailed:
        result.error = A32JniNativeInvokeError::ServiceFailed;
        break;
    case runtime::A32ServiceDispatchError::InstructionLimitExceeded:
        result.error =
            A32JniNativeInvokeError::InstructionLimitExceeded;
        break;
    }
    if (result.error != A32JniNativeInvokeError::None) {
        result.failing_svc_immediate =
            execution.failing_svc_immediate;
        return result;
    }
    if (!execution.stop_pc_reached) {
        result.error =
            A32JniNativeInvokeError::InstructionLimitExceeded;
        return result;
    }

    result.returned_value = execution.regs[0];
    return result;
}

const char* to_string(
    A32JniRegistryError error) noexcept {
    switch (error) {
    case A32JniRegistryError::None:
        return "none";
    case A32JniRegistryError::InvalidLimits:
        return "invalid_limits";
    case A32JniRegistryError::InvalidClassHandle:
        return "invalid_class_handle";
    case A32JniRegistryError::InvalidName:
        return "invalid_name";
    case A32JniRegistryError::ClassLimitExceeded:
        return "class_limit_exceeded";
    case A32JniRegistryError::DuplicateClassName:
        return "duplicate_class_name";
    case A32JniRegistryError::DuplicateClassHandle:
        return "duplicate_class_handle";
    case A32JniRegistryError::UnknownClass:
        return "unknown_class";
    case A32JniRegistryError::InvalidMethod:
        return "invalid_method";
    case A32JniRegistryError::MethodLimitExceeded:
        return "method_limit_exceeded";
    case A32JniRegistryError::DuplicateMethod:
        return "duplicate_method";
    case A32JniRegistryError::InvalidMemberHandle:
        return "invalid_member_handle";
    case A32JniRegistryError::InvalidMemberKind:
        return "invalid_member_kind";
    case A32JniRegistryError::MemberLimitExceeded:
        return "member_limit_exceeded";
    case A32JniRegistryError::DuplicateMemberHandle:
        return "duplicate_member_handle";
    case A32JniRegistryError::DuplicateMember:
        return "duplicate_member";
    case A32JniRegistryError::InvalidReferenceHandle:
        return "invalid_reference_handle";
    case A32JniRegistryError::ReferenceLimitExceeded:
        return "reference_limit_exceeded";
    case A32JniRegistryError::DuplicateReferenceHandle:
        return "duplicate_reference_handle";
    case A32JniRegistryError::ReferenceCountExceeded:
        return "reference_count_exceeded";
    case A32JniRegistryError::InvalidArrayHandle:
        return "invalid_array_handle";
    case A32JniRegistryError::InvalidArrayLength:
        return "invalid_array_length";
    case A32JniRegistryError::ArrayLimitExceeded:
        return "array_limit_exceeded";
    case A32JniRegistryError::DuplicateArrayHandle:
        return "duplicate_array_handle";
    case A32JniRegistryError::InvalidStringValue:
        return "invalid_string_value";
    case A32JniRegistryError::StringLimitExceeded:
        return "string_limit_exceeded";
    case A32JniRegistryError::StringHandleExhausted:
        return "string_handle_exhausted";
    case A32JniRegistryError::ArrayHandleExhausted:
        return "array_handle_exhausted";
    case A32JniRegistryError::ExceptionPending:
        return "exception_pending";
    }
    return "unknown";
}

const char* to_string(
    A32JniVmInstallError error) noexcept {
    switch (error) {
    case A32JniVmInstallError::None:
        return "none";
    case A32JniVmInstallError::InvalidLayout:
        return "invalid_layout";
    case A32JniVmInstallError::SnapshotFailed:
        return "snapshot_failed";
    case A32JniVmInstallError::WriteFailed:
        return "write_failed";
    case A32JniVmInstallError::RollbackFailed:
        return "rollback_failed";
    }
    return "unknown";
}

const char* to_string(
    A32JniOnLoadError error) noexcept {
    switch (error) {
    case A32JniOnLoadError::None:
        return "none";
    case A32JniOnLoadError::InvalidOptions:
        return "invalid_options";
    case A32JniOnLoadError::InvalidObject:
        return "invalid_object";
    case A32JniOnLoadError::SymbolIndexFailed:
        return "symbol_index_failed";
    case A32JniOnLoadError::SymbolLookupFailed:
        return "symbol_lookup_failed";
    case A32JniOnLoadError::InvalidFunctionAddress:
        return "invalid_function_address";
    case A32JniOnLoadError::MemoryFault:
        return "memory_fault";
    case A32JniOnLoadError::CpuException:
        return "cpu_exception";
    case A32JniOnLoadError::ServiceLimitExceeded:
        return "service_limit_exceeded";
    case A32JniOnLoadError::ServiceUnhandled:
        return "service_unhandled";
    case A32JniOnLoadError::ServiceFailed:
        return "service_failed";
    case A32JniOnLoadError::ServiceSuspended:
        return "service_suspended";
    case A32JniOnLoadError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    case A32JniOnLoadError::UnsupportedVersion:
        return "unsupported_version";
    }
    return "unknown";
}

const char* to_string(
    A32JniNativeInvokeError error) noexcept {
    switch (error) {
    case A32JniNativeInvokeError::None:
        return "none";
    case A32JniNativeInvokeError::InvalidOptions:
        return "invalid_options";
    case A32JniNativeInvokeError::ClassNotFound:
        return "class_not_found";
    case A32JniNativeInvokeError::NativeNotFound:
        return "native_not_found";
    case A32JniNativeInvokeError::NonZeroArgumentSignature:
        return "nonzero_argument_signature";
    case A32JniNativeInvokeError::InvalidFunctionAddress:
        return "invalid_function_address";
    case A32JniNativeInvokeError::MemoryFault:
        return "memory_fault";
    case A32JniNativeInvokeError::CpuException:
        return "cpu_exception";
    case A32JniNativeInvokeError::ServiceLimitExceeded:
        return "service_limit_exceeded";
    case A32JniNativeInvokeError::ServiceUnhandled:
        return "service_unhandled";
    case A32JniNativeInvokeError::ServiceFailed:
        return "service_failed";
    case A32JniNativeInvokeError::ServiceSuspended:
        return "service_suspended";
    case A32JniNativeInvokeError::InstructionLimitExceeded:
        return "instruction_limit_exceeded";
    }
    return "unknown";
}

}  // namespace liba32android::compat
