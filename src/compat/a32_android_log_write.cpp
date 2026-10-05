#include "compat/a32_android_log_write.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "memory/guest_memory.h"

namespace liba32android::compat {
namespace {

[[nodiscard]] bool read_guest_c_string(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::size_t max_payload_bytes,
    std::string& output) {
    output.clear();

    for (std::size_t offset = 0;; ++offset) {
        if (offset >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max() - address)) {
            return false;
        }

        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(address + static_cast<std::uint32_t>(offset), byte)) {
            return false;
        }

        if (byte[0] == 0) {
            return true;
        }
        if (offset == max_payload_bytes) {
            return false;
        }

        output.push_back(static_cast<char>(byte[0]));
    }
}

[[nodiscard]] bool read_guest_format_string(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::size_t max_payload_bytes,
    std::optional<std::size_t> precision,
    std::string& output) {
    output.clear();
    const std::size_t limit =
        precision.has_value()
            ? std::min(max_payload_bytes, *precision)
            : max_payload_bytes;

    for (std::size_t offset = 0;; ++offset) {
        if (precision.has_value() && offset == limit) {
            return true;
        }
        if (offset >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max() - address)) {
            return false;
        }

        std::array<std::uint8_t, 1> byte{};
        if (!memory.read(address + static_cast<std::uint32_t>(offset), byte)) {
            return false;
        }
        if (byte[0] == 0) {
            return true;
        }
        if (offset == max_payload_bytes) {
            return false;
        }
        output.push_back(static_cast<char>(byte[0]));
    }
}

[[nodiscard]] bool align_guest_address(
    std::uint32_t address,
    std::uint32_t alignment,
    std::uint32_t& aligned) noexcept {
    const std::uint64_t value =
        (static_cast<std::uint64_t>(address) + alignment - 1U) &
        ~(static_cast<std::uint64_t>(alignment) - 1U);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    aligned = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool read_u32_le(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint32_t& value) {
    if (address > std::numeric_limits<std::uint32_t>::max() - 3U) {
        return false;
    }
    std::array<std::uint8_t, 4> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

[[nodiscard]] bool read_u64_le(
    const memory::GuestMemory& memory,
    std::uint32_t address,
    std::uint64_t& value) {
    if (address > std::numeric_limits<std::uint32_t>::max() - 7U) {
        return false;
    }
    std::array<std::uint8_t, 8> bytes{};
    if (!memory.read(address, bytes)) return false;
    value = 0U;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return true;
}

class GuestVarargReader {
public:
    GuestVarargReader(
        const memory::GuestMemory& memory,
        const std::array<std::uint32_t, 16>& regs,
        bool va_list_mode) noexcept
        : memory_(memory),
          regs_(regs),
          memory_only_(va_list_mode),
          register_index_(3U),
          memory_address_(va_list_mode ? regs[3] : regs[13]) {}

    [[nodiscard]] bool read_u32(std::uint32_t& value) {
        if (!memory_only_ && register_index_ < 4U) {
            value = regs_[register_index_++];
            return true;
        }

        std::uint32_t address{};
        if (!align_guest_address(memory_address_, 4U, address) ||
            !read_u32_le(memory_, address, value) ||
            address > std::numeric_limits<std::uint32_t>::max() - 4U) {
            return false;
        }
        memory_address_ = address + 4U;
        register_index_ = 4U;
        return true;
    }

    [[nodiscard]] bool read_u64(std::uint64_t& value) {
        if (!memory_only_ && register_index_ < 4U) {
            if ((register_index_ & 1U) != 0U) {
                ++register_index_;
            }
            if (register_index_ <= 2U) {
                value = static_cast<std::uint64_t>(regs_[register_index_]) |
                        (static_cast<std::uint64_t>(
                             regs_[register_index_ + 1U]) << 32U);
                register_index_ += 2U;
                return true;
            }
        }

        std::uint32_t address{};
        if (!align_guest_address(memory_address_, 8U, address) ||
            !read_u64_le(memory_, address, value) ||
            address > std::numeric_limits<std::uint32_t>::max() - 8U) {
            return false;
        }
        memory_address_ = address + 8U;
        register_index_ = 4U;
        return true;
    }

private:
    const memory::GuestMemory& memory_;
    const std::array<std::uint32_t, 16>& regs_;
    bool memory_only_{};
    std::uint32_t register_index_{};
    std::uint32_t memory_address_{};
};

[[nodiscard]] bool append_bounded(
    std::string& output,
    std::string_view piece,
    std::size_t max_output_bytes) {
    if (piece.size() > max_output_bytes - std::min(output.size(), max_output_bytes)) {
        return false;
    }
    output.append(piece.data(), piece.size());
    return output.size() <= max_output_bytes;
}

[[nodiscard]] bool append_padding(
    std::string& output,
    std::size_t count,
    char value,
    std::size_t max_output_bytes) {
    if (count > max_output_bytes - std::min(output.size(), max_output_bytes)) {
        return false;
    }
    output.append(count, value);
    return output.size() <= max_output_bytes;
}

struct ParsedFormat {
    bool left{};
    bool plus{};
    bool space{};
    bool alternate{};
    bool zero{};
    std::optional<std::size_t> width;
    std::optional<std::size_t> precision;
    std::string length;
    char conversion{};
};

[[nodiscard]] bool consume_argument(
    std::size_t& count,
    std::size_t max_arguments) noexcept {
    if (count >= max_arguments) return false;
    ++count;
    return true;
}

[[nodiscard]] bool parse_decimal_limit(
    std::string_view format,
    std::size_t& index,
    std::size_t limit,
    std::size_t& value) noexcept {
    value = 0U;
    bool any = false;
    while (index < format.size() &&
           format[index] >= '0' && format[index] <= '9') {
        any = true;
        const std::size_t digit =
            static_cast<std::size_t>(format[index] - '0');
        if (value > (limit - std::min(limit, digit)) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        if (value > limit) return false;
        ++index;
    }
    return any;
}

[[nodiscard]] bool read_dynamic_field(
    GuestVarargReader& reader,
    std::size_t& argument_count,
    std::size_t max_arguments,
    std::size_t limit,
    bool precision,
    std::optional<std::size_t>& value,
    bool& left) {
    if (!consume_argument(argument_count, max_arguments)) return false;
    std::uint32_t raw{};
    if (!reader.read_u32(raw)) return false;
    const std::int32_t signed_value = std::bit_cast<std::int32_t>(raw);
    if (precision && signed_value < 0) {
        value.reset();
        return true;
    }
    if (signed_value == std::numeric_limits<std::int32_t>::min()) {
        return false;
    }
    std::int64_t magnitude = signed_value;
    if (magnitude < 0) {
        magnitude = -magnitude;
        if (!precision) left = true;
    }
    if (static_cast<std::uint64_t>(magnitude) > limit) return false;
    value = static_cast<std::size_t>(magnitude);
    return true;
}

[[nodiscard]] bool parse_format(
    std::string_view format,
    std::size_t& index,
    GuestVarargReader& reader,
    std::size_t& argument_count,
    const A32AndroidLogFormatOptions& options,
    ParsedFormat& parsed) {
    parsed = {};

    bool parsing_flags = true;
    while (parsing_flags && index < format.size()) {
        switch (format[index]) {
        case '-': parsed.left = true; ++index; break;
        case '+': parsed.plus = true; ++index; break;
        case ' ': parsed.space = true; ++index; break;
        case '#': parsed.alternate = true; ++index; break;
        case '0': parsed.zero = true; ++index; break;
        default: parsing_flags = false; break;
        }
    }

    if (index < format.size() && format[index] == '*') {
        ++index;
        if (!read_dynamic_field(
                reader,
                argument_count,
                options.max_arguments,
                options.max_field_width,
                false,
                parsed.width,
                parsed.left)) {
            return false;
        }
    } else {
        const std::size_t width_start = index;
        std::size_t width{};
        if (parse_decimal_limit(
                format, index, options.max_field_width, width)) {
            parsed.width = width;
        } else if (index != width_start) {
            return false;
        }
    }

    if (index < format.size() && format[index] == '.') {
        ++index;
        if (index < format.size() && format[index] == '*') {
            ++index;
            if (!read_dynamic_field(
                    reader,
                    argument_count,
                    options.max_arguments,
                    options.max_precision,
                    true,
                    parsed.precision,
                    parsed.left)) {
                return false;
            }
        } else {
            const std::size_t precision_start = index;
            std::size_t precision_value{};
            if (parse_decimal_limit(
                    format,
                    index,
                    options.max_precision,
                    precision_value)) {
                parsed.precision = precision_value;
            } else if (index == precision_start) {
                parsed.precision = 0U;
            } else {
                return false;
            }
        }
    }

    if (index + 1U < format.size() &&
        ((format[index] == 'h' && format[index + 1U] == 'h') ||
         (format[index] == 'l' && format[index + 1U] == 'l'))) {
        parsed.length.assign(format.substr(index, 2U));
        index += 2U;
    } else if (index < format.size() &&
               (format[index] == 'h' || format[index] == 'l' ||
                format[index] == 'j' || format[index] == 'z' ||
                format[index] == 't' || format[index] == 'L')) {
        parsed.length.assign(1U, format[index]);
        ++index;
    }

    if (index >= format.size()) return false;
    parsed.conversion = format[index++];
    return true;
}

[[nodiscard]] std::string canonical_flags(const ParsedFormat& parsed) {
    std::string result;
    if (parsed.left) result.push_back('-');
    if (parsed.plus) result.push_back('+');
    if (parsed.space) result.push_back(' ');
    if (parsed.alternate) result.push_back('#');
    if (parsed.zero) result.push_back('0');
    return result;
}

[[nodiscard]] std::string canonical_prefix(const ParsedFormat& parsed) {
    std::string spec{"%"};
    spec += canonical_flags(parsed);
    if (parsed.width.has_value()) {
        spec += std::to_string(*parsed.width);
    }
    if (parsed.precision.has_value()) {
        spec.push_back('.');
        spec += std::to_string(*parsed.precision);
    }
    return spec;
}

template <typename T>
[[nodiscard]] bool append_snprintf(
    std::string& output,
    std::size_t max_output_bytes,
    const std::string& spec,
    T value) {
    const int needed = std::snprintf(nullptr, 0, spec.c_str(), value);
    if (needed < 0) return false;
    const auto needed_size = static_cast<std::size_t>(needed);
    if (needed_size >
        max_output_bytes - std::min(output.size(), max_output_bytes)) {
        return false;
    }

    std::string temporary(needed_size + 1U, '\0');
    const int written = std::snprintf(
        temporary.data(), temporary.size(), spec.c_str(), value);
    if (written != needed) return false;
    return append_bounded(
        output,
        std::string_view{temporary.data(), needed_size},
        max_output_bytes);
}

[[nodiscard]] bool append_string_field(
    std::string& output,
    std::string_view value,
    const ParsedFormat& parsed,
    std::size_t max_output_bytes) {
    if (parsed.plus || parsed.space || parsed.alternate || parsed.zero ||
        !parsed.length.empty()) {
        return false;
    }

    if (parsed.precision.has_value() &&
        value.size() > *parsed.precision) {
        value = value.substr(0, *parsed.precision);
    }

    const std::size_t width = parsed.width.value_or(0U);
    const std::size_t padding = width > value.size()
        ? width - value.size()
        : 0U;
    if (!parsed.left &&
        !append_padding(output, padding, ' ', max_output_bytes)) {
        return false;
    }
    if (!append_bounded(output, value, max_output_bytes)) return false;
    if (parsed.left &&
        !append_padding(output, padding, ' ', max_output_bytes)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool append_pointer_field(
    std::string& output,
    std::uint32_t guest_pointer,
    const ParsedFormat& parsed,
    std::size_t max_output_bytes) {
    if (parsed.plus || parsed.space || parsed.alternate ||
        parsed.precision.has_value() || !parsed.length.empty()) {
        return false;
    }

    std::array<char, 8> digits{};
    const auto converted = std::to_chars(
        digits.data(), digits.data() + digits.size(), guest_pointer, 16);
    if (converted.ec != std::errc{}) return false;
    std::string piece{"0x"};
    piece.append(digits.data(), converted.ptr);

    const std::size_t width = parsed.width.value_or(0U);
    const std::size_t padding = width > piece.size()
        ? width - piece.size()
        : 0U;
    if (!parsed.left) {
        if (parsed.zero && padding != 0U) {
            if (!append_bounded(output, "0x", max_output_bytes) ||
                !append_padding(output, padding, '0', max_output_bytes) ||
                !append_bounded(
                    output,
                    std::string_view{digits.data(),
                                     static_cast<std::size_t>(
                                         converted.ptr - digits.data())},
                    max_output_bytes)) {
                return false;
            }
            return true;
        }
        if (!append_padding(output, padding, ' ', max_output_bytes)) {
            return false;
        }
    }
    if (!append_bounded(output, piece, max_output_bytes)) return false;
    if (parsed.left &&
        !append_padding(output, padding, ' ', max_output_bytes)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool read_signed_integer(
    GuestVarargReader& reader,
    std::string_view length,
    std::int64_t& value) {
    if (length == "ll" || length == "j") {
        std::uint64_t raw{};
        if (!reader.read_u64(raw)) return false;
        value = std::bit_cast<std::int64_t>(raw);
        return true;
    }

    if (length == "L") return false;
    std::uint32_t raw{};
    if (!reader.read_u32(raw)) return false;
    if (length == "hh") {
        value = static_cast<std::int8_t>(raw);
    } else if (length == "h") {
        value = static_cast<std::int16_t>(raw);
    } else {
        value = std::bit_cast<std::int32_t>(raw);
    }
    return true;
}

[[nodiscard]] bool read_unsigned_integer(
    GuestVarargReader& reader,
    std::string_view length,
    std::uint64_t& value) {
    if (length == "ll" || length == "j") {
        return reader.read_u64(value);
    }
    if (length == "L") return false;
    std::uint32_t raw{};
    if (!reader.read_u32(raw)) return false;
    if (length == "hh") {
        value = static_cast<std::uint8_t>(raw);
    } else if (length == "h") {
        value = static_cast<std::uint16_t>(raw);
    } else {
        value = raw;
    }
    return true;
}

[[nodiscard]] bool format_guest_log(
    const memory::GuestMemory& memory,
    std::string_view format,
    GuestVarargReader& reader,
    const A32AndroidLogFormatOptions& options,
    std::string& output) {
    output.clear();
    std::size_t argument_count = 0U;

    for (std::size_t index = 0; index < format.size();) {
        if (format[index] != '%') {
            if (!append_bounded(
                    output,
                    std::string_view{format.data() + index, 1U},
                    options.max_output_bytes)) {
                return false;
            }
            ++index;
            continue;
        }

        ++index;
        if (index < format.size() && format[index] == '%') {
            if (!append_bounded(output, "%", options.max_output_bytes)) {
                return false;
            }
            ++index;
            continue;
        }

        ParsedFormat parsed;
        if (!parse_format(
                format,
                index,
                reader,
                argument_count,
                options,
                parsed)) {
            return false;
        }

        switch (parsed.conversion) {
        case 'd':
        case 'i': {
            if (!consume_argument(argument_count, options.max_arguments)) {
                return false;
            }
            std::int64_t value{};
            if (!read_signed_integer(reader, parsed.length, value)) {
                return false;
            }
            std::string spec = canonical_prefix(parsed);
            spec += "ll";
            spec.push_back(parsed.conversion);
            if (!append_snprintf(
                    output,
                    options.max_output_bytes,
                    spec,
                    static_cast<long long>(value))) {
                return false;
            }
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            if (!consume_argument(argument_count, options.max_arguments)) {
                return false;
            }
            std::uint64_t value{};
            if (!read_unsigned_integer(reader, parsed.length, value)) {
                return false;
            }
            std::string spec = canonical_prefix(parsed);
            spec += "ll";
            spec.push_back(parsed.conversion);
            if (!append_snprintf(
                    output,
                    options.max_output_bytes,
                    spec,
                    static_cast<unsigned long long>(value))) {
                return false;
            }
            break;
        }
        case 'c': {
            if (!parsed.length.empty() || parsed.precision.has_value() ||
                parsed.plus || parsed.space || parsed.alternate) {
                return false;
            }
            if (!consume_argument(argument_count, options.max_arguments)) {
                return false;
            }
            std::uint32_t raw{};
            if (!reader.read_u32(raw)) return false;
            std::string spec = canonical_prefix(parsed);
            spec.push_back('c');
            if (!append_snprintf(
                    output,
                    options.max_output_bytes,
                    spec,
                    static_cast<int>(raw & 0xffU))) {
                return false;
            }
            break;
        }
        case 's': {
            if (!consume_argument(argument_count, options.max_arguments)) {
                return false;
            }
            std::uint32_t address{};
            if (!reader.read_u32(address)) return false;
            if (address == 0U) {
                if (!append_string_field(
                        output, "(null)", parsed, options.max_output_bytes)) {
                    return false;
                }
                break;
            }
            std::string value;
            if (!read_guest_format_string(
                    memory,
                    address,
                    options.max_string_argument_bytes,
                    parsed.precision,
                    value) ||
                !append_string_field(
                    output,
                    value,
                    parsed,
                    options.max_output_bytes)) {
                return false;
            }
            break;
        }
        case 'p': {
            if (!consume_argument(argument_count, options.max_arguments)) {
                return false;
            }
            std::uint32_t address{};
            if (!reader.read_u32(address) ||
                !append_pointer_field(
                    output,
                    address,
                    parsed,
                    options.max_output_bytes)) {
                return false;
            }
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            if (!(parsed.length.empty() || parsed.length == "l")) {
                return false;
            }
            if (!consume_argument(argument_count, options.max_arguments)) {
                return false;
            }
            std::uint64_t raw{};
            if (!reader.read_u64(raw)) return false;
            const double value = std::bit_cast<double>(raw);
            std::string spec = canonical_prefix(parsed);
            spec.push_back(parsed.conversion);
            if (!append_snprintf(
                    output,
                    options.max_output_bytes,
                    spec,
                    value)) {
                return false;
            }
            break;
        }
        default:
            // %n and every unselected extension are rejected rather than
            // writing guest memory or delegating a guest-controlled format to
            // a host variadic function.
            return false;
        }
    }
    return true;
}

}  // namespace

runtime::A32HostServiceDisposition A32AndroidLogWriteService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    if (svc_immediate != svc_immediate_) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }

    const std::int32_t priority = std::bit_cast<std::int32_t>(regs[0]);
    const std::uint32_t tag_address = regs[1];
    const std::uint32_t text_address = regs[2];

    // Android accepts a null tag and substitutes platform/default policy.
    // A null text pointer is not a valid string argument for this bridge.
    std::optional<std::string> tag_storage;
    if (tag_address != 0) {
        tag_storage.emplace();
        if (!read_guest_c_string(
                memory, tag_address, options_.max_tag_bytes, *tag_storage)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
    }

    if (text_address == 0) {
        return runtime::A32HostServiceDisposition::Failed;
    }
    std::string text;
    if (!read_guest_c_string(
            memory, text_address, options_.max_text_bytes, text)) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    std::optional<std::string_view> tag;
    if (tag_storage.has_value()) {
        tag = std::string_view{*tag_storage};
    }

    const std::int32_t result =
        sink_.write(priority, tag, std::string_view{text});
    regs[0] = std::bit_cast<std::uint32_t>(result);
    return runtime::A32HostServiceDisposition::Handled;
}

runtime::A32HostServiceDisposition A32AndroidLogPrintService::handle(
    memory::GuestMemory& memory,
    std::uint32_t svc_immediate,
    std::array<std::uint32_t, 16>& regs,
    std::uint32_t&) {
    const bool print_call = svc_immediate == print_svc_immediate_;
    const bool vprint_call = svc_immediate == vprint_svc_immediate_;
    if (!print_call && !vprint_call) {
        return runtime::A32HostServiceDisposition::Unhandled;
    }

    const std::int32_t priority = std::bit_cast<std::int32_t>(regs[0]);
    const std::uint32_t tag_address = regs[1];
    const std::uint32_t format_address = regs[2];

    std::optional<std::string> tag_storage;
    if (tag_address != 0U) {
        tag_storage.emplace();
        if (!read_guest_c_string(
                memory,
                tag_address,
                options_.max_tag_bytes,
                *tag_storage)) {
            return runtime::A32HostServiceDisposition::Failed;
        }
    }
    if (format_address == 0U) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    std::string format;
    if (!read_guest_c_string(
            memory,
            format_address,
            options_.max_format_bytes,
            format)) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    GuestVarargReader reader{memory, regs, vprint_call};
    std::string text;
    if (!format_guest_log(memory, format, reader, options_, text)) {
        return runtime::A32HostServiceDisposition::Failed;
    }

    std::optional<std::string_view> tag;
    if (tag_storage.has_value()) {
        tag = std::string_view{*tag_storage};
    }

    const std::int32_t result =
        sink_.write(priority, tag, std::string_view{text});
    regs[0] = std::bit_cast<std::uint32_t>(result);
    return runtime::A32HostServiceDisposition::Handled;
}

}  // namespace liba32android::compat
