#include <util.hpp>
#include <pch.hpp>

namespace util {

uint64_t seconds_since_epoch() {
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}
