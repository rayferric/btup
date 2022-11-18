namespace app {
	

    void run() {
        // Init LibTorrent Session
        lt::settings_pack session_settings;
        session_settings.set_bool(lt::settings_pack::anonymous_mode, true);
        session_settings.set_int(lt::settings_pack::max_metadata_size, 100 * 1024 * 1024); // Maximum 100 MiB of metadata
        session.apply_settings(session_settings);

        // Purge tmp dir
        std::filesystem::remove_all(config::tmp_dir());
    }

    
}
