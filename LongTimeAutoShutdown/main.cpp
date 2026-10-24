// ============================================================
//  AutoShutdown.cpp
//  鼠标闲置自动强制关机（Win7 ~ Win11，兼容触控屏）
//  编译：cl /EHsc /std:c++17 AutoShutdown.cpp user32.lib gdi32.lib
//        shell32.lib comctl32.lib advapi32.lib
// ============================================================

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>  
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib") 

// ============================================================
//  1. 迷你 JSON 库
// ============================================================
namespace mini {

    struct JValue {
        enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
        bool b = false;
        double n = 0;
        std::string s;
        std::vector<JValue> a;
        std::map<std::string, JValue> o;

        // 读取辅助
        double GetNum(const char* k, double def) const {
            auto it = o.find(k);
            return (it != o.end() && it->second.type == NUM) ? it->second.n : def;
        }
        bool GetBool(const char* k, bool def) const {
            auto it = o.find(k);
            return (it != o.end() && it->second.type == BOOL) ? it->second.b : def;
        }
        std::string GetStr(const char* k, const char* def) const {
            auto it = o.find(k);
            return (it != o.end() && it->second.type == STR) ? it->second.s : def;
        }
    };

    inline JValue JBool(bool v) { JValue j; j.type = JValue::BOOL; j.b = v; return j; }
    inline JValue JNum(double v) { JValue j; j.type = JValue::NUM;  j.n = v; return j; }
    inline JValue JStr(const std::string& v) { JValue j; j.type = JValue::STR; j.s = v; return j; }
    inline JValue JArr() { JValue j; j.type = JValue::ARR; return j; }
    inline JValue JObj() { JValue j; j.type = JValue::OBJ; return j; }

    // ---------- 解析 ----------
    class Parser {
    public:
        static bool Parse(const std::string& t, JValue& out, std::string& err) {
            const char* p = t.c_str();
            const char* e = p + t.size();
            Skip(p, e);
            if (!ParseValue(p, e, out, err)) return false;
            Skip(p, e);
            if (p != e) { err = "多余字符"; return false; }
            return true;
        }
    private:
        static void Skip(const char*& p, const char* e) {
            while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        }
        static bool ParseString(const char*& p, const char* e, std::string& out, std::string& err) {
            if (p >= e || *p != '"') { err = "期望字符串"; return false; }
            ++p;
            out.clear();
            while (p < e) {
                char c = *p++;
                if (c == '"') return true;
                if (c == '\\') {
                    if (p >= e) break;
                    char esc = *p++;
                    switch (esc) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (e - p < 4) { err = "\\u 转义不完整"; return false; }
                        char hex[5] = { p[0], p[1], p[2], p[3], 0 };
                        p += 4;
                        unsigned cp = (unsigned)strtoul(hex, nullptr, 16);
                        // 简易 UTF-8 编码
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: err = "未知转义"; return false;
                    }
                }
                else {
                    out += c;
                }
            }
            err = "字符串未结束"; return false;
        }
        static bool ParseNumber(const char*& p, const char* e, double& out, std::string& err) {
            const char* start = p;
            if (p < e && (*p == '-' || *p == '+')) ++p;
            while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' ||
                *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) ++p;
            if (start == p) { err = "无效数字"; return false; }
            std::string tmp(start, p);
            out = atof(tmp.c_str());
            return true;
        }
        static bool ParseValue(const char*& p, const char* e, JValue& v, std::string& err) {
            Skip(p, e);
            if (p >= e) { err = "意外结束"; return false; }
            char c = *p;
            if (c == '{') {
                v.type = JValue::OBJ; ++p; Skip(p, e);
                if (p < e && *p == '}') { ++p; return true; }
                while (p < e) {
                    Skip(p, e);
                    std::string key;
                    if (!ParseString(p, e, key, err)) return false;
                    Skip(p, e);
                    if (p >= e || *p != ':') { err = "缺少冒号"; return false; }
                    ++p;
                    JValue child;
                    if (!ParseValue(p, e, child, err)) return false;
                    v.o[key] = child;
                    Skip(p, e);
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == '}') { ++p; return true; }
                    err = "对象未结束"; return false;
                }
                err = "对象未结束"; return false;
            }
            if (c == '[') {
                v.type = JValue::ARR; ++p; Skip(p, e);
                if (p < e && *p == ']') { ++p; return true; }
                while (p < e) {
                    JValue child;
                    if (!ParseValue(p, e, child, err)) return false;
                    v.a.push_back(child);
                    Skip(p, e);
                    if (p < e && *p == ',') { ++p; continue; }
                    if (p < e && *p == ']') { ++p; return true; }
                    err = "数组未结束"; return false;
                }
                err = "数组未结束"; return false;
            }
            if (c == '"') { v.type = JValue::STR; return ParseString(p, e, v.s, err); }
            if (e - p >= 4 && !strncmp(p, "true", 4)) { v.type = JValue::BOOL; v.b = true;  p += 4; return true; }
            if (e - p >= 5 && !strncmp(p, "false", 5)) { v.type = JValue::BOOL; v.b = false; p += 5; return true; }
            if (e - p >= 4 && !strncmp(p, "null", 4)) { v.type = JValue::NUL;  p += 4; return true; }
            v.type = JValue::NUM;
            return ParseNumber(p, e, v.n, err);
        }
    };

    inline std::string EscapeStr(const std::string& s) {
        std::string r;
        for (char c : s) {
            switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); r += b; }
                else r += c;
            }
        }
        return r;
    }

    inline std::string Serialize(const JValue& v, int indent = 0) {
        std::string pad(indent * 2, ' ');
        std::string pad2((indent + 1) * 2, ' ');
        switch (v.type) {
        case JValue::NUL: return "null";
        case JValue::BOOL: return v.b ? "true" : "false";
        case JValue::NUM: {
            char buf[64];
            double d = v.n;
            if (d == (double)(long long)d) sprintf_s(buf, "%lld", (long long)d);
            else sprintf_s(buf, "%.6g", d);
            return buf;
        }
        case JValue::STR: return "\"" + EscapeStr(v.s) + "\"";
        case JValue::ARR: {
            if (v.a.empty()) return "[]";
            std::string s = "[\n";
            for (size_t i = 0; i < v.a.size(); ++i) {
                s += pad2 + Serialize(v.a[i], indent + 1);
                if (i + 1 < v.a.size()) s += ",";
                s += "\n";
            }
            return s + pad + "]";
        }
        case JValue::OBJ: {
            if (v.o.empty()) return "{}";
            std::string s = "{\n";
            size_t i = 0;
            for (auto& kv : v.o) {
                s += pad2 + "\"" + EscapeStr(kv.first) + "\": " + Serialize(kv.second, indent + 1);
                if (++i < v.o.size()) s += ",";
                s += "\n";
            }
            return s + pad + "}";
        }
        }
        return "null";
    }

} // namespace mini

// ============================================================
//  2. 编码工具
// ============================================================
static std::string W2U8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring U82W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ============================================================
//  3. 配置
// ============================================================
struct Config {
    bool        enabled = true;
    int         countdownSeconds = 300;
    bool        countdownVisible = true;
    std::string positionMode = "topright"; // topleft/topright/bottomleft/bottomright/center/custom
    int         customX = 100;
    int         customY = 100;
    int         fontSize = 48;
    std::string textColor = "#FF4040";
    std::string bgColor = "#000000";
    int         bgAlpha = 200;        // 30~255
    bool        autoStart = true;
    bool        keyboardCounts = false;
    bool        warnFlash = true;
    int         warnSeconds = 10;
    bool        hideToTrayOnClose = true;
};

static Config g_cfg;
static bool   g_loading = false;

// ============================================================
//  4. 全局
// ============================================================
static HINSTANCE        g_hInst = nullptr;
static HWND             g_hMain = nullptr;
static HWND             g_hOverlay = nullptr;
static HHOOK            g_hMouseHook = nullptr;
static HHOOK            g_hKbHook = nullptr;
static NOTIFYICONDATAW  g_nid = {};
static HFONT            g_hUiFont = nullptr;
static ULONGLONG        g_lastActivity = 0;
static bool g_controlsCreated = false;
static bool             g_shutdownTrig = false;
static bool             g_previewMode = false;
static ULONGLONG        g_previewEnd = 0;
static COLORREF         g_custColors[16] = {};
static std::wstring     g_lastOverlayText;
static int              g_lastOX = -1, g_lastOY = -1, g_lastOW = -1, g_lastOH = -1;

#define WM_TRAYMSG   (WM_APP + 1)
#define TIMER_TICK   1

#define IDC_CHK_ENABLED    1001
#define IDC_EDIT_SECONDS   1002
#define IDC_COMBO_POS      1003
#define IDC_EDIT_X         1004
#define IDC_EDIT_Y         1005
#define IDC_EDIT_FONT      1006
#define IDC_BTN_TCOLOR     1007
#define IDC_BTN_BCOLOR     1008
#define IDC_EDIT_ALPHA     1009
#define IDC_CHK_VISIBLE    1010
#define IDC_CHK_KEYBOARD   1011
#define IDC_CHK_WARN       1012
#define IDC_EDIT_WARNSEC   1013
#define IDC_CHK_AUTOSTART  1014
#define IDC_STATIC_STATUS  1015
#define IDC_BTN_PREVIEW    1016
#define IDC_BTN_TRAY       1017
#define IDC_BTN_QUIT       1018
#define IDC_LBL_TCOLOR     1019
#define IDC_LBL_BCOLOR     1020
#define IDC_MENU_SHOW      2001
#define IDC_MENU_TOGGLE    2002
#define IDC_MENU_OVERLAY   2003
#define IDC_MENU_EXIT      2004

// ============================================================
//  5. 路径 / 文件
// ============================================================
static std::wstring GetExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    size_t p = s.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? L"." : s.substr(0, p);
}
static std::wstring GetConfigPath() {
    return GetExeDir() + L"\\config.json";
}

// ============================================================
//  6. 颜色
// ============================================================
static COLORREF ParseColor(const std::string& hex) {
    unsigned r = 255, g = 0, b = 0;
    if (hex.size() >= 7 && hex[0] == '#') {
        r = (unsigned)strtoul(hex.substr(1, 2).c_str(), nullptr, 16);
        g = (unsigned)strtoul(hex.substr(3, 2).c_str(), nullptr, 16);
        b = (unsigned)strtoul(hex.substr(5, 2).c_str(), nullptr, 16);
    }
    return RGB(r, g, b);
}
static std::string FormatColor(COLORREF c) {
    char buf[16];
    sprintf_s(buf, "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

// ============================================================
//  7. 配置保存 / 加载
// ============================================================
static void SaveConfig() {
    using namespace mini;
    JValue root = JObj();
    root.o["enabled"] = JBool(g_cfg.enabled);
    root.o["countdownSeconds"] = JNum(g_cfg.countdownSeconds);
    root.o["countdownVisible"] = JBool(g_cfg.countdownVisible);
    root.o["positionMode"] = JStr(g_cfg.positionMode);
    root.o["customX"] = JNum(g_cfg.customX);
    root.o["customY"] = JNum(g_cfg.customY);
    root.o["fontSize"] = JNum(g_cfg.fontSize);
    root.o["textColor"] = JStr(g_cfg.textColor);
    root.o["bgColor"] = JStr(g_cfg.bgColor);
    root.o["bgAlpha"] = JNum(g_cfg.bgAlpha);
    root.o["autoStart"] = JBool(g_cfg.autoStart);
    root.o["keyboardCounts"] = JBool(g_cfg.keyboardCounts);
    root.o["warnFlash"] = JBool(g_cfg.warnFlash);
    root.o["warnSeconds"] = JNum(g_cfg.warnSeconds);
    root.o["hideToTrayOnClose"] = JBool(g_cfg.hideToTrayOnClose);

    std::string text = Serialize(root);
    FILE* fp = _wfopen(GetConfigPath().c_str(), L"wb");
    if (fp) {
        fwrite(text.data(), 1, text.size(), fp);
        fclose(fp);
    }
}

static void LoadConfig() {
    using namespace mini;
    FILE* fp = _wfopen(GetConfigPath().c_str(), L"rb");
    if (!fp) { SaveConfig(); return; }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
    fclose(fp);

    JValue root;
    std::string err;
    if (!Parser::Parse(text, root, err)) return;

    g_cfg.enabled = root.GetBool("enabled", g_cfg.enabled);
    g_cfg.countdownSeconds = (int)root.GetNum("countdownSeconds", g_cfg.countdownSeconds);
    g_cfg.countdownVisible = root.GetBool("countdownVisible", g_cfg.countdownVisible);
    g_cfg.positionMode = root.GetStr("positionMode", g_cfg.positionMode.c_str());
    g_cfg.customX = (int)root.GetNum("customX", g_cfg.customX);
    g_cfg.customY = (int)root.GetNum("customY", g_cfg.customY);
    g_cfg.fontSize = (int)root.GetNum("fontSize", g_cfg.fontSize);
    g_cfg.textColor = root.GetStr("textColor", g_cfg.textColor.c_str());
    g_cfg.bgColor = root.GetStr("bgColor", g_cfg.bgColor.c_str());
    g_cfg.bgAlpha = (int)root.GetNum("bgAlpha", g_cfg.bgAlpha);
    g_cfg.autoStart = root.GetBool("autoStart", g_cfg.autoStart);
    g_cfg.keyboardCounts = root.GetBool("keyboardCounts", g_cfg.keyboardCounts);
    g_cfg.warnFlash = root.GetBool("warnFlash", g_cfg.warnFlash);
    g_cfg.warnSeconds = (int)root.GetNum("warnSeconds", g_cfg.warnSeconds);
    g_cfg.hideToTrayOnClose = root.GetBool("hideToTrayOnClose", g_cfg.hideToTrayOnClose);

    if (g_cfg.countdownSeconds < 300)   g_cfg.countdownSeconds = 300;
    if (g_cfg.fontSize < 10)            g_cfg.fontSize = 10;
    if (g_cfg.fontSize > 400)           g_cfg.fontSize = 400;
    if (g_cfg.bgAlpha < 30)             g_cfg.bgAlpha = 30;
    if (g_cfg.bgAlpha > 255)            g_cfg.bgAlpha = 255;
}

// ============================================================
//  8. 管理员权限 / 自启动
// ============================================================
static bool IsElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;
    TOKEN_ELEVATION te = {};
    DWORD sz = sizeof(te);
    bool ok = false;
    if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &sz))
        ok = (te.TokenIsElevated != 0);
    CloseHandle(hToken);
    return ok;
}

static void RelaunchAsAdmin() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    if (!ShellExecuteExW(&sei)) {
        MessageBoxW(nullptr, L"本程序需要管理员权限才能正常关机。\n请右键选择“以管理员身份运行”。",
            L"权限不足", MB_ICONERROR);
    }
}

static void ApplyAutoStart(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring val = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(hKey, L"AutoShutdownTimer", 0, REG_SZ,
            (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    }
    else {
        RegDeleteValueW(hKey, L"AutoShutdownTimer");
    }
    RegCloseKey(hKey);
}

// ============================================================
//  9. 强制关机
// ============================================================
static bool EnableShutdownPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    ok = ok && (GetLastError() == ERROR_SUCCESS);
    CloseHandle(hToken);
    return ok != FALSE;
}

static void ForceShutdown() {
    EnableShutdownPrivilege();
    SaveConfig();

    // 优先使用 ExitWindowsEx，EWX_POWEROFF 明确表示"关机"，不会重启
    BOOL ok = ExitWindowsEx(
        EWX_POWEROFF | EWX_FORCE | EWX_FORCEIFHUNG,
        SHTDN_REASON_MAJOR_APPLICATION |
        SHTDN_REASON_MINOR_OTHER |
        SHTDN_REASON_FLAG_PLANNED);

    if (!ok) {
        // 备用方案：注意 bRebootAfterShutdown 必须是 FALSE（原来误写为 TRUE 导致重启）
        InitiateSystemShutdownExW(
            nullptr,
            (LPWSTR)L"倒计时结束，系统将强制关机。",
            0,
            TRUE,        // bForceAppsClosed
            FALSE,       // bRebootAfterShutdown ← 关键，FALSE = 关机
            SHTDN_REASON_MAJOR_APPLICATION |
            SHTDN_REASON_MINOR_OTHER |
            SHTDN_REASON_FLAG_PLANNED);
    }
}

// ============================================================
//  10. 空闲检测（底层鼠标 / 键盘钩子）
// ============================================================
static void MarkActivity() {
    g_lastActivity = GetTickCount64();
    g_shutdownTrig = false;
}

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lParam;
        switch (wParam) {
        case WM_MOUSEMOVE: {
            static POINT lastPt = { -99999, -99999 };
            if (m->pt.x != lastPt.x || m->pt.y != lastPt.y) {
                lastPt = m->pt;
                MarkActivity();
            }
            break;
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
            MarkActivity();
            break;
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK KbProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_cfg.keyboardCounts) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) MarkActivity();
    }
    return CallNextHookEx(g_hKbHook, nCode, wParam, lParam);
}

static void InstallHooks() {
    if (!g_hMouseHook)
        g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    if (!g_hKbHook)
        g_hKbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, GetModuleHandleW(nullptr), 0);
}
static void UninstallHooks() {
    if (g_hMouseHook) { UnhookWindowsHookEx(g_hMouseHook); g_hMouseHook = nullptr; }
    if (g_hKbHook) { UnhookWindowsHookEx(g_hKbHook);    g_hKbHook = nullptr; }
}

// ============================================================
//  11. 倒计时浮层
// ============================================================
static HFONT MakeOverlayFont() {
    return CreateFontW(g_cfg.fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

static void CalcOverlayPos(int w, int h, int& x, int& y) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    const int M = 24;
    const std::string& mode = g_cfg.positionMode;
    if (mode == "topleft") { x = M;             y = M; }
    else if (mode == "topright") { x = sw - w - M;    y = M; }
    else if (mode == "bottomleft") { x = M;             y = sh - h - M; }
    else if (mode == "bottomright") { x = sw - w - M;    y = sh - h - M; }
    else if (mode == "center") { x = (sw - w) / 2;  y = (sh - h) / 2; }
    else { x = g_cfg.customX; y = g_cfg.customY; }
}

static void UpdateOverlay(ULONGLONG msLeft, bool preview) {
    if (!preview && !g_cfg.countdownVisible) {
        ShowWindow(g_hOverlay, SW_HIDE);
        return;
    }
    int totalSec = (int)((msLeft + 999) / 1000);
    wchar_t buf[64];
    if (totalSec >= 3600)
        wsprintfW(buf, L"%02d:%02d:%02d", totalSec / 3600, (totalSec % 3600) / 60, totalSec % 60);
    else
        wsprintfW(buf, L"%02d:%02d", totalSec / 60, totalSec % 60);

    // 测量文本
    HDC hdc = GetDC(nullptr);
    HFONT f = MakeOverlayFont();
    HGDIOBJ old = SelectObject(hdc, f);
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
    SelectObject(hdc, old);
    DeleteObject(f);
    ReleaseDC(nullptr, hdc);

    int padX = 28, padY = 14;
    int w = sz.cx + padX * 2;
    int h = sz.cy + padY * 2;
    int x, y;
    CalcOverlayPos(w, h, x, y);

    bool geomChanged = (w != g_lastOW || h != g_lastOH || x != g_lastOX || y != g_lastOY);
    if (geomChanged) {
        SetWindowPos(g_hOverlay, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_lastOX = x; g_lastOY = y; g_lastOW = w; g_lastOH = h;
    }
    else {
        SetWindowPos(g_hOverlay, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_hOverlay, SW_SHOWNOACTIVATE);
    }

    SetLayeredWindowAttributes(g_hOverlay, 0, (BYTE)g_cfg.bgAlpha, LWA_ALPHA);

    if (g_lastOverlayText != buf) {
        g_lastOverlayText = buf;
        InvalidateRect(g_hOverlay, nullptr, FALSE);
    }
    else {
        InvalidateRect(g_hOverlay, nullptr, FALSE);
    }
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(ParseColor(g_cfg.bgColor));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);

        COLORREF tc = ParseColor(g_cfg.textColor);
        // 警告闪烁
        ULONGLONG now = GetTickCount64();
        bool warn = g_cfg.warnFlash && g_cfg.enabled && !g_previewMode &&
            !g_shutdownTrig &&
            (g_lastActivity + (ULONGLONG)g_cfg.countdownSeconds * 1000ULL > now) &&
            ((g_lastActivity + (ULONGLONG)g_cfg.countdownSeconds * 1000ULL) - now)
            <= (ULONGLONG)g_cfg.warnSeconds * 1000ULL;
        if (warn && ((now / 400) % 2 == 0))
            tc = ParseColor(g_cfg.bgColor);

        SetTextColor(hdc, tc);

        HFONT f = MakeOverlayFont();
        HGDIOBJ old = SelectObject(hdc, f);
        DrawTextW(hdc, g_lastOverlayText.c_str(), -1, &rc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
//  12. 状态栏刷新
// ============================================================
static void UpdateStatusText() {
    if (!g_hMain) return;
    wchar_t buf[512] = {};
    ULONGLONG now = GetTickCount64();
    if (!g_cfg.enabled) {
        wsprintfW(buf, L"状态：已禁用（不会自动关机）");
    }
    else {
        ULONGLONG total = (ULONGLONG)g_cfg.countdownSeconds * 1000ULL;
        ULONGLONG idle = now - g_lastActivity;
        int idleSec = (int)(idle / 1000);
        if (idle >= total) {
            wsprintfW(buf, L"状态：倒计时结束，正在强制关机…");
        }
        else {
            int leftSec = (int)((total - idle) / 1000);
            wsprintfW(buf,
                L"状态：已启用\n"
                L"当前空闲：%d 秒 / 总计 %d 秒\n"
                L"距离强制关机还有：%d 秒",
                idleSec, g_cfg.countdownSeconds, leftSec);
        }
    }
    SetDlgItemTextW(g_hMain, IDC_STATIC_STATUS, buf);
}

// ============================================================
//  13. 从控件同步配置
// ============================================================
static int GetEditInt(int id, int def) {
    wchar_t buf[32] = {};
    GetDlgItemTextW(g_hMain, id, buf, 32);
    if (buf[0] == 0) return def;
    return _wtoi(buf);
}

static void SyncFromControls() {
    if (g_loading || !g_hMain) return;

    g_cfg.enabled = (IsDlgButtonChecked(g_hMain, IDC_CHK_ENABLED) == BST_CHECKED);

    int sec = GetEditInt(IDC_EDIT_SECONDS, g_cfg.countdownSeconds);
    if (sec < 300) sec = 300;   // 下限 300 秒
    g_cfg.countdownSeconds = sec;

    int pos = (int)SendDlgItemMessageW(g_hMain, IDC_COMBO_POS, CB_GETCURSEL, 0, 0);
    static const char* modes[] = { "topleft", "topright", "bottomleft", "bottomright", "center", "custom" };
    if (pos >= 0 && pos < 6) g_cfg.positionMode = modes[pos];

    g_cfg.customX = GetEditInt(IDC_EDIT_X, g_cfg.customX);
    g_cfg.customY = GetEditInt(IDC_EDIT_Y, g_cfg.customY);

    int fs = GetEditInt(IDC_EDIT_FONT, g_cfg.fontSize);
    if (fs < 10)  fs = 10;
    if (fs > 400) fs = 400;
    g_cfg.fontSize = fs;

    int al = GetEditInt(IDC_EDIT_ALPHA, g_cfg.bgAlpha);
    if (al < 30)  al = 30;
    if (al > 255) al = 255;
    g_cfg.bgAlpha = al;

    g_cfg.countdownVisible = (IsDlgButtonChecked(g_hMain, IDC_CHK_VISIBLE) == BST_CHECKED);
    g_cfg.keyboardCounts = (IsDlgButtonChecked(g_hMain, IDC_CHK_KEYBOARD) == BST_CHECKED);
    g_cfg.warnFlash = (IsDlgButtonChecked(g_hMain, IDC_CHK_WARN) == BST_CHECKED);

    int ws = GetEditInt(IDC_EDIT_WARNSEC, g_cfg.warnSeconds);
    if (ws < 0)   ws = 0;
    if (ws > 600) ws = 600;
    g_cfg.warnSeconds = ws;

    bool autoStart = (IsDlgButtonChecked(g_hMain, IDC_CHK_AUTOSTART) == BST_CHECKED);
    if (autoStart != g_cfg.autoStart) {
        g_cfg.autoStart = autoStart;
        ApplyAutoStart(autoStart);
    }

    SaveConfig();
    UpdateStatusText();
}

// ============================================================
//  14. 控件创建 / 应用配置到界面
// ============================================================
static HWND MkCtl(const wchar_t* cls, const wchar_t* text, DWORD style,
    int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, g_hMain, (HMENU)(INT_PTR)id, g_hInst, nullptr);
}

static void LayoutControls(HWND hwnd) {
    if (!g_controlsCreated) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;
    if (cw <= 0 || ch <= 0) return;

    const int margin = 15;
    const int btnH = 32;
    const int gap = 12;

    // 底部三个按钮自适应宽度
    int btnY = ch - margin - btnH;
    int btnW = (cw - margin * 2 - gap * 2) / 3;
    if (btnW < 90) btnW = 90;

    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_PREVIEW), nullptr,
        margin, btnY, btnW, btnH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_TRAY), nullptr,
        margin + btnW + gap, btnY, btnW, btnH, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_QUIT), nullptr,
        margin + (btnW + gap) * 2, btnY, btnW, btnH, SWP_NOZORDER);

    // 状态栏拉伸
    int statusY = 325;
    int statusH = btnY - margin - statusY;
    if (statusH < 70) statusH = 70;
    SetWindowPos(GetDlgItem(hwnd, IDC_STATIC_STATUS), nullptr,
        margin, statusY, cw - margin * 2, statusH, SWP_NOZORDER);
}

static void CreateControls(HWND hwnd) {
    g_hUiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

    MkCtl(L"BUTTON", L"启用强制关机", BS_AUTOCHECKBOX, 15, 12, 300, 24, IDC_CHK_ENABLED);

    MkCtl(L"STATIC", L"倒计时时间（秒）：", SS_LEFT, 15, 50, 130, 22, -1);
    MkCtl(L"EDIT", L"", ES_NUMBER | WS_BORDER, 150, 48, 90, 24, IDC_EDIT_SECONDS);
    MkCtl(L"STATIC", L"（鼠标/触控无操作后开始计时）", SS_LEFT, 250, 52, 250, 20, -1);

    MkCtl(L"STATIC", L"显示位置：", SS_LEFT, 15, 86, 70, 22, -1);
    HWND hCombo = MkCtl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
        88, 84, 140, 200, IDC_COMBO_POS);
    const wchar_t* posItems[] = { L"左上角", L"右上角", L"左下角", L"右下角", L"居中", L"自定义坐标" };
    for (auto it : posItems) SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)it);

    MkCtl(L"STATIC", L"X:", SS_LEFT, 240, 88, 20, 20, -1);
    MkCtl(L"EDIT", L"", ES_NUMBER | WS_BORDER, 260, 84, 70, 24, IDC_EDIT_X);
    MkCtl(L"STATIC", L"Y:", SS_LEFT, 340, 88, 20, 20, -1);
    MkCtl(L"EDIT", L"", ES_NUMBER | WS_BORDER, 360, 84, 70, 24, IDC_EDIT_Y);

    MkCtl(L"STATIC", L"字号：", SS_LEFT, 15, 122, 40, 22, -1);
    MkCtl(L"EDIT", L"", ES_NUMBER | WS_BORDER, 58, 118, 60, 24, IDC_EDIT_FONT);
    MkCtl(L"STATIC", L"不透明度（30-255）：", SS_LEFT, 135, 122, 140, 22, -1);
    MkCtl(L"EDIT", L"", ES_NUMBER | WS_BORDER, 275, 118, 60, 24, IDC_EDIT_ALPHA);

    MkCtl(L"STATIC", L"文字颜色：", SS_LEFT, 15, 158, 75, 22, -1);
    MkCtl(L"BUTTON", L"选择…", BS_PUSHBUTTON, 90, 154, 70, 26, IDC_BTN_TCOLOR);
    MkCtl(L"STATIC", L"", SS_OWNERDRAW | WS_BORDER, 168, 156, 40, 22, IDC_LBL_TCOLOR);

    MkCtl(L"STATIC", L"背景颜色：", SS_LEFT, 225, 158, 75, 22, -1);
    MkCtl(L"BUTTON", L"选择…", BS_PUSHBUTTON, 300, 154, 70, 26, IDC_BTN_BCOLOR);
    MkCtl(L"STATIC", L"", SS_OWNERDRAW | WS_BORDER, 378, 156, 40, 22, IDC_LBL_BCOLOR);

    MkCtl(L"BUTTON", L"在屏幕上显示倒计时浮层", BS_AUTOCHECKBOX, 15, 195, 300, 24, IDC_CHK_VISIBLE);
    MkCtl(L"BUTTON", L"键盘输入也算作“使用电脑”", BS_AUTOCHECKBOX, 15, 225, 300, 24, IDC_CHK_KEYBOARD);
    MkCtl(L"BUTTON", L"最后", BS_AUTOCHECKBOX, 15, 255, 60, 24, IDC_CHK_WARN);
    MkCtl(L"EDIT", L"", ES_NUMBER | WS_BORDER, 62, 256, 60, 24, IDC_EDIT_WARNSEC);
    MkCtl(L"STATIC", L"秒闪烁警告", SS_LEFT, 128, 259, 120, 22, -1);

    MkCtl(L"BUTTON", L"开机自启动", BS_AUTOCHECKBOX, 15, 290, 300, 24, IDC_CHK_AUTOSTART);

    MkCtl(L"STATIC", L"", SS_LEFT, 15, 325, 480, 70, IDC_STATIC_STATUS);

    MkCtl(L"BUTTON", L"预览倒计时", BS_PUSHBUTTON, 15, 405, 130, 32, IDC_BTN_PREVIEW);
    MkCtl(L"BUTTON", L"隐藏到托盘", BS_PUSHBUTTON, 160, 405, 130, 32, IDC_BTN_TRAY);
    MkCtl(L"BUTTON", L"退出程序", BS_PUSHBUTTON, 305, 405, 130, 32, IDC_BTN_QUIT);

    // 统一字体
    EnumChildWindows(hwnd, [](HWND c, LPARAM f) -> BOOL {
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return TRUE;
        }, (LPARAM)g_hUiFont);

    g_controlsCreated = true;
    LayoutControls(hwnd);
}

static void ApplyConfigToControls() {
    g_loading = true;
    CheckDlgButton(g_hMain, IDC_CHK_ENABLED,
        g_cfg.enabled ? BST_CHECKED : BST_UNCHECKED);

    wchar_t buf[32];
    wsprintfW(buf, L"%d", g_cfg.countdownSeconds);
    SetDlgItemTextW(g_hMain, IDC_EDIT_SECONDS, buf);

    static const char* modes[] = { "topleft", "topright", "bottomleft", "bottomright", "center", "custom" };
    int idx = 5;
    for (int i = 0; i < 6; ++i) if (g_cfg.positionMode == modes[i]) { idx = i; break; }
    SendDlgItemMessageW(g_hMain, IDC_COMBO_POS, CB_SETCURSEL, idx, 0);

    wsprintfW(buf, L"%d", g_cfg.customX);
    SetDlgItemTextW(g_hMain, IDC_EDIT_X, buf);
    wsprintfW(buf, L"%d", g_cfg.customY);
    SetDlgItemTextW(g_hMain, IDC_EDIT_Y, buf);
    wsprintfW(buf, L"%d", g_cfg.fontSize);
    SetDlgItemTextW(g_hMain, IDC_EDIT_FONT, buf);
    wsprintfW(buf, L"%d", g_cfg.bgAlpha);
    SetDlgItemTextW(g_hMain, IDC_EDIT_ALPHA, buf);
    wsprintfW(buf, L"%d", g_cfg.warnSeconds);
    SetDlgItemTextW(g_hMain, IDC_EDIT_WARNSEC, buf);

    CheckDlgButton(g_hMain, IDC_CHK_VISIBLE,
        g_cfg.countdownVisible ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hMain, IDC_CHK_KEYBOARD,
        g_cfg.keyboardCounts ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hMain, IDC_CHK_WARN,
        g_cfg.warnFlash ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hMain, IDC_CHK_AUTOSTART,
        g_cfg.autoStart ? BST_CHECKED : BST_UNCHECKED);

    // 颜色预览块
    InvalidateRect(GetDlgItem(g_hMain, IDC_LBL_TCOLOR), nullptr, TRUE);
    InvalidateRect(GetDlgItem(g_hMain, IDC_LBL_BCOLOR), nullptr, TRUE);
    g_loading = false;
    UpdateStatusText();
}

// ============================================================
//  15. 托盘
// ============================================================
static void AddTrayIcon() {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYMSG;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"自动强制关机助手", 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void UpdateTrayTip() {
    wchar_t tip[128];
    if (g_cfg.enabled)
        lstrcpynW(tip, L"自动强制关机助手（已启用）", 128);
    else
        lstrcpynW(tip, L"自动强制关机助手（已禁用）", 128);
    lstrcpynW(g_nid.szTip, tip, 128);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDC_MENU_SHOW, L"显示主窗口");
    AppendMenuW(menu, MF_STRING | (g_cfg.enabled ? MF_CHECKED : 0),
        IDC_MENU_TOGGLE, L"启用强制关机");
    AppendMenuW(menu, MF_STRING | (g_cfg.countdownVisible ? MF_CHECKED : 0),
        IDC_MENU_OVERLAY, L"显示倒计时浮层");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDC_MENU_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hMain);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        pt.x, pt.y, 0, g_hMain, nullptr);
    PostMessage(g_hMain, WM_NULL, 0, 0);   // ← 新增：让菜单可靠关闭
    DestroyMenu(menu);
}

// ============================================================
//  16. 主窗口过程
// ============================================================
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hMain = hwnd;
        CreateControls(hwnd);
        ApplyConfigToControls();
        UpdateTrayTip();
        return 0;

    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 560;
        mmi->ptMinTrackSize.y = 540;
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // ===== 托盘右键菜单 =====
        if (id == IDC_MENU_SHOW) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return 0;
        }
        if (id == IDC_MENU_TOGGLE) {
            g_cfg.enabled = !g_cfg.enabled;
            CheckDlgButton(hwnd, IDC_CHK_ENABLED,
                g_cfg.enabled ? BST_CHECKED : BST_UNCHECKED);
            SaveConfig();
            UpdateTrayTip();
            UpdateStatusText();
            return 0;
        }
        if (id == IDC_MENU_OVERLAY) {
            g_cfg.countdownVisible = !g_cfg.countdownVisible;
            CheckDlgButton(hwnd, IDC_CHK_VISIBLE,
                g_cfg.countdownVisible ? BST_CHECKED : BST_UNCHECKED);
            SaveConfig();
            if (!g_cfg.countdownVisible) ShowWindow(g_hOverlay, SW_HIDE);
            return 0;
        }
        if (id == IDC_MENU_EXIT) {
            // 直接销毁窗口，绕过 WM_CLOSE 的"隐藏到托盘"逻辑
            DestroyWindow(hwnd);
            return 0;
        }

        // ===== 主窗口按钮 =====
        if (id == IDC_BTN_TCOLOR) {
            COLORREF cur = ParseColor(g_cfg.textColor);
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hwnd;
            cc.rgbResult = cur;
            cc.lpCustColors = g_custColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_cfg.textColor = FormatColor(cc.rgbResult);
                SaveConfig();
                InvalidateRect(GetDlgItem(hwnd, IDC_LBL_TCOLOR), nullptr, TRUE);
                InvalidateRect(g_hOverlay, nullptr, FALSE);
            }
            return 0;
        }
        if (id == IDC_BTN_BCOLOR) {
            COLORREF cur = ParseColor(g_cfg.bgColor);
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hwnd;
            cc.rgbResult = cur;
            cc.lpCustColors = g_custColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_cfg.bgColor = FormatColor(cc.rgbResult);
                SaveConfig();
                InvalidateRect(GetDlgItem(hwnd, IDC_LBL_BCOLOR), nullptr, TRUE);
                InvalidateRect(g_hOverlay, nullptr, FALSE);
            }
            return 0;
        }
        if (id == IDC_BTN_PREVIEW) {
            g_previewMode = true;
            g_previewEnd = GetTickCount64() + 8000;
            g_lastOX = g_lastOY = g_lastOW = g_lastOH = -1;
            return 0;
        }
        if (id == IDC_BTN_TRAY) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (id == IDC_BTN_QUIT) {
            DestroyWindow(hwnd);
            return 0;
        }

        // ===== 编辑框 / 复选框 / 下拉框 实时同步 =====
        if (code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) {
            SyncFromControls();
            UpdateTrayTip();
            g_lastOX = g_lastOY = g_lastOW = g_lastOH = -1;
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_LBL_TCOLOR || dis->CtlID == IDC_LBL_BCOLOR) {
            COLORREF c = (dis->CtlID == IDC_LBL_TCOLOR)
                ? ParseColor(g_cfg.textColor)
                : ParseColor(g_cfg.bgColor);
            HBRUSH br = CreateSolidBrush(c);
            FillRect(dis->hDC, &dis->rcItem, br);
            DeleteObject(br);
            FrameRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == TIMER_TICK) {
            ULONGLONG now = GetTickCount64();

            if (g_previewMode) {
                if (now >= g_previewEnd) {
                    g_previewMode = false;
                    ShowWindow(g_hOverlay, SW_HIDE);
                }
                else {
                    UpdateOverlay(g_previewEnd - now, true);
                }
                UpdateStatusText();
                return 0;
            }

            if (!g_cfg.enabled) {
                ShowWindow(g_hOverlay, SW_HIDE);
                UpdateStatusText();
                return 0;
            }

            ULONGLONG total = (ULONGLONG)g_cfg.countdownSeconds * 1000ULL;
            ULONGLONG elapsed = now - g_lastActivity;

            if (elapsed >= total) {
                if (!g_shutdownTrig) {
                    g_shutdownTrig = true;
                    ForceShutdown();
                }
            }
            else {
                UpdateOverlay(total - elapsed, false);
            }
            UpdateStatusText();
        }
        return 0;

    case WM_TRAYMSG:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONDBLCLK:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
        case WM_RBUTTONUP:
            ShowTrayMenu();
            break;
        }
        return 0;

    case WM_CLOSE:
        if (g_cfg.hideToTrayOnClose) {
            ShowWindow(hwnd, SW_HIDE);
        }
        else {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        SaveConfig();
        UninstallHooks();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        KillTimer(hwnd, TIMER_TICK);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
//  17. 入口
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 1) 管理员权限
    if (!IsElevated()) {
        RelaunchAsAdmin();
        return 0;
    }

    // 2) 单实例
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\AutoShutdownTimer_SingleInstance");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(L"AutoShutdownMainWnd", nullptr);
        if (old) {
            ShowWindow(old, SW_RESTORE);
            SetForegroundWindow(old);
        }
        return 0;
    }

    g_hInst = hInst;
    SetProcessDPIAware();

    // 3) 加载配置
    LoadConfig();
    ApplyAutoStart(g_cfg.autoStart);

    // 4) 初始化
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    for (int i = 0; i < 16; ++i) g_custColors[i] = RGB(255, 255, 255);

    // 5) 注册窗口类
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AutoShutdownMainWnd";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    WNDCLASSEXW wc2 = { sizeof(wc2) };
    wc2.lpfnWndProc = OverlayProc;
    wc2.hInstance = hInst;
    wc2.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc2.lpszClassName = L"AutoShutdownOverlay";
    RegisterClassExW(&wc2);

    // 6) 创建主窗口（居中）
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 580, wh = 600;   // 加大初始尺寸，配合 WM_SIZE 布局
    g_hMain = CreateWindowExW(WS_EX_APPWINDOW, L"AutoShutdownMainWnd",
        L"自动强制关机助手",
        WS_OVERLAPPEDWINDOW,   // 含 WS_THICKFRAME | WS_MAXIMIZEBOX
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hMain) return 0;

    // 7) 创建浮层
    g_hOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"AutoShutdownOverlay", L"", WS_POPUP,
        0, 0, 100, 100, nullptr, nullptr, hInst, nullptr);

    // 8) 托盘 + 钩子 + 定时器
    AddTrayIcon();
    InstallHooks();
    g_lastActivity = GetTickCount64();
    SetTimer(g_hMain, TIMER_TICK, 200, nullptr);

    ShowWindow(g_hMain, SW_SHOW);
    UpdateWindow(g_hMain);

    // 9) 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) CloseHandle(hMutex);
    return 0;
}