#include <pch.hpp>

#include <util.hpp>
#include <stats.hpp>
#include <config.hpp>

struct torrent_userdata {
	std::string name;
	bool seeding;
	uint64_t add_time;
	lt::torrent_handle handle;
};

lt::session session;
uint64_t last_update_time = 0; // Forces update on the first iteration of the main loop
std::vector<std::unique_ptr<torrent_userdata>> recounting_torrents;
std::vector<std::unique_ptr<torrent_userdata>> seeding_torrents;

void discover_new_torrents_and_unregister_deleted_ones() {
	if (config::verbose())
		std::cout << "Discovering new torrents." << std::endl;

	std::map<std::string, stats::torrent> valid_stats;
	uint32_t torrents_discovered = 0;

	std::filesystem::create_directory(config::torrents_dir());
	for (auto &item : std::filesystem::directory_iterator(config::torrents_dir())) {
		if (item.is_directory())
			continue;

		auto &path = item.path();
		std::string name = path.filename().string();

		if (stats::get().contains(name))
			valid_stats[name] = stats::get().at(name);
		else {
			valid_stats[name] = { 0, std::numeric_limits<uint32_t>::max() };
			torrents_discovered++;
		}
	}

	uint32_t torrents_unregistered = stats::get().size() - (valid_stats.size() - torrents_discovered);
	if (torrents_discovered != 0 || torrents_unregistered != 0) {
		std::cout << "Discovered " << torrents_discovered << " new torrent" << (torrents_discovered == 1 ? ". " : "s. ")
				<< "Unregistered " << torrents_unregistered << " torrent" << (torrents_unregistered == 1 ? "." : "s.")
				<< std::endl;
	}
	
	if (torrents_discovered != 0 || torrents_unregistered != 0)
		stats::set(std::move(valid_stats));
}

void get_torrent_names_from_stats(std::vector<std::string> &names) {
	names.resize(stats::get().size());
	std::transform(stats::get().begin(), stats::get().end(), names.begin(), [](auto &item) {
		return item.first;
	});
}

void sort_torrent_names_by_seeder_count(std::vector<std::string> &names) {
	std::sort(
		names.begin(),
		names.end(),
		[](const std::string &a, const std::string &b) {
			return stats::get().at(a).number_of_seeders < stats::get().at(b).number_of_seeders;
		}
	);
}

void purge_data_of_deleted_torrents_then_get_names_of_torrents_with_data_and_total_data_size(std::vector<std::string> &names, uint64_t &total_data_size) {
	if (config::verbose())
		std::cout << "Purging data of deleted torrents." << std::endl;
	
	std::filesystem::create_directory(config::data_dir());
	uint32_t total_purged = 0;

	total_data_size = 0;

	for (auto &item : std::filesystem::directory_iterator(config::data_dir())) {
		if (!item.is_directory())
			continue;

		auto &path = item.path();
		std::string name = path.filename().string();

		auto torrent_path = config::torrents_dir() / name;
		if (!std::filesystem::exists(torrent_path)) {
			std::cout << "Torrent \"" << name << "\" does not exist." << std::endl;
			std::cout << "Deleting all data belonging to \"" << name << "\"." << std::endl;
			std::filesystem::remove_all(path);
			total_purged++;
		} else {
			lt::torrent_info info(torrent_path);
			total_data_size += info.total_size();
			names.push_back(name);
		}
	}

	if (total_purged != 0) {
		std::cout << "Purged " << total_purged << " torrent" << (total_purged == 1 ? "'s data." : "s' data.")
				<< " Maximum size of data kept on disk is " << (total_data_size / 1024.0F / 1024.0F / 1024.0F) << " GiB." << std::endl;
	}
}

void manage_torrent_seeding(const std::vector<std::string> &torrent_names, std::vector<std::string> &data_names, uint64_t total_data_size) {
	if (config::verbose())
		std::cout << "Checking the seeding list." << std::endl;
	
	// torrent_names is sorted by the ascending number of seeders
	for (const std::string &name : torrent_names) {
		const stats::torrent &torrent = stats::get().at(name);

		std::filesystem::path torrent_path = config::torrents_dir() / name;
		if (!std::filesystem::exists(torrent_path)) {
			std::cout << "Torrent \"" << name << "\" does not exist." << std::endl;
			std::cout << "Unregistering \"" << name << "\"." << std::endl;
			stats::remove(name);
		}

		{
			auto it = std::find_if(seeding_torrents.begin(), seeding_torrents.end(), [&](std::unique_ptr<torrent_userdata> &userdata) {
				return userdata->name == name;
			});

			if (torrent.number_of_seeders >= config::min_seeders_to_ignore() + (it != seeding_torrents.end() ? 1 : 0)) {
				// If torrent should not be seeding
				if (it != seeding_torrents.end()) {
					// And if torrent is already seeding
					std::cout << "Stopping seeding of \"" << name << "\" with " << torrent.number_of_seeders << " seeders." << std::endl;
				
					if ((*it)->handle.is_valid() && (*it)->handle.in_session())
						// Remove the torrent from session if it is already added
						session.remove_torrent((*it)->handle);
					seeding_torrents.erase(it); // Remove the torrent from the seeding list

					std::cout << "Total number of torrents seeding is " << seeding_torrents.size() << "." << std::endl;
				}

				break; // Further torrents will only have lower number of seeders due to sorting
			} else {
				// If torrent should be seeding
				if (it != seeding_torrents.end()) // And it is already seeding
					continue;
			}
		}

		auto torrent_info = std::make_shared<lt::torrent_info>(torrent_path.string());
		uint64_t data_size = torrent_info->total_size();

		// Attempt to purge less important torrent data if the size of this torrent would make the data directory too large
		if (total_data_size + data_size > config::max_data_size()) {
			std::cout << "Unable to start seeding \"" << name << "\", because the data directory is too large. Attempting to free some space." << std::endl;

			auto it = data_names.end() - 1; // data_names is sorted by the ascending number of seeders

			// Check how much space can be freed
			uint64_t space_available_to_free = 0;
			while (true) {
				if (it == data_names.begin() || stats::get().at(*it).number_of_seeders <= torrent.number_of_seeders) {
					std::cout << "Could not find enough bytes of less important data to free. The torrent will not be seeded." << std::endl;
					std::cout << "Unregistering \"" << name << "\"." << std::endl;
					stats::remove(name);
					break;
				}

				space_available_to_free += lt::torrent_info(config::torrents_dir() / (*it)).total_size();

				if (total_data_size - space_available_to_free + data_size <= config::max_data_size()) {
					std::cout << "Found enough bytes of less important data to free. Purging." << std::endl;

					auto it2 = data_names.end() - 1;
					while (it2 != it) {
						std::cout << "Deleting all data belonging to \"" << (*it2) << "\"." << std::endl;
						std::filesystem::remove_all(config::data_dir() / (*it2));
						it2--;
						data_names.pop_back();
					}
					total_data_size -= space_available_to_free;

					goto start_seeding;
				}

				it--;
			}

			continue;
		}

		start_seeding:

		std::cout << "Attempting to start seeding of \"" << name << "\" with " << torrent.number_of_seeders << " seeders." << std::endl;

		seeding_torrents.push_back(std::make_unique<torrent_userdata>());
		seeding_torrents.back()->name = name;
		seeding_torrents.back()->seeding = true;
		seeding_torrents.back()->add_time = util::seconds_since_epoch();

		lt::add_torrent_params params;
		params.save_path = (config::data_dir() / name).string();
		params.ti = std::move(torrent_info);
		params.userdata = seeding_torrents.back().get();

		session.async_add_torrent(params);
	}
}

void finish_timed_out_recounting_tickets() {
	if (config::verbose())
		std::cout << "Finalizing recounting tickets that have timed out." << std::endl;

	auto it = recounting_torrents.begin();
	while (it != recounting_torrents.end()) {
		torrent_userdata &userdata = **it;

		if (util::seconds_since_epoch() - userdata.add_time > 30) {
			uint32_t number_of_seeders = userdata.handle.status().list_seeds;
			std::cout << "Torrent \"" << userdata.name << "\" did not receive a response from the tracker. The number of known DHT nodes (" << number_of_seeders << ") will be used instead." << std::endl;
			stats::set(userdata.name, util::seconds_since_epoch(), number_of_seeders);
			session.remove_torrent(userdata.handle);
			std::filesystem::remove_all(config::tmp_dir() / userdata.name);

			it = recounting_torrents.erase(it);
		} else
			it++;
	}
}

void start_recounting_seeders() {
	if (config::verbose())
		std::cout << "Starting to recount torrents." << std::endl; 

	if (recounting_torrents.size() < config::max_parallel_recounts()) {
		for (const auto &item : stats::get()) {
			const auto &name = item.first;
			const auto &torrent = item.second;

			std::filesystem::path torrent_path = config::torrents_dir() / name;
			if (!std::filesystem::exists(torrent_path)) {
				std::cout << "Torrent \"" << name << "\" does not exist." << std::endl;
				std::cout << "Unregistering \"" << name << "\"." << std::endl;
				stats::remove(name);
			}

			auto it = std::find_if(recounting_torrents.begin(), recounting_torrents.end(), [&](std::unique_ptr<torrent_userdata> &userdata) {
				return userdata->name == name;
			});

			if (it != recounting_torrents.end()) // If torrent is already being recounted
				continue;

			if (util::seconds_since_epoch() - stats::get().at(name).last_recounting_time < config::min_age_to_recount_seeds())
				continue;
			
			std::cout << "Recounting seeders for \"" << name << "\"." <<  std::endl;

			recounting_torrents.push_back(std::make_unique<torrent_userdata>());
			recounting_torrents.back()->name = name;
			recounting_torrents.back()->seeding = false;
			recounting_torrents.back()->add_time = util::seconds_since_epoch();

			lt::add_torrent_params params;
			params.save_path = (config::tmp_dir() / name).string();
			params.ti = std::make_shared<lt::torrent_info>(torrent_path.string());
			params.userdata = recounting_torrents.back().get();
			params.download_limit = 100 * 1024 * 1024; // Limit download to 100 KiB/s

			session.async_add_torrent(params);

			if (recounting_torrents.size() >= config::max_parallel_recounts())
				break;
		}
	}
}

void process_libtorrent_alerts() {
	if (config::verbose())
		std::cout << "Processing LibTorrent alerts." << std::endl;

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
					// If torrent was added in order to recount its seeders
					std::cout << "Sending a scrape request for \"" << userdata->name << "\"." << std::endl;
					add_torrent_alert->handle.scrape_tracker(); // Send the scrape request
				} else {
					// If torrent was added to start being seeded
					if (stats::get().at(userdata->name).number_of_seeders == 0 && userdata->handle.status().state != lt::torrent_status::state_t::seeding) {
						std::cout << "Torrent \"" << userdata->name << "\" has no seeders at all and a local copy of torrent's data is not available. Unable to start seeding this torrent." << std::endl;
						std::cout << "Unregistering \"" << userdata->name << "\"." << std::endl;
						stats::remove(userdata->name);

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
				uint64_t elapsed = util::seconds_since_epoch() - userdata->add_time;
				std::cout << "Received scrape reply for \"" << userdata->name << "\" after " << elapsed << " second" << (elapsed == 1 ? "." : "s.") << std::endl;

				uint32_t number_of_seeders = static_cast<uint32_t>(scrape_reply_alert->complete); // Eventual -1 wraps to max uint32_t
				if (number_of_seeders == std::numeric_limits<uint32_t>::max()) {
					std::cout << "Scrape reply contained an invalid number of seeders. Ignoring it." << std::endl;
					continue;
				}

				std::cout << "Tracker knows " << number_of_seeders << " seeder" << (number_of_seeders == 1 ? "" : "s") << " of \"" << userdata->name << "\"." << std::endl;

				if (!userdata->seeding) {
					stats::set(userdata->name, util::seconds_since_epoch(), number_of_seeders);

					session.remove_torrent(userdata->handle);
					std::filesystem::remove_all(config::tmp_dir() / userdata->name);
					recounting_torrents.erase(std::find_if(recounting_torrents.begin(), recounting_torrents.end(), [&](auto &x) {
						return x.get() == userdata;
					}));
				}
			}
		}
	}
}

int main(int argc, char **argv) {
	try {
		// Parse command line options
		if (config::init(argc, argv))
			return 0;

		// Init LibTorrent session settings
		{
			lt::settings_pack settings;
			settings.set_bool(lt::settings_pack::anonymous_mode, true);
			settings.set_int(lt::settings_pack::max_metadata_size, 1024 * 1024 * 1240);
			session.apply_settings(settings);
		}

		// Load statistics from previous sessions if they exist
		stats::try_load();

		while (true) {
			// Update
			if (util::seconds_since_epoch() - last_update_time > config::update_delay()) {
				if (config::verbose())
					std::cout << "Periodic process update has started." << std::endl;

				discover_new_torrents_and_unregister_deleted_ones();

				std::vector<std::string> torrent_names;
				get_torrent_names_from_stats(torrent_names);

				std::vector<std::string> data_names;
				uint64_t total_data_size;
				purge_data_of_deleted_torrents_then_get_names_of_torrents_with_data_and_total_data_size(data_names, total_data_size);

				sort_torrent_names_by_seeder_count(torrent_names);
				sort_torrent_names_by_seeder_count(data_names);

				manage_torrent_seeding(torrent_names, data_names, total_data_size);
				
				last_update_time = util::seconds_since_epoch();
			}

			finish_timed_out_recounting_tickets();
			start_recounting_seeders();

			process_libtorrent_alerts();

			// Save statistics if they've changed and enough time has passed
			stats::try_save();

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	} catch (std::exception &e) {
		std::cout << "Fatal Error: " << e.what() << std::endl;
	}

	return 0;
}