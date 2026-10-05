#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "runtime/a32_service_dispatch.h"

namespace liba32android::compat {

struct A32AndroidLogWriteOptions {
    // Maximum payload bytes, excluding the required terminating NUL.
    std::size_t max_tag_bytes{};
    std::size_t max_text_bytes{};
};

struct A32AndroidLogFormatOptions {
    // Every guest-controlled byte/argument source is explicitly bounded.
    std::size_t max_tag_bytes{};
    std::size_t max_format_bytes{};
    std::size_t max_output_bytes{};
    std::size_t max_string_argument_bytes{};
    std::size_t max_arguments{};
    std::size_t max_field_width{};
    std::size_t max_precision{};
};

class A32AndroidLogSink {
public:
    virtual ~A32AndroidLogSink() = default;

    // tag == std::nullopt represents a guest null tag pointer.
    [[nodiscard]] virtual std::int32_t write(
        std::int32_t priority,
        std::optional<std::string_view> tag,
        std::string_view text) = 0;
};

class A32AndroidLogWriteService final
    : public runtime::A32HostServiceHandler {
public:
    A32AndroidLogWriteService(
        std::uint32_t svc_immediate,
        A32AndroidLogSink& sink,
        A32AndroidLogWriteOptions options) noexcept
        : svc_immediate_(svc_immediate), sink_(sink), options_(options) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

    [[nodiscard]] std::uint32_t svc_immediate() const noexcept {
        return svc_immediate_;
    }

private:
    std::uint32_t svc_immediate_{};
    A32AndroidLogSink& sink_;
    A32AndroidLogWriteOptions options_;
};

class A32AndroidLogPrintService final
    : public runtime::A32HostServiceHandler {
public:
    A32AndroidLogPrintService(
        std::uint32_t print_svc_immediate,
        std::uint32_t vprint_svc_immediate,
        A32AndroidLogSink& sink,
        A32AndroidLogFormatOptions options) noexcept
        : print_svc_immediate_(print_svc_immediate),
          vprint_svc_immediate_(vprint_svc_immediate),
          sink_(sink),
          options_(options) {}

    [[nodiscard]] runtime::A32HostServiceDisposition handle(
        memory::GuestMemory& memory,
        std::uint32_t svc_immediate,
        std::array<std::uint32_t, 16>& regs,
        std::uint32_t& cpsr) override;

private:
    std::uint32_t print_svc_immediate_{};
    std::uint32_t vprint_svc_immediate_{};
    A32AndroidLogSink& sink_;
    A32AndroidLogFormatOptions options_;
};

}  // namespace liba32android::compat
