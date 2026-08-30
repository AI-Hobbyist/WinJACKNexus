#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace WinJACKNexus::Common {

WINJACK_COMMON_API const char* libraryVersion() noexcept;

struct WINJACK_COMMON_API VersionInfo final {
	std::uint32_t major {};
	std::uint32_t minor {};
	std::uint32_t patch {};

	VersionInfo() noexcept = default;
	VersionInfo(std::uint32_t majorVersion,
				std::uint32_t minorVersion,
				std::uint32_t patchVersion,
				std::string_view gitHashValue = {}) noexcept;

	std::string toString() const;
	std::string gitHashString() const;
	static std::optional<VersionInfo> fromString(std::string_view value);

	bool operator==(const VersionInfo&) const noexcept = default;

private:
	std::array<char, 65> gitHash_ {};
};

}