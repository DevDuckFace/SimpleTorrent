#include "torrent_engine.h"
#include <libtorrent/session_params.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <fstream>
#include <thread>
#include <chrono>

TorrentEngine::TorrentEngine() 
    : uploadBlocked_(true), maxConnections_(100), aggressiveMode_(false), askForFolder_(false), language_(L"pt") {}

TorrentEngine::~TorrentEngine() {
    shutdown();
}

void TorrentEngine::initialize(const std::wstring& appDataPath) {
    appDataPath_ = appDataPath;

    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::user_agent, "SimpleTorrent/1.0");
    settings.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::status | lt::alert_category::error |
        lt::alert_category::storage);

    settings.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6881,[::]:6881");
    settings.set_bool(lt::settings_pack::enable_dht, true);
    settings.set_bool(lt::settings_pack::enable_lsd, true);

    session_ = std::make_unique<lt::session>(settings);
    
    applySettings();
}

void TorrentEngine::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_) {
        session_->pause();
        session_.reset();
    }
    handles_.clear();
}

void TorrentEngine::applySettings() {
    if (!session_) return;

    lt::settings_pack settings;
    settings.set_int(lt::settings_pack::connections_limit, maxConnections_);

    if (aggressiveMode_) {
        settings.set_int(lt::settings_pack::tracker_backoff, 250);
        settings.set_int(lt::settings_pack::min_reconnect_time, 10);
        settings.set_bool(lt::settings_pack::announce_to_all_trackers, true);
        settings.set_bool(lt::settings_pack::announce_to_all_tiers, true);
        settings.set_int(lt::settings_pack::connection_speed, 100);
        settings.set_int(lt::settings_pack::peer_connect_timeout, 10);
        settings.set_int(lt::settings_pack::choking_algorithm, lt::settings_pack::fixed_slots_choker);
    } else {
        settings.set_int(lt::settings_pack::tracker_backoff, 250);
        settings.set_int(lt::settings_pack::min_reconnect_time, 60);
        settings.set_bool(lt::settings_pack::announce_to_all_trackers, false);
        settings.set_bool(lt::settings_pack::announce_to_all_tiers, false);
        settings.set_int(lt::settings_pack::connection_speed, 20);
        settings.set_int(lt::settings_pack::peer_connect_timeout, 15);
        settings.set_int(lt::settings_pack::choking_algorithm, lt::settings_pack::fastest_upload);
    }

    if (uploadBlocked_) {
        settings.set_int(lt::settings_pack::upload_rate_limit, 1024); // 1 KB/s minimum
    } else {
        settings.set_int(lt::settings_pack::upload_rate_limit, 0);   // Unlimited
    }

    session_->apply_settings(settings);

    // Apply per-torrent upload limits
    for (auto& h : handles_) {
        if (h.is_valid()) {
            h.set_upload_limit(uploadBlocked_ ? 1024 : 0);
        }
    }
}

void TorrentEngine::saveState() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) return;

    // Save configuration
    std::string configPath = wideToUtf8(appDataPath_) + "\\config.txt";
    std::ofstream cfg(configPath);
    if (cfg.is_open()) {
        cfg << wideToUtf8(savePath_) << "\n";
        cfg << maxConnections_ << "\n";
        cfg << (aggressiveMode_ ? 1 : 0) << "\n";
        cfg << (uploadBlocked_ ? 1 : 0) << "\n";
        cfg << (askForFolder_ ? 1 : 0) << "\n";
        cfg << wideToUtf8(language_) << "\n";
    }

    // Clear old resume files
    std::string searchPath = wideToUtf8(appDataPath_) + "\\*.resume";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string file = wideToUtf8(appDataPath_) + "\\" + fd.cFileName;
            DeleteFileA(file.c_str());
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    // Request save_resume_data for all torrents
    session_->pause();
    int outstanding = 0;
    for (auto& h : handles_) {
        if (h.is_valid()) {
            h.save_resume_data(lt::torrent_handle::save_info_dict);
            outstanding++;
        }
    }

    // Wait and collect alerts
    int fileIdx = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (outstanding > 0) {
        if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(10)) break; // Timeout
        
        std::vector<lt::alert*> alerts;
        session_->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            if (auto* s = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                std::string path = wideToUtf8(appDataPath_) + "\\" + std::to_string(fileIdx++) + ".resume";
                std::ofstream f(path, std::ios::binary);
                if (f.is_open()) {
                    std::vector<char> buf = lt::write_resume_data_buf(s->params);
                    f.write(buf.data(), buf.size());
                }
                outstanding--;
            }
            else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                outstanding--;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void TorrentEngine::loadState() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) return;

    // Load configuration
    std::string configPath = wideToUtf8(appDataPath_) + "\\config.txt";
    std::ifstream cfg(configPath);
    if (cfg.is_open()) {
        std::string pathStr;
        std::getline(cfg, pathStr);
        if (!pathStr.empty()) savePath_ = utf8ToWide(pathStr);
        
        int aggressiveInt = 0, uploadInt = 0, askFolderInt = 0;
        cfg >> maxConnections_ >> aggressiveInt >> uploadInt >> askFolderInt;
        aggressiveMode_ = (aggressiveInt != 0);
        uploadBlocked_ = (uploadInt != 0);
        askForFolder_ = (askFolderInt != 0);
        
        std::string langStr;
        cfg >> langStr;
        if (!langStr.empty()) language_ = utf8ToWide(langStr);
    }

    applySettings();

    // Load all resume files
    std::string searchPath = wideToUtf8(appDataPath_) + "\\*.resume";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string file = wideToUtf8(appDataPath_) + "\\" + fd.cFileName;
            std::ifstream f(file, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                std::streamsize size = f.tellg();
                f.seekg(0, std::ios::beg);
                std::vector<char> buf(size);
                if (f.read(buf.data(), size)) {
                    lt::error_code ec;
                    lt::add_torrent_params params = lt::read_resume_data(buf, ec);
                    if (!ec) {
                        params.save_path = wideToUtf8(savePath_);
                        if (uploadBlocked_) params.upload_limit = 1024;
                        lt::torrent_handle h = session_->add_torrent(params);
                        handles_.push_back(h);
                    }
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

bool TorrentEngine::addTorrentFile(const std::wstring& path, const std::wstring& savePathOverride) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) return false;

    try {
        lt::add_torrent_params params;
        std::string utf8Path = wideToUtf8(path);
        params.ti = std::make_shared<lt::torrent_info>(utf8Path);
        params.save_path = savePathOverride.empty() ? wideToUtf8(savePath_) : wideToUtf8(savePathOverride);

        if (uploadBlocked_) {
            params.upload_limit = 1024;
        }

        lt::torrent_handle h = session_->add_torrent(params);
        handles_.push_back(h);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool TorrentEngine::addMagnetLink(const std::wstring& uri, const std::wstring& savePathOverride) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_) return false;

    try {
        std::string magnetStr = wideToUtf8(uri);
        lt::add_torrent_params params = lt::parse_magnet_uri(magnetStr);
        params.save_path = savePathOverride.empty() ? wideToUtf8(savePath_) : wideToUtf8(savePathOverride);

        if (uploadBlocked_) {
            params.upload_limit = 1024;
        }

        lt::torrent_handle h = session_->add_torrent(params);
        handles_.push_back(h);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<TorrentInfo> TorrentEngine::getStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TorrentInfo> result;
    if (!session_) return result;

    for (auto& h : handles_) {
        if (!h.is_valid()) continue;

        lt::torrent_status s = h.status();
        TorrentInfo info;
        info.name = utf8ToWide(s.name);
        if (info.name.empty()) {
            info.name = L"(buscando metadata...)";
        }
        info.progress = s.progress;
        info.downloadRate = s.download_rate;
        info.uploadRate = s.upload_rate;
        info.totalSize = s.total_wanted;
        info.downloaded = s.total_wanted_done;
        info.stateStr = stateToString(s.state);
        info.numSeeds = s.num_seeds;
        info.numPeers = s.num_peers;
        info.isPaused = (s.flags & lt::torrent_flags::paused) != lt::torrent_flags_t{};

        if (info.isPaused) {
            info.stateStr = L"Pausado";
        }

        result.push_back(info);
    }

    return result;
}

size_t TorrentEngine::getCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handles_.size();
}

void TorrentEngine::removeTorrent(size_t index, bool deleteFiles) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_ || index >= handles_.size()) return;

    if (deleteFiles) {
        session_->remove_torrent(handles_[index], lt::session::delete_files);
    } else {
        session_->remove_torrent(handles_[index]);
    }
    handles_.erase(handles_.begin() + static_cast<ptrdiff_t>(index));
}

void TorrentEngine::pauseResumeTorrent(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_ || index >= handles_.size()) return;

    lt::torrent_status s = handles_[index].status();
    bool paused = (s.flags & lt::torrent_flags::paused) != lt::torrent_flags_t{};

    if (paused) {
        handles_[index].resume();
    } else {
        handles_[index].pause();
    }
}

void TorrentEngine::setUploadBlocked(bool blocked) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uploadBlocked_ = blocked;
    }
    applySettings();
}

bool TorrentEngine::isUploadBlocked() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return uploadBlocked_;
}

void TorrentEngine::setSavePath(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    savePath_ = path;
}

std::wstring TorrentEngine::getSavePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return savePath_;
}

void TorrentEngine::setMaxConnections(int maxConn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maxConnections_ = maxConn;
    }
    applySettings();
}

int TorrentEngine::getMaxConnections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return maxConnections_;
}

void TorrentEngine::setAggressiveMode(bool aggressive) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aggressiveMode_ = aggressive;
    }
    applySettings();
}

bool TorrentEngine::isAggressiveMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aggressiveMode_;
}

void TorrentEngine::setAskForFolder(bool ask) {
    std::lock_guard<std::mutex> lock(mutex_);
    askForFolder_ = ask;
}

bool TorrentEngine::isAskForFolder() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return askForFolder_;
}

void TorrentEngine::setLanguage(const std::wstring& lang) {
    std::lock_guard<std::mutex> lock(mutex_);
    language_ = lang;
}

std::wstring TorrentEngine::getLanguage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return language_;
}

std::wstring TorrentEngine::stateToString(lt::torrent_status::state_t s) {
    switch (s) {
        case lt::torrent_status::checking_files:          return L"Verificando";
        case lt::torrent_status::downloading_metadata:    return L"Metadata";
        case lt::torrent_status::downloading:             return L"Baixando";
        case lt::torrent_status::finished:                return L"Completo";
        case lt::torrent_status::seeding:                 return L"Seeding";
        case lt::torrent_status::checking_resume_data:    return L"Verificando";
        default:                                          return L"Desconhecido";
    }
}

std::string TorrentEngine::wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string result(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        &result[0], sz, nullptr, nullptr);
    return result;
}

std::wstring TorrentEngine::utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                  nullptr, 0);
    std::wstring result(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                        &result[0], sz);
    return result;
}
