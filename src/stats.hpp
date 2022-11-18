#include <map>
#include <cstdint>
#include <string>

namespace stats {
	struct torrent {
		uint64_t last_recounting_time;
		uint32_t number_of_seeders;
	};

	void try_load();

	void try_save();

	const std::map<std::string, torrent> &get();

	void set(std::map<std::string, torrent> &&stats);

	void set(const std::string &name, uint64_t last_recounting_time, uint32_t number_of_seeders);

	void remove(const std::string &name);

    extern std::map<std::string, torrent> _stats;
	extern bool _stats_changed;
	extern uint64_t _last_save_time;
}
