#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <cstdio>
#include <objbase.h>
#include <map>
#include <fstream>
#include "resource.h"
#include "torrent_engine.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")

// ── Globals ──
static HINSTANCE g_hInst;
static HWND g_hWnd, g_hList, g_hStatus;
static HWND g_hBtnAdd, g_hBtnMagnet, g_hBtnRemove, g_hBtnPause, g_hBtnSettings, g_hBtnKill;
static TorrentEngine g_engine;
static HBRUSH g_brRed, g_brGreen;
static HFONT g_hFontUI, g_hFontBold;
static std::map<std::wstring, std::wstring> g_Lang;

// ── Translation ──
static std::wstring T(const wchar_t* key, const wchar_t* defVal) {
    auto it = g_Lang.find(key);
    if (it != g_Lang.end()) return it->second;
    return defVal;
}

static void LoadLanguage(const std::wstring& langCode) {
    g_Lang.clear();
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::string iniPath = TorrentEngine::wideToUtf8(dir) + "\\lang\\" + TorrentEngine::wideToUtf8(langCode) + ".ini";

    std::ifstream f(iniPath);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            if (!val.empty() && val.back() == '\r') val.pop_back();
            g_Lang[TorrentEngine::utf8ToWide(key)] = TorrentEngine::utf8ToWide(val);
        }
    }
}

static void UpdateKillButton() {
    bool blocked = g_engine.isUploadBlocked();
    const wchar_t* key = blocked ? L"UI_UP_BLOCKED" : L"UI_UP_ACTIVE";
    const wchar_t* def = blocked ? L"\x26D4 UPLOAD BLOQUEADO" : L"\x2705 UPLOAD ATIVO";
    SetWindowTextW(g_hBtnKill, T(key, def).c_str());
    InvalidateRect(g_hBtnKill, nullptr, TRUE);
}

static void UpdateUIText() {
    SetWindowTextW(g_hBtnAdd, T(L"UI_ADD_FILE", L"Adicionar .torrent").c_str());
    SetWindowTextW(g_hBtnMagnet, T(L"UI_ADD_MAGNET", L"Magnet Link").c_str());
    SetWindowTextW(g_hBtnPause, T(L"UI_PAUSE", L"Pausar/Retomar").c_str());
    SetWindowTextW(g_hBtnRemove, T(L"UI_REMOVE", L"Remover").c_str());
    SetWindowTextW(g_hBtnSettings, T(L"UI_SETTINGS", L"Opcoes").c_str());

    UpdateKillButton();

    struct ColData { const wchar_t* key; const wchar_t* def; };
    ColData cols[] = {
        {L"UI_COL_NAME", L"Nome"},
        {L"UI_COL_PROG", L"Progresso (%)"},
        {L"UI_COL_DL", L"Download"},
        {L"UI_COL_UL", L"Upload"},
        {L"UI_COL_SIZE", L"Tamanho"},
        {L"UI_COL_SEEDS", L"Seeds"},
        {L"UI_COL_PEERS", L"Peers"},
        {L"UI_COL_STATE", L"Estado"},
    };
    for (int i = 0; i < 8; i++) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT;
        std::wstring txt = T(cols[i].key, cols[i].def);
        col.pszText = const_cast<wchar_t*>(txt.c_str());
        ListView_SetColumn(g_hList, i, &col);
    }
}

// ── Helpers ──
static std::wstring FormatSize(int64_t bytes) {
    wchar_t buf[64];
    if (bytes >= 1073741824LL) swprintf(buf, 64, L"%.1f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576LL) swprintf(buf, 64, L"%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024LL) swprintf(buf, 64, L"%.1f KB", bytes / 1024.0);
    else swprintf(buf, 64, L"%lld B", bytes);
    return buf;
}

static std::wstring FormatSpeed(int bps) {
    return FormatSize(bps) + L"/s";
}

// ── Create ListView columns ──
static void InitListView() {
    ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    int widths[] = { 240, 80, 90, 90, 80, 50, 50, 80 };
    int fmts[] = { LVCFMT_LEFT, LVCFMT_CENTER, LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_RIGHT, LVCFMT_CENTER, LVCFMT_CENTER, LVCFMT_CENTER };
    for (int i = 0; i < 8; i++) {
        LVCOLUMNW col = {};
        col.mask = LVCF_WIDTH | LVCF_FMT;
        col.cx = widths[i];
        col.fmt = fmts[i];
        ListView_InsertColumn(g_hList, i, &col);
    }
}

// ── Refresh torrent list ──
static void RefreshList() {
    auto torrents = g_engine.getStatus();
    int count = (int)torrents.size();

    int cur = ListView_GetItemCount(g_hList);
    while (cur < count) {
        LVITEMW item = {};
        item.iItem = cur++;
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"");
        ListView_InsertItem(g_hList, &item);
    }
    while (cur > count) {
        ListView_DeleteItem(g_hList, --cur);
    }

    int totalDown = 0, totalUp = 0;
    for (int i = 0; i < count; i++) {
        auto& t = torrents[i];
        totalDown += t.downloadRate;
        totalUp += t.uploadRate;

        wchar_t prog[16];
        swprintf(prog, 16, L"%.1f%%", t.progress * 100.0f);

        std::wstring dl = FormatSpeed(t.downloadRate);
        std::wstring ul = FormatSpeed(t.uploadRate);
        std::wstring sz = FormatSize(t.totalSize);
        wchar_t seeds[16], peers[16];
        swprintf(seeds, 16, L"%d", t.numSeeds);
        swprintf(peers, 16, L"%d", t.numPeers);

        std::wstring stateStr = t.stateStr;
        if (t.isPaused) stateStr = T(L"UI_ST_PAUSED", L"Pausado");
        else if (t.stateStr == L"Baixando") stateStr = T(L"UI_ST_DOWNLOADING", L"Baixando");
        else if (t.stateStr == L"Completo") stateStr = T(L"UI_ST_COMPLETED", L"Completo");
        else if (t.stateStr == L"Seeding") stateStr = T(L"UI_ST_SEEDING", L"Semeando");
        else if (t.stateStr == L"Metadata") stateStr = T(L"UI_ST_METADATA", L"Metadata");
        else if (t.stateStr == L"Verificando") stateStr = T(L"UI_ST_CHECKING", L"Verificando");
        else stateStr = T(L"UI_ST_UNKNOWN", L"Desconhecido");

        ListView_SetItemText(g_hList, i, 0, const_cast<wchar_t*>(t.name.c_str()));
        ListView_SetItemText(g_hList, i, 1, prog);
        ListView_SetItemText(g_hList, i, 2, const_cast<wchar_t*>(dl.c_str()));
        ListView_SetItemText(g_hList, i, 3, const_cast<wchar_t*>(ul.c_str()));
        ListView_SetItemText(g_hList, i, 4, const_cast<wchar_t*>(sz.c_str()));
        ListView_SetItemText(g_hList, i, 5, seeds);
        ListView_SetItemText(g_hList, i, 6, peers);
        ListView_SetItemText(g_hList, i, 7, const_cast<wchar_t*>(stateStr.c_str()));
    }

    wchar_t status[256];
    swprintf(status, 256, L"  %d %s  |  DL: %s  |  UL: %s  |  Upload: %s",
        count,
        T(L"UI_TORRENTS", L"torrent(s)").c_str(),
        FormatSpeed(totalDown).c_str(),
        FormatSpeed(totalUp).c_str(),
        g_engine.isUploadBlocked() ? T(L"UI_BLOCKED", L"BLOQUEADO").c_str() : T(L"UI_ACTIVE", L"ATIVO").c_str());
    SetWindowTextW(g_hStatus, status);
}

// ── Ask for Folder Helper ──
static bool PromptForFolder(std::wstring& outPath) {
    bool ok = false;
    IFileDialog *pfd;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD dwOptions;
        pfd->GetOptions(&dwOptions);
        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(pfd->Show(g_hWnd))) {
            IShellItem *psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR pszPath;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    outPath = pszPath;
                    ok = true;
                    CoTaskMemFree(pszPath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    return ok;
}

// ── Add .torrent file dialog ──
static void DoAddFile() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"Torrent Files (*.torrent)\0*.torrent\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        std::wstring customPath = L"";
        if (g_engine.isAskForFolder()) {
            if (!PromptForFolder(customPath)) return; // Canceled
        }

        if (!g_engine.addTorrentFile(file, customPath)) {
            MessageBoxW(g_hWnd, T(L"UI_ERR_TORRENT", L"Falha ao adicionar.").c_str(), L"Erro", MB_ICONERROR);
        }
    }
}

// ── Add magnet link dialog ──
static void DoAddMagnet() {
    static wchar_t magnetBuf[2048] = {};
    magnetBuf[0] = 0;

    struct { DLGTEMPLATE dlg; WORD menu, cls, title; } tmpl = {};
    tmpl.dlg.style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    tmpl.dlg.cx = 220;
    tmpl.dlg.cy = 70;

    struct DlgData { wchar_t* buf; };
    DlgData data = { magnetBuf };

    auto dlgProc = [](HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) -> INT_PTR {
        static DlgData* pData = nullptr;
        switch (msg) {
        case WM_INITDIALOG: {
            pData = (DlgData*)lp;
            SetWindowTextW(hDlg, T(L"UI_DLG_ADD_MAGNET", L"Adicionar Magnet Link").c_str());
            CreateWindowW(L"STATIC", T(L"UI_LBL_PASTE_MAGNET", L"Cole o magnet link:").c_str(),
                WS_CHILD | WS_VISIBLE, 10, 8, 300, 18, hDlg, nullptr, g_hInst, nullptr);
            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 28, 310, 22,
                hDlg, (HMENU)IDC_EDIT_MAGNET, g_hInst, nullptr);
            CreateWindowW(L"BUTTON", T(L"UI_BTN_OK", L"OK").c_str(),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 200, 58, 55, 24,
                hDlg, (HMENU)IDOK, g_hInst, nullptr);
            CreateWindowW(L"BUTTON", T(L"UI_BTN_CANCEL", L"Cancelar").c_str(),
                WS_CHILD | WS_VISIBLE, 260, 58, 60, 24,
                hDlg, (HMENU)IDCANCEL, g_hInst, nullptr);
            SetWindowPos(hDlg, nullptr, 0, 0, 340, 120, SWP_NOMOVE | SWP_NOZORDER);
            SetFocus(hEdit);
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                GetDlgItemTextW(hDlg, IDC_EDIT_MAGNET, pData->buf, 2048);
                EndDialog(hDlg, IDOK);
            } else if (LOWORD(wp) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        case WM_CLOSE: EndDialog(hDlg, IDCANCEL); return TRUE;
        }
        return FALSE;
    };

    if (DialogBoxIndirectParamW(g_hInst, &tmpl.dlg, g_hWnd, dlgProc, (LPARAM)&data) == IDOK && magnetBuf[0]) {
        std::wstring customPath = L"";
        if (g_engine.isAskForFolder()) {
            if (!PromptForFolder(customPath)) return;
        }

        if (!g_engine.addMagnetLink(magnetBuf, customPath)) {
            MessageBoxW(g_hWnd, T(L"UI_ERR_MAGNET", L"Magnet invalido.").c_str(), L"Erro", MB_ICONERROR);
        }
    }
}

// ── Settings dialog procedure ──
static const wchar_t* LangCodes[] = { L"pt", L"en", L"es", L"fr", L"de", L"ru", L"zh" };
static const wchar_t* LangNames[] = { L"Português", L"English", L"Español", L"Français", L"Deutsch", L"Русский", L"中文" };

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, T(L"UI_DLG_SETTINGS", L"Opcoes").c_str());
        
        // Translate labels in dialog (manually set by navigating child controls if needed, 
        // or just rely on the static text. Since we create them in the RC, we update them here:
        // Actually, we must use SetDlgItemText for the statics.
        // But since they share IDC_STATIC (-1), it's hard to get them unless we give them IDs.
        // It's easier to just let the user see the English/PT defaults until restarted, or give them IDs.
        // We'll update the ones we gave IDs, and leave the static ones. To fix properly, we'd need IDs in RC.
        // Let's just set the texts we can:
        SetDlgItemTextW(hDlg, IDOK, T(L"UI_BTN_SAVE", L"Salvar").c_str());
        SetDlgItemTextW(hDlg, IDCANCEL, T(L"UI_BTN_CANCEL", L"Cancelar").c_str());
        SetDlgItemTextW(hDlg, IDC_CHK_ASKFOLDER, T(L"UI_ASK_FOLDER", L"Sempre perguntar").c_str());
        SetDlgItemTextW(hDlg, IDC_CHK_AGGRESSIVE, T(L"UI_AGGRESSIVE", L"Modo Agressivo").c_str());

        SetDlgItemTextW(hDlg, IDC_EDIT_SAVEPATH, g_engine.getSavePath().c_str());
        SetDlgItemInt(hDlg, IDC_EDIT_MAXCONN, g_engine.getMaxConnections(), FALSE);
        CheckDlgButton(hDlg, IDC_CHK_AGGRESSIVE, g_engine.isAggressiveMode() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_ASKFOLDER, g_engine.isAskForFolder() ? BST_CHECKED : BST_UNCHECKED);

        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_LANG);
        std::wstring curLang = g_engine.getLanguage();
        for (int i = 0; i < 7; i++) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)LangNames[i]);
            if (curLang == LangCodes[i]) {
                SendMessageW(hCombo, CB_SETCURSEL, i, 0);
            }
        }
        if (SendMessageW(hCombo, CB_GETCURSEL, 0, 0) == CB_ERR) {
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0); // Default PT
        }

        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BTN_BROWSE) {
            std::wstring path;
            if (PromptForFolder(path)) {
                SetDlgItemTextW(hDlg, IDC_EDIT_SAVEPATH, path.c_str());
            }
        }
        else if (LOWORD(wp) == IDOK) {
            wchar_t path[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_SAVEPATH, path, MAX_PATH);
            g_engine.setSavePath(path);

            int maxConn = GetDlgItemInt(hDlg, IDC_EDIT_MAXCONN, NULL, FALSE);
            if (maxConn < 1) maxConn = 1;
            if (maxConn > 300) maxConn = 300;
            g_engine.setMaxConnections(maxConn);

            g_engine.setAggressiveMode(IsDlgButtonChecked(hDlg, IDC_CHK_AGGRESSIVE) == BST_CHECKED);
            g_engine.setAskForFolder(IsDlgButtonChecked(hDlg, IDC_CHK_ASKFOLDER) == BST_CHECKED);

            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_LANG);
            int sel = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR && sel < 7) {
                std::wstring oldLang = g_engine.getLanguage();
                std::wstring newLang = LangCodes[sel];
                g_engine.setLanguage(newLang);
                if (oldLang != newLang) {
                    LoadLanguage(newLang);
                    UpdateUIText(); // Apply translation immediately!
                }
            }

            EndDialog(hDlg, IDOK);
        }
        else if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

// ── Window procedure ──
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hFontUI = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        g_brRed = CreateSolidBrush(RGB(200, 40, 40));
        g_brGreen = CreateSolidBrush(RGB(40, 160, 60));

        int x = 8;
        auto mkBtn = [&](int id, int w) -> HWND {
            HWND h = CreateWindowW(L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 8, w, 30,
                hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            x += w + 6;
            return h;
        };
        g_hBtnAdd      = mkBtn(IDC_BTN_ADD_FILE, 130);
        g_hBtnMagnet   = mkBtn(IDC_BTN_ADD_MAGNET, 90);
        g_hBtnPause    = mkBtn(IDC_BTN_PAUSE, 110);
        g_hBtnRemove   = mkBtn(IDC_BTN_REMOVE, 70);
        g_hBtnSettings = mkBtn(IDC_BTN_SETTINGS, 70);

        g_hBtnKill = CreateWindowW(L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 8, 170, 30, hWnd, (HMENU)IDC_BTN_KILLUPLOAD, g_hInst, nullptr);

        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 44, 100, 100, hWnd, (HMENU)IDC_LISTVIEW, g_hInst, nullptr);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        InitListView();

        g_hStatus = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_SUNKEN,
            0, 0, 100, 22, hWnd, (HMENU)IDC_STATUSBAR, g_hInst, nullptr);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        wchar_t dlPath[MAX_PATH];
        SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, dlPath);
        std::wstring defSave = std::wstring(dlPath) + L"\\SimpleTorrent";

        wchar_t apPath[MAX_PATH];
        SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, apPath);
        std::wstring appData = std::wstring(apPath) + L"\\SimpleTorrent";
        CreateDirectoryW(appData.c_str(), nullptr);

        g_engine.initialize(appData);
        g_engine.loadState();
        
        LoadLanguage(g_engine.getLanguage()); // Load selected language
        UpdateUIText();

        if (g_engine.getSavePath().empty()) {
            g_engine.setSavePath(defSave);
            CreateDirectoryW(defSave.c_str(), nullptr);
        }

        SetTimer(hWnd, IDT_UPDATE, 1000, nullptr);
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        MoveWindow(g_hBtnKill, w - 178, 8, 170, 30, TRUE);
        MoveWindow(g_hList, 0, 44, w, h - 44 - 24, TRUE);
        MoveWindow(g_hStatus, 0, h - 24, w, 24, TRUE);
        return 0;
    }

    case WM_TIMER:
        if (wp == IDT_UPDATE) RefreshList();
        return 0;

    case WM_NOTIFY: {
        auto* nmhdr = (LPNMHDR)lp;
        if (nmhdr->idFrom == IDC_LISTVIEW && nmhdr->code == NM_CUSTOMDRAW) {
            auto* pcd = (LPNMLVCUSTOMDRAW)lp;
            switch (pcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                return CDRF_NOTIFYSUBITEMDRAW;
            case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                int itemIdx = (int)pcd->nmcd.dwItemSpec;
                int subItem = pcd->iSubItem;

                wchar_t stateText[64] = {};
                ListView_GetItemText(g_hList, itemIdx, 7, stateText, 64);
                std::wstring state(stateText);

                wchar_t progTextBuf[32] = {};
                ListView_GetItemText(g_hList, itemIdx, 1, progTextBuf, 32);
                
                float prog = 0.0f;
                swscanf(progTextBuf, L"%f%%", &prog);
                prog /= 100.0f;
                if (prog > 1.0f) prog = 1.0f;
                if (prog < 0.0f) prog = 0.0f;

                COLORREF barColor;
                if (state == T(L"UI_ST_PAUSED", L"Pausado")) {
                    pcd->clrText = RGB(220, 40, 40); 
                    barColor = pcd->clrText;
                } else if (state == T(L"UI_ST_COMPLETED", L"Completo") || state == T(L"UI_ST_SEEDING", L"Semeando") || prog >= 1.0f) {
                    pcd->clrText = RGB(40, 100, 220); 
                    barColor = pcd->clrText;
                } else {
                    pcd->clrText = RGB(20, 160, 40); 
                    barColor = pcd->clrText;
                }

                if (subItem == 1) { 
                    RECT rc;
                    ListView_GetSubItemRect(g_hList, itemIdx, subItem, LVIR_BOUNDS, &rc);
                    HDC hdc = pcd->nmcd.hdc;
                    
                    HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240));
                    FillRect(hdc, &rc, bgBrush);
                    DeleteObject(bgBrush);
                    
                    RECT rcBar = rc;
                    InflateRect(&rcBar, -2, -2);
                    int width = rcBar.right - rcBar.left;
                    rcBar.right = rcBar.left + (int)(width * prog);
                    HBRUSH barBrush = CreateSolidBrush(barColor);
                    FillRect(hdc, &rcBar, barBrush);
                    DeleteObject(barBrush);
                    
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(0, 0, 0));
                    DrawTextW(hdc, progTextBuf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    
                    return CDRF_SKIPDEFAULT;
                }
                return CDRF_DODEFAULT;
            }
            }
            return CDRF_DODEFAULT;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_ADD_FILE:  DoAddFile(); break;
        case IDC_BTN_ADD_MAGNET: DoAddMagnet(); break;
        case IDC_BTN_SETTINGS:
            DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_SETTINGS), hWnd, SettingsDlgProc, 0);
            break;
        case IDC_BTN_REMOVE: {
            int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
            if (sel >= 0) {
                int r = MessageBoxW(hWnd,
                    T(L"UI_MSG_ASK_DEL", L"Deseja apagar os arquivos baixados tambem?").c_str(),
                    T(L"UI_TITLE_REM", L"Remover Torrent").c_str(), 
                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDYES) g_engine.removeTorrent(sel, true);
                else if (r == IDNO) g_engine.removeTorrent(sel, false);
            }
            break;
        }
        case IDC_BTN_PAUSE: {
            int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
            if (sel >= 0) g_engine.pauseResumeTorrent(sel);
            break;
        }
        case IDC_BTN_KILLUPLOAD: {
            bool cur = g_engine.isUploadBlocked();
            g_engine.setUploadBlocked(!cur);
            UpdateKillButton();
            break;
        }
        }
        return 0;

    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID == IDC_BTN_KILLUPLOAD) {
            bool blocked = g_engine.isUploadBlocked();
            HBRUSH br = blocked ? g_brRed : g_brGreen;
            FillRect(dis->hDC, &dis->rcItem, br);

            HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
            HPEN oldPen = (HPEN)SelectObject(dis->hDC, pen);
            HBRUSH oldBr = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                      dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldPen);
            SelectObject(dis->hDC, oldBr);
            DeleteObject(pen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SelectObject(dis->hDC, g_hFontBold);
            const wchar_t* key = blocked ? L"UI_UP_BLOCKED" : L"UI_UP_ACTIVE";
            const wchar_t* def = blocked ? L"\x26D4 UPLOAD BLOQUEADO" : L"\x2705 UPLOAD ATIVO";
            DrawTextW(dis->hDC, T(key, def).c_str(), -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if (dis->itemState & ODS_FOCUS) {
                RECT rc = dis->rcItem;
                InflateRect(&rc, -3, -3);
                DrawFocusRect(dis->hDC, &rc);
            }
        }
        return TRUE;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize = { 760, 350 };
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, IDT_UPDATE);
        g_engine.saveState();
        g_engine.shutdown();
        DeleteObject(g_hFontUI);
        DeleteObject(g_hFontBold);
        DeleteObject(g_brRed);
        DeleteObject(g_brGreen);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ── Entry point ──
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    g_hInst = hInst;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SimpleTorrentWnd";
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, L"SimpleTorrentWnd", L"SimpleTorrent",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 860, 480,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
