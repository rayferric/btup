#include <stats.hpp>
#include <pch.hpp>

#include <util.hpp>

namespace stats {
	void try_load() {
		if (std::filesystem::exists("stats.txt")) {
			std::cout << "Recovering torrent statistics from previous session." << std::endl;

			std::ifstream stats_file("stats.txt");
			std::string line;

			std::getline(stats_file, line); // Skip header
			while (!stats_file.eof()) {
				std::getline(stats_file, line);
				if (line.empty())
					continue;

				std::stringstream ss(line);
				std::string name;
				std::string last_recounting_time;
				std::string number_of_seeders;
				
				std::getline(ss, name, '/');
				std::getline(ss, last_recounting_time, '/');
				std::getline(ss, number_of_seeders, '/');

				_stats[name] = torrent{std::stoull(last_recounting_time), static_cast<uint32_t>(std::stoul(number_of_seeders))};
			}
		}
	}

	void try_save() {
		if (_stats_changed && util::seconds_since_epoch() - _last_save_time > 30) {
			std::cout << "Saving statistics." << std::endl;
			
			std::ofstream stats_file("stats.txt");
			stats_file << "torrent-name/last-recounting-time/number-of-seeders" << std::endl; // Add the header
			for (const auto &[name, torrent] : _stats)
				stats_file << name << '/' << torrent.last_recounting_time << '/' << torrent.number_of_seeders << std::endl;

			_stats_changed = false;
			_last_save_time = util::seconds_since_epoch();
		}
	}

	const std::map<std::string, torrent> &get() {
		return _stats;
	}

	void set(std::map<std::string, torrent> &&stats) {
		_stats = std::move(stats);
		_stats_changed = true;
	}

	void set(const std::string &name, uint64_t last_recounting_time, uint32_t number_of_seeders) {
		_stats[name] = torrent{last_recounting_time, number_of_seeders};
		_stats_changed = true;
	}

	void remove(const std::string &name) {
		_stats.erase(name);
		_stats_changed = true;
	}

    std::map<std::string, torrent> _stats;
	bool _stats_changed = false;
	uint64_t _last_save_time = 0;
}
