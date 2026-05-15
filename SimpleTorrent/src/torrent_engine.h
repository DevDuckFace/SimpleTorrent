#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>

namespace lt = libtorrent;

struct TorrentInfo {
    std::wstring name;
    float progress;       // 0.0 - 1.0
    int downloadRate;     // bytes/sec
    int uploadRate;       // bytes/sec
    int64_t totalSize;
    int64_t downloaded;
    std::wstring stateStr;
    int numSeeds;
    int numPeers;
    bool isPaused;
};

class TorrentEngine {
public:
    TorrentEngine();
    ~TorrentEngine();

    void initialize(const std::wstring& appDataPath);
    void shutdown();

    bool addTorrentFile(const std::wstring& path, const std::wstring& savePathOverride = L"");
    bool addMagnetLink(const std::wstring& uri, const std::wstring& savePathOverride = L"");

    std::vector<TorrentInfo> getStatus();
    size_t getCount() const;

    void removeTorrent(size_t index, bool deleteFiles = false);
    void pauseResumeTorrent(size_t index);

    void setUploadBlocked(bool blocked);
    bool isUploadBlocked() const;

    void setSavePath(const std::wstring& path);
    std::wstring getSavePath() const;

    void setMaxConnections(int maxConn);
    int getMaxConnections() const;

    void setAggressiveMode(bool aggressive);
    bool isAggressiveMode() const;

    void setAskForFolder(bool ask);
    bool isAskForFolder() const;

    void setLanguage(const std::wstring& lang);
    std::wstring getLanguage() const;

    void loadState();
    void saveState();

    static std::string wideToUtf8(const std::wstring& wide);
    static std::wstring utf8ToWide(const std::string& utf8);

private:
    std::unique_ptr<lt::session> session_;
    std::vector<lt::torrent_handle> handles_;
    mutable std::mutex mutex_;
    
    bool uploadBlocked_;
    std::wstring savePath_;
    std::wstring appDataPath_;
    int maxConnections_;
    bool aggressiveMode_;
    bool askForFolder_;
    std::wstring language_;

    void applySettings();
    static std::wstring stateToString(lt::torrent_status::state_t s);
};
