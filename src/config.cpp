#include <config.hpp>
#include <pch.hpp>

namespace config {
	// Returns true or throws if should terminate
	bool init(int argc, char **argv) {
		for (int i = 1; i < argc; i++) {
			std::string arg = argv[i];

			if (arg == "--help" || arg == "-h") {
				std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
				std::cout << "Options:" << std::endl;
				std::cout << "	--help (-h)                                  Show this message." << std::endl;
				std::cout << "	--version (-v)                               Show information about the program release." << std::endl;
				std::cout << "	--update-delay (-u) {seconds}                Execute actions based on the collected seeder counts every {seconds} second(s). Seeders are recounted continuously and independently of the update delay. (default: 10)" << std::endl;
				std::cout << "	--min-seeders-to-ignore (-i) {count}         Do not seed torrents with {count} or more seeders. (default: 3)" << std::endl;
				std::cout << "	--min-age-to-recount-seeders (-c) {seconds}  Recount seeders for torrents every {seconds} second(s). (default: 24 * 60 * 60)" << std::endl;
				std::cout << "	--max-parallel-recounts (-P) {count}         Only ever recount seeders for {count} torrents simultaneously. (default: 100)" << std::endl;
				std::cout << "	--torrents-dir (-t) {path}                   Path to the directory containing the torrents. (default: ./torrents)" << std::endl;
				std::cout << "	--data-dir (-D) {path}                       Path to the directory containing the torrent data. (default: ./data)" << std::endl;
				std::cout << "	--tmp-dir (-T) {path}                        Path to the directory containing temporary torrent data. Used when recounting seeders. (default: /tmp/btup/{PID})" << std::endl;
				std::cout << "	--max-data-size (-s) {gibibytes}             Never allow data directory size to exceed {gibibytes} GiB space limit. (default: 100)" << std::endl;
				std::cout << "	--verbose (-V)                               Actively report the status of the process. Useful for debugging." << std::endl;
				return true;
			} else if (arg == "--version" || arg == "-v") {
				std::cout << "BitTorrent UP! 0.2.0" << std::endl;
				return true;
			} else if (arg == "--update-delay" || arg == "-u") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --update-delay.");
				_update_delay = std::stoull(argv[i + 1]);
				i++;
			} else if (arg == "--min-seeders-to-ignore" || arg == "-i") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --min-seeders-to-ignore.");
				_min_seeders_to_ignore = std::stoul(argv[i + 1]);
				i++;
			} else if (arg == "--min-age-to-recount-seeders" || arg == "-c") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --min-age-to-recount-seeds.");
				_min_age_to_recount_seeds = std::stoull(argv[i + 1]);
				i++;
			} else if (arg == "--max-parallel-recounts" || arg == "-P") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --max-parallel-recounts.");
				_max_parallel_recounts = std::stoul(argv[i + 1]);
				i++;
			} else if (arg == "--torrents-dir" || arg == "-t") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --torrents-dir.");
				_torrents_dir = argv[i + 1];
				i++;
			} else if (arg == "--data-dir" || arg == "-D") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --data-dir.");
				_data_dir = argv[i + 1];
				i++;
			} else if (arg == "--tmp-dir" || arg == "-T") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --tmp-dir.");
				_tmp_dir = argv[i + 1];
				i++;
			} else if (arg == "--max-data-size" || arg == "-s") {
				if (i + 1 >= argc)
					throw std::runtime_error("Missing argument for --max-data-size.");
				_max_data_size = std::stoull(argv[i + 1]);
				i++;
			} else if (arg == "--verbose" || arg == "-V") {
				_verbose = true;
			} else
				throw std::runtime_error("Unknown option: " + arg);
		}
		return false;
	}

    uint64_t update_delay() {
        return _update_delay;
    }

    uint32_t min_seeders_to_ignore() {
        return _min_seeders_to_ignore;
    }

    uint64_t min_age_to_recount_seeds() {
        return _min_age_to_recount_seeds;
    }

	uint32_t max_parallel_recounts() {
        return _max_parallel_recounts;
    }

	const std::filesystem::path &torrents_dir() {
        return _torrents_dir;
    }

	const std::filesystem::path &data_dir() {
		return _data_dir;
	}

	const std::filesystem::path &tmp_dir() {
		return _tmp_dir;
	}

	uint64_t max_data_size() {
		return _max_data_size * 1024ULL * 1024ULL * 1024ULL;
	}

	bool verbose() {
		return _verbose;
	}

    uint64_t _update_delay = 10; // Discover new torrents and forget those that were deleted every 10 minutes
	uint32_t _min_seeders_to_ignore = 3; // Do not seed torrents with 3 or more seeds
	uint64_t _min_age_to_recount_seeds = 24 * 60 * 60; // Recount seeders for torrents every 24 hours
	uint32_t _max_parallel_recounts = 100; // Only ever recount seeders for 100 torrents simultaneously
	std::filesystem::path _torrents_dir = "./torrents";
	std::filesystem::path _data_dir = "./data";
	std::filesystem::path _tmp_dir = "/tmp/btup/" + std::to_string(getpid());
	uint64_t _max_data_size = 100;
	bool _verbose = false;
}
