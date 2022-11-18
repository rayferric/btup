#include <cstdint>
#include <filesystem>

namespace config {
	// Returns true or throws if should terminate
	bool init(int argc, char **argv);

    uint64_t update_delay();

    uint32_t min_seeders_to_ignore();

    uint64_t min_age_to_recount_seeds();

	uint32_t max_parallel_recounts();

	const std::filesystem::path &torrents_dir();

	const std::filesystem::path &data_dir();

	const std::filesystem::path &tmp_dir();

	uint64_t max_data_size();

	bool verbose();

    extern uint64_t _update_delay; // Discover new torrents, unregister those that were deleted and purge unused data every 10 minutes
	extern uint32_t _min_seeders_to_ignore; // Do not seed torrents with 3 or more seeds
	extern uint64_t _min_age_to_recount_seeds; // Recount seeders for torrents every 24 hours
	extern uint32_t _max_parallel_recounts; // Only ever recount seeders for 100 torrents simultaneously
	extern std::filesystem::path _torrents_dir;
	extern std::filesystem::path _data_dir;
	extern std::filesystem::path _tmp_dir;
	extern uint64_t _max_data_size; // Never allow data directory size to exceed 100 GiB space limit.
	extern bool _verbose;
}
