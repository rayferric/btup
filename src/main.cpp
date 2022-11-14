#include <pch.hpp>

namespace config {
	uint64_t check_delay = 10 * 60; // Discover new torrents and forget those that were deleted every 10 minutes
	uint32_t min_seeds_to_ignore = 3; // Do not seed torrents with 3 or more seeds
	uint64_t min_age_to_recount_seeds = 24 * 60 * 60; // Recount seeders for torrents every 24 hours
	uint32_t max_parallel_recounts = 100; // Only ever recount seeders for 100 torrents simultaneously
};

struct torrent_stats {
	uint64_t last_recounting_time;
	uint32_t number_of_seeders;
};

struct torrent_userdata {
	std::string name;
	bool seeding;
	uint64_t add_time;
	lt::torrent_handle handle;
};

uint64_t seconds_since_epoch() {
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int main(int argc, char **argv) {
	// // DEBUG
	// lt::session s;
	// lt::add_torrent_params p;
	// p.save_path = "data/1AFC64BD8FE5DC3E0420D833764ED29A15508653";
	// p.ti = std::make_shared<lt::torrent_info>(std::string("torrents/1AFC64BD8FE5DC3E0420D833764ED29A15508653"));
	// p.download_limit = 0;
	// lt::torrent_handle h = s.add_torrent(p);
	// // h.scrape_tracker();
	// while (true) {
	// 	std::cout << h.status().num_incomplete << " " << h.status().num_complete << ' ' << h.status().list_seeds << " " << (h.status().download_rate / 1024.0F) << std::endl;
		
	// 	// std::vector<lt::alert*> alerts;
	// 	// s.pop_alerts(&alerts);

	// 	// for (lt::alert const* a : alerts) {
	// 	// std::cout << a->message() << std::endl;
	// 	// }
		
	// 	std::this_thread::sleep_for(std::chrono::seconds(1));
	// }


	// Parse command line options
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];

		if (arg == "--help" || arg == "-h") {
			std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
			std::cout << "Options:" << std::endl;
			std::cout << "	--help (-h)                                  Show this message." << std::endl;
			std::cout << "	--version (-v)                               Show information about the program release." << std::endl;
			std::cout << "	--check-delay (-d) <seconds>                 Discover new torrents and forget those that were deleted every <seconds> seconds. (default: 10 * 60)" << std::endl;
			std::cout << "	--min-seeds-to-ignore (-i) <seeds>           Do not seed torrents with <seeds> or more seeds. (default: 3)" << std::endl;
			std::cout << "	--min-age-to-recount-seeders (-c) <seconds>  Recount seeders for torrents every <seconds> seconds. (default: 24 * 60 * 60)" << std::endl;
			std::cout << "	--max-parallel-recounts (-p) <count>         Only ever recount seeders for <count> torrents simultaneously. (default: 100)" << std::endl;
			return 0;
		} else if (arg == "--version" || arg == "-v") {
			std::cout << "BitTorrent UP! 1.0.0" << std::endl;
			return 0;
		} else if (arg == "--list-check-delay" || arg == "-d") {
			if (i + 1 >= argc)
				throw std::runtime_error("Missing argument for --check-delay.");
			config::check_delay = std::stoull(argv[i + 1]);
			i++;
		} else if (arg == "--min-seeds-to-ignore" || arg == "-i") {
			if (i + 1 >= argc)
				throw std::runtime_error("Missing argument for --min-seeds-to-ignore.");
			config::min_seeds_to_ignore = std::stoul(argv[i + 1]);
			i++;
		} else if (arg == "--min-age-to-recount-seeders" || arg == "-c") {
			if (i + 1 >= argc)
				throw std::runtime_error("Missing argument for --min-age-to-recount-seeds.");
			config::min_age_to_recount_seeds = std::stoul(argv[i + 1]);
			i++;
		} else if (arg == "--max-parallel-recounts" || arg == "-p") {
			if (i + 1 >= argc)
				throw std::runtime_error("Missing argument for ---max-parallel-recounts.");
			config::max_parallel_recounts = std::stoul(argv[i + 1]);
			i++;
		} else
			throw std::runtime_error("Unknown option: " + arg);
	}

	lt::session session;
	uint64_t last_check_time = 0, last_save_time = 0;
	bool stats_changed = false;
	std::map<std::string, torrent_stats> stats;
	std::vector<std::unique_ptr<torrent_userdata>> recounting_torrents;
	recounting_torrents.reserve(config::max_parallel_recounts);
	std::vector<std::unique_ptr<torrent_userdata>> seeding_torrents;

	// Load previous statistics if they exists
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

			stats[name] = { std::stoull(last_recounting_time), static_cast<uint32_t>(std::stoul(number_of_seeders)) };
		}
	}

	while (true) {
		// Discover new torrents and forget those that were deleted
		if (seconds_since_epoch() - last_check_time > config::check_delay) {
			std::cout << "Starting a routine check of the torrent list." << std::endl;

			std::map<std::string, torrent_stats> valid_stats;
			uint32_t torrents_discovered = 0;

			for (auto &item : std::filesystem::directory_iterator(std::filesystem::current_path() / "torrents")) {
				if (item.is_directory())
					continue;

				auto &path = item.path();
				std::string name = path.filename().string();

				if (stats.contains(name))
					valid_stats[name] = stats[name];
				else {
					valid_stats[name] = { 0, std::numeric_limits<uint32_t>::max() };
					torrents_discovered++;
				}
			}

			uint32_t torrents_unregistered = stats.size() - (valid_stats.size() - torrents_discovered);
			std::cout << "Discovered " << torrents_discovered << " new torrent" << (torrents_discovered == 1 ? ". " : "s. ")
					<< "Unregistered " << torrents_unregistered << " torrent" << (torrents_unregistered == 1 ? "." : "s.")
					<< std::endl;
			
			if (torrents_discovered != 0 || torrents_unregistered != 0) {
				stats = valid_stats;
				stats_changed = true;
			}

			last_check_time = seconds_since_epoch();
		}

		// Stop recounting seeders for torrents that did not receive a response in the last 30 seconds
		auto it = recounting_torrents.begin();
		while (it != recounting_torrents.end()) {
			torrent_userdata &userdata = **it;

			if (seconds_since_epoch() - userdata.add_time > 30) {
				uint32_t number_of_seeders = userdata.handle.status().list_seeds;
				std::cout << "Torrent \"" << userdata.name << "\" did not receive a response from the tracker. The number of known DHT nodes (" << number_of_seeders << ") will be used instead." << std::endl;
				stats[userdata.name].number_of_seeders = number_of_seeders;
				stats[userdata.name].last_recounting_time = seconds_since_epoch();
				stats_changed = true;
				session.remove_torrent(userdata.handle);

				it = recounting_torrents.erase(it);
			} else
				it++;
		}

		// Start recounting seeders
		if (recounting_torrents.size() < config::max_parallel_recounts) {
			for (const auto &item : stats) {
				const auto &name = item.first;
				const auto &torrent = item.second;

				if (!std::filesystem::exists("torrents/" + name)) {
					std::cerr << "Torrent \"" << name << "\" does not exist." << std::endl;
					std::cerr << "Unregistering \"" << name << "\"." << std::endl;
					stats.erase(name);
					stats_changed = true;
				}

				auto it = std::find_if(recounting_torrents.begin(), recounting_torrents.end(), [&](std::unique_ptr<torrent_userdata> &userdata) {
					return userdata->name == name;
				});

				if (it != recounting_torrents.end()) // If torrent is already being recounted
					continue;

				if (seconds_since_epoch() - stats[name].last_recounting_time < config::min_age_to_recount_seeds)
					continue;
				
				std::cout << "Recounting seeders for \"" << name << "\"." <<  std::endl;

				recounting_torrents.push_back(std::make_unique<torrent_userdata>());
				recounting_torrents.back()->name = name;
				recounting_torrents.back()->seeding = false;
				recounting_torrents.back()->add_time = seconds_since_epoch();

				lt::add_torrent_params params;
				params.save_path = "data/" + name;
				params.ti = std::make_shared<lt::torrent_info>("torrents/" + name);
				params.userdata = recounting_torrents.back().get();
				params.download_limit = 100 * 1024 * 1024; // Limit download to 100 KiB/s

				session.async_add_torrent(params);

				if (recounting_torrents.size() >= config::max_parallel_recounts)
					break;
			}
		}

		// Start/stop seeding torrents
		for (const auto &item : stats) {
			const std::string &name = item.first;
			const torrent_stats &torrent = item.second;

			if (!std::filesystem::exists("torrents/" + name)) {
				std::cerr << "Torrent \"" << name << "\" does not exist." << std::endl;
				std::cerr << "Unregistering \"" << name << "\"." << std::endl;
				stats.erase(name);
				stats_changed = true;
			}

			auto it = std::find_if(seeding_torrents.begin(), seeding_torrents.end(), [&](std::unique_ptr<torrent_userdata> &userdata) {
				return userdata->name == name;
			});

			if (torrent.number_of_seeders >= config::min_seeds_to_ignore + (it != seeding_torrents.end() ? 1 : 0)) { // If torrent should not be seeding
				if (it != seeding_torrents.end()) { // And if torrent is already seeding
					std::cout << "Stopping seeding of \"" << name << "\" with " << torrent.number_of_seeders << " seeders." << std::endl;
				
					if ((*it)->handle.is_valid() && (*it)->handle.in_session()) // Remove the torrent from session if it is already added
						session.remove_torrent((*it)->handle);
					seeding_torrents.erase(it); // Remove the torrent from the seeding list

					std::cout << "Total number of torrents seeding is " << seeding_torrents.size() << "." << std::endl;
				}

				continue;
			} else { // If torrent should be seeding
				if (it != seeding_torrents.end()) // And it is already seeding
					continue;
			}

			std::cout << "Attempting to start seeding of \"" << name << "\" with " << torrent.number_of_seeders << " seeders." << std::endl;

			seeding_torrents.push_back(std::make_unique<torrent_userdata>());
			seeding_torrents.back()->name = name;
			seeding_torrents.back()->seeding = true;
			seeding_torrents.back()->add_time = seconds_since_epoch();

			lt::add_torrent_params params;
			params.save_path = "data/" + name;
			params.ti = std::make_shared<lt::torrent_info>("torrents/" + name);
			params.userdata = seeding_torrents.back().get();

			session.async_add_torrent(params);
		}

		// Process LibTorrent alerts for recounting and seeding
		std::vector<lt::alert *> alerts;
		session.pop_alerts(&alerts);

		for (lt::alert *alert : alerts) {
			// std::cout << "Received LibTorrent alert: " << alert->message() << std::endl;

			if (auto *torrent_alert = dynamic_cast<lt::torrent_alert *>(alert)) {
				if (!torrent_alert->handle.is_valid() || !torrent_alert->handle.in_session())
					continue;

				auto *userdata = static_cast<torrent_userdata *>(torrent_alert->handle.userdata());
				if (!userdata) {
					session.remove_torrent(torrent_alert->handle);
					continue;
				}
				userdata->handle = torrent_alert->handle;

				if (auto *add_torrent_alert = lt::alert_cast<lt::add_torrent_alert>(torrent_alert)) {
					if (!userdata->seeding) {
						std::cout << "Sending a scrape request for \"" << userdata->name << "\"." << std::endl;
						add_torrent_alert->handle.scrape_tracker(); // Send the scrape request
					} else { // If torrent was added for seeding
						if (stats[userdata->name].number_of_seeders == 0 && userdata->handle.status().state != lt::torrent_status::state_t::seeding) {
							std::cerr << "Torrent \"" << userdata->name << "\" has no seeders at all and a local copy of torrent's data is not available. Unable to start seeding this torrent." << std::endl;
							std::cerr << "Unregistering \"" << userdata->name << "\"." << std::endl;
							stats.erase(userdata->name);
							stats_changed = true;

							session.remove_torrent(userdata->handle);
							seeding_torrents.erase(std::find_if(seeding_torrents.begin(), seeding_torrents.end(), [&](auto &x) {
								return x.get() == userdata;
							}));
						} else {
							std::cout << "Successfully started seeding \"" << userdata->name << "\"." << std::endl;
						}

						std::cout << "Total number of torrents seeding is " << seeding_torrents.size() << "." << std::endl;
					}
				} else if (auto *scrape_reply_alert = lt::alert_cast<lt::scrape_reply_alert>(torrent_alert)) {
					uint64_t elapsed = seconds_since_epoch() - userdata->add_time;
					std::cout << "Received scrape reply for \"" << userdata->name << "\" after " << elapsed << " second" << (elapsed == 1 ? "." : "s.") << std::endl;

					uint32_t number_of_seeders = static_cast<uint32_t>(scrape_reply_alert->complete); // Eventual -1 wraps to max uint32_t
					if (number_of_seeders == std::numeric_limits<uint32_t>::max()) {
						std::cerr << "Scrape reply contained an invalid number of seeders. Ignoring it." << std::endl;
						continue;
					}

					std::cout << "Tracker knows " << number_of_seeders << " seeder" << (number_of_seeders == 1 ? "" : "s") << " of \"" << userdata->name << "\"." << std::endl;

					if (!userdata->seeding) {
						stats[userdata->name].last_recounting_time = seconds_since_epoch();
						stats[userdata->name].number_of_seeders = number_of_seeders;
						stats_changed = true;

						session.remove_torrent(userdata->handle);
						recounting_torrents.erase(std::find_if(recounting_torrents.begin(), recounting_torrents.end(), [&](auto &x) {
							return x.get() == userdata;
						}));
					}
				}
			}
		}

		// Save statistics every 30 seconds
		if (stats_changed && seconds_since_epoch() - last_save_time > 30) {
			std::cout << "Saving statistics." << std::endl;
			
			std::ofstream stats_file("stats.txt");
			stats_file << "torrent-name/last-recounting-time/number-of-seeders" << std::endl; // Add the header
			for (const auto &[name, torrent] : stats)
				stats_file << name << '/' << torrent.last_recounting_time << '/' << torrent.number_of_seeders << std::endl;

			stats_changed = false;
			last_save_time = seconds_since_epoch();
		}

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	return 0;
}