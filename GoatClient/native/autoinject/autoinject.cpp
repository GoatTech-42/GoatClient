// Goat Client auto-injector — single self-contained EXE.
// Auto-detects a running Minecraft (64-bit) java process and injects the
// embedded GoatClientNative.dll. UI is black-minimalist.
#include <windows.h>
#include <wtypes.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "gdiplus.lib")

// ---------------------------------------------------------------------------
// Resources (matching VapeLoader's resource_ids.h)
// ---------------------------------------------------------------------------
#define IDR_ASSET_CLOSE              101
#define IDR_ASSET_LOGO               102
#define IDR_ASSET_MASK_BOTTOM        103
#define IDR_ASSET_MASK_LEFT          104
#define IDR_ASSET_MASK_RIGHT         105
#define IDR_ASSET_MASK_TOP           106
#define IDR_ASSET_MINIMIZE           107
#define IDR_ASSET_PROXIMA_REGULAR    108
#define IDR_ASSET_PROXIMA_SEMIBOLD   109
#define IDR_ASSET_ROUNDED_RECT       110
#define IDR_PAYLOAD_DLL              200

namespace {
constexpr float CanvasWidth = 824.0f;
constexpr float CanvasHeight = 484.0f;

// Vape V4 palette
constexpr Gdiplus::ARGB kBackground   = 0xFF1A191A;
constexpr Gdiplus::ARGB kPanel        = 0xFF1F1E1F;
constexpr Gdiplus::ARGB kPanelBorder  = 0xFF2B2A2B;
constexpr Gdiplus::ARGB kHoverPanel   = 0xFF262526;
constexpr Gdiplus::ARGB kGreen        = 0xFF2B7054; // #2B7054
constexpr Gdiplus::ARGB kGreenHover   = 0xFF31825F; // #31825F
constexpr Gdiplus::ARGB kGreenBright  = 0xFF058B6F;
constexpr Gdiplus::ARGB kBlue         = 0xFF2E78E3;
constexpr Gdiplus::ARGB kTextPrimary  = 0xFFDAD7DB;
constexpr Gdiplus::ARGB kTextDim      = 0xFF747174;
constexpr Gdiplus::ARGB kTextDimmer   = 0xFF69666A;
constexpr Gdiplus::ARGB kTextWhite    = 0xFFE0E5E2;
constexpr Gdiplus::ARGB kTextDisabled = 0xFF747174;
constexpr Gdiplus::ARGB kError        = 0xFFCC3333;
constexpr Gdiplus::ARGB kSuccess      = 0xFF34C77B;
}

// ---------------------------------------------------------------------------
// Shared state (UI thread + injection thread)
// ---------------------------------------------------------------------------
enum class State { Scanning, Found, Injecting, Done, Error, AlreadyInjected };

struct SharedState {
    std::mutex mutex;
    State state = State::Scanning;
    std::wstring status;
    std::wstring windowTitle;
    DWORD pid = 0;
    float progress = 0.0f; // 0..1 during injecting
    bool alreadyLoaded = false;
};

// ---------------------------------------------------------------------------
// Process discovery (port of injector.c enumerate_candidates)
// ---------------------------------------------------------------------------
static bool titleMatches(const wchar_t* title) {
    if (title == nullptr || title[0] == L'\0') return false;
    std::wstring t = title;
    std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    return t.find(L"minecraft") != std::wstring::npos ||
           t.find(L"lunar") != std::wstring::npos ||
           t.find(L"feather") != std::wstring::npos;
}

static void captureWindowTitles(std::vector<std::pair<DWORD, std::wstring>>& out) {
    struct Ctx { std::vector<std::pair<DWORD, std::wstring>>* out; };
    Ctx ctx{&out};
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (!::IsWindowVisible(hwnd) || ::GetWindowTextLengthW(hwnd) == 0) return TRUE;
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) return TRUE;
        wchar_t title[256]{};
        if (::GetWindowTextW(hwnd, title, 256) == 0) return TRUE;
        if (!titleMatches(title)) return TRUE;
        c->out->push_back({pid, title});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
}

static DWORD findMinecraftProcess(std::wstring& title) {
    // Only consider java.exe/javaw.exe with a visible "Minecraft" window.
    std::vector<std::pair<DWORD, std::wstring>> titled;
    captureWindowTitles(titled);
    if (titled.empty()) return 0;

    std::vector<DWORD> javaPids;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{sizeof(entry)};
        if (::Process32FirstW(snap, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"java.exe") == 0 ||
                    _wcsicmp(entry.szExeFile, L"javaw.exe") == 0) {
                    javaPids.push_back(entry.th32ProcessID);
                }
            } while (::Process32NextW(snap, &entry));
        }
        ::CloseHandle(snap);
    }
    if (javaPids.empty()) return 0;

    for (auto& [pid, t] : titled) {
        if (std::find(javaPids.begin(), javaPids.end(), pid) != javaPids.end()) {
            title = t;
            return pid;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Injection (port of injector.c inject_library)
// ---------------------------------------------------------------------------
static uintptr_t remoteModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t result = 0;
    MODULEENTRY32W entry{sizeof(entry)};
    if (::Module32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, name) == 0) { result = (uintptr_t)entry.modBaseAddr; break; }
        } while (::Module32NextW(snap, &entry));
    }
    ::CloseHandle(snap);
    return result;
}

static uintptr_t remoteModuleByPath(DWORD pid, const wchar_t* path) {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t result = 0;
    MODULEENTRY32W entry{sizeof(entry)};
    if (::Module32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExePath, path) == 0) { result = (uintptr_t)entry.modBaseAddr; break; }
        } while (::Module32NextW(snap, &entry));
    }
    ::CloseHandle(snap);
    return result;
}

static bool requireX64Target(HANDLE process) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto fn = (IsWow64Process2Fn)::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (fn) {
        USHORT pm = IMAGE_FILE_MACHINE_UNKNOWN, nm = IMAGE_FILE_MACHINE_UNKNOWN;
        if (!fn(process, &pm, &nm)) return false;
        return pm == IMAGE_FILE_MACHINE_UNKNOWN && nm == IMAGE_FILE_MACHINE_AMD64;
    }
    BOOL wow64 = FALSE;
    if (!::IsWow64Process(process, &wow64)) return false;
    return !wow64 && sizeof(void*) == 8;
}

static std::wstring lastErrorText(const wchar_t* op) {
    DWORD e = ::GetLastError();
    wchar_t* msg = nullptr;
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0,
                     (wchar_t*)&msg, 0, nullptr);
    std::wstring out = op;
    out += L" failed (";
    out += std::to_wstring(e);
    out += L")";
    if (msg) { out += L": "; out += msg; ::LocalFree(msg); }
    return out;
}

// Returns 0 = success, 1 = already loaded, -1 = error (error text in `err`).
static int injectLibrary(DWORD pid, const std::wstring& dllPath, std::wstring& err) {
    if (remoteModuleByPath(pid, dllPath.c_str()) != 0) return 1;

    HANDLE process = ::OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                   FALSE, pid);
    if (!process) { err = lastErrorText(L"OpenProcess"); return -1; }

    int result = -1;
    HANDLE thread = nullptr;
    LPVOID remotePath = nullptr;

    if (!requireX64Target(process)) {
        err = L"Target Minecraft is not 64-bit; injection refused. Use a 64-bit JVM.";
        goto cleanup;
    }

    {
        SIZE_T pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
        SIZE_T written = 0;
        remotePath = ::VirtualAllocEx(process, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath || !::WriteProcessMemory(process, remotePath, dllPath.c_str(), pathBytes, &written) ||
            written != pathBytes) {
            err = lastErrorText(L"WriteProcessMemory");
            goto cleanup;
        }
    }

    {
        HMODULE localKernel = ::GetModuleHandleW(L"kernel32.dll");
        FARPROC localLoad = ::GetProcAddress(localKernel, "LoadLibraryW");
        uintptr_t remoteKernel = remoteModuleBase(pid, L"kernel32.dll");
        if (!localKernel || !localLoad || !remoteKernel) {
            err = L"Could not resolve remote kernel32!LoadLibraryW";
            goto cleanup;
        }
        auto remoteLoad = (LPTHREAD_START_ROUTINE)((uintptr_t)localLoad - (uintptr_t)localKernel + remoteKernel);
        thread = ::CreateRemoteThread(process, nullptr, 0, remoteLoad, remotePath, 0, nullptr);
        if (!thread) { err = lastErrorText(L"CreateRemoteThread"); goto cleanup; }
    }

    if (::WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0) {
        err = L"Remote LoadLibraryW did not finish within 30 seconds";
        goto cleanup;
    }
    {
        // LoadLibraryW returns the module handle on success (NULL on failure).
        // Read it directly instead of polling the (race-prone) module snapshot.
        DWORD exitCode = 0;
        if (::GetExitCodeThread(thread, &exitCode) && exitCode != 0) {
            result = 0;
        } else {
            // Fall back to a brief module snapshot check for robustness.
            for (int attempt = 0; attempt < 40; ++attempt) {
                if (remoteModuleByPath(pid, dllPath.c_str()) != 0) { result = 0; break; }
                ::Sleep(50);
            }
            if (result != 0) {
                err = L"DLL failed to load in target (see goatclient-native.log)";
            }
        }
    }

cleanup:
    if (thread) ::CloseHandle(thread);
    if (remotePath && process) ::VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    if (process) ::CloseHandle(process);
    return result;
}

// ---------------------------------------------------------------------------
// Embedding helpers
// ---------------------------------------------------------------------------
static std::wstring executableDir() {
    wchar_t path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    auto sep = result.find_last_of(L"\\/");
    return sep == std::wstring::npos ? L"." : result.substr(0, sep);
}

// Extract embedded DLL to a file beside the EXE; returns path (empty on failure).
static std::wstring materializeDll(HINSTANCE instance) {
    HRSRC res = ::FindResourceW(instance, MAKEINTRESOURCEW(IDR_PAYLOAD_DLL), RT_RCDATA);
    if (!res) return {};
    HGLOBAL loaded = ::LoadResource(instance, res);
    if (!loaded) return {};
    const void* data = ::LockResource(loaded);
    DWORD size = ::SizeofResource(instance, res);
    if (!data || size == 0) return {};

    std::wstring path = executableDir() + L"\\GoatClientNative.dll";
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    DWORD written = 0;
    BOOL ok = ::WriteFile(file, data, size, &written, nullptr) && written == size;
    ::CloseHandle(file);
    return ok ? path : std::wstring{};
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
struct ResourceView { const void* data{}; DWORD size{}; };

static ResourceView resourceView(HINSTANCE instance, int id) {
    HRSRC r = ::FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL l = r ? ::LoadResource(instance, r) : nullptr;
    return { l ? ::LockResource(l) : nullptr, r ? ::SizeofResource(instance, r) : 0 };
}

static void addRoundedPath(Gdiplus::GraphicsPath& path, float x, float y,
                           float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

class InjectorApp {
public:
    InjectorApp(HINSTANCE instance) : instance_(instance) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr);
        ResourceView regular = resourceView(instance_, IDR_ASSET_PROXIMA_REGULAR);
        ResourceView semibold = resourceView(instance_, IDR_ASSET_PROXIMA_SEMIBOLD);
        if (regular.data) fonts_.AddMemoryFont(regular.data, (INT)regular.size);
        if (semibold.data) fonts_.AddMemoryFont(semibold.data, (INT)semibold.size);
        logo_ = loadImage(IDR_ASSET_LOGO);
        maskTop_ = loadImage(IDR_ASSET_MASK_TOP);
        maskBottom_ = loadImage(IDR_ASSET_MASK_BOTTOM);
        maskLeft_ = loadImage(IDR_ASSET_MASK_LEFT);
        maskRight_ = loadImage(IDR_ASSET_MASK_RIGHT);
        roundedRect_ = loadImage(IDR_ASSET_ROUNDED_RECT);
    }
    ~InjectorApp() {
        logo_.reset(); maskTop_.reset(); maskBottom_.reset();
        maskLeft_.reset(); maskRight_.reset(); roundedRect_.reset();
        for (IStream* s : streams_) s->Release();
        streams_.clear();
        if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
        if (injectThread_.joinable()) injectThread_.join();
    }

    int run(int showCommand) {
        WNDCLASSEXW cls{sizeof(cls)};
        cls.style = CS_HREDRAW | CS_VREDRAW;
        cls.lpfnWndProc = windowProc;
        cls.hInstance = instance_;
        cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = ::LoadIconW(instance_, MAKEINTRESOURCEW(100));
        cls.hIconSm = cls.hIcon;
        cls.lpszClassName = L"GoatClientInject";
        ::RegisterClassExW(&cls);

        UINT dpi = ::GetDpiForSystem();
        int cw = MulDiv((int)CanvasWidth, dpi, 96);
        int ch = MulDiv((int)CanvasHeight, dpi, 96);
        RECT bounds{0, 0, cw, ch};
        ::AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                   FALSE, 0, dpi);
        window_ = ::CreateWindowExW(0, cls.lpszClassName, L"Goat Client",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
            bounds.bottom - bounds.top, nullptr, nullptr, instance_, this);
        if (!window_) return 1;
        lastFrame_ = std::chrono::steady_clock::now();
        lastScan_ = std::chrono::steady_clock::now();
        ::SetTimer(window_, 1, 16, nullptr);
        ::ShowWindow(window_, showCommand);
        ::UpdateWindow(window_);

        MSG msg{};
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        return (int)msg.wParam;
    }

private:
    std::unique_ptr<Gdiplus::Image> loadImage(int id) {
        ResourceView rv = resourceView(instance_, id);
        if (!rv.data) return {};
        IStream* stream = ::SHCreateMemStream((const BYTE*)rv.data, rv.size);
        if (!stream) return {};
        auto img = std::make_unique<Gdiplus::Image>(stream);
        if (img->GetLastStatus() != Gdiplus::Ok) { img.reset(); stream->Release(); return {}; }
        streams_.push_back(stream);
        return img;
    }

    // -- injection orchestration (background thread) --
    void beginInjection(DWORD pid, const std::wstring& title) {
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.state = State::Injecting;
            state_.pid = pid;
            state_.windowTitle = title;
            state_.progress = 0.0f;
            state_.status = L"Preparing injection...";
        }
        ::InvalidateRect(window_, nullptr, FALSE);
        if (injectThread_.joinable()) injectThread_.join();
        injectThread_ = std::thread([this, pid] { injectionWorker(pid); });
    }

    void injectionWorker(DWORD pid) {
        auto setProgress = [&](float p, const wchar_t* msg) {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.progress = p;
            state_.status = msg;
        };
        auto setDone = [&](const wchar_t* msg) {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.state = State::Done;
            state_.progress = 1.0f;
            state_.status = msg;
        };
        auto setError = [&](const wchar_t* msg) {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.state = State::Error;
            state_.status = msg;
        };
        auto setAlready = [&](const wchar_t* msg) {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.state = State::AlreadyInjected;
            state_.status = msg;
        };

        setProgress(0.10f, L"Extracting native module...");
        std::wstring dll = materializeDll(instance_);
        if (dll.empty()) { setError(L"Failed to extract GoatClientNative.dll next to the EXE"); goto notify; }

        setProgress(0.35f, L"Injecting into Minecraft...");
        {
            std::wstring err;
            int r = injectLibrary(pid, dll, err);
            if (r == 1) { setAlready(L"Goat Client is already injected in this Minecraft"); goto notify; }
            if (r != 0) { setError((L"Injection failed: " + err).c_str()); goto notify; }
        }
        setProgress(0.85f, L"Waiting for Goat Client to load...");
        // Give the JVM a few seconds to bootstrap; check for the native log marker is best-effort.
        for (int i = 0; i < 60; ++i) {
            ::Sleep(500);
            if (remoteModuleByPath(pid, dll.c_str()) != 0) {
                // still mapped → load held (natives pinned). Continue to let Java init.
            }
            // Transition progress smoothly up to 95%.
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.progress = std::min(0.95f, 0.85f + 0.01f * i);
        }
        setDone(L"Goat Client loaded — press RIGHT SHIFT in game to open the GUI");

    notify:
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    // -- window proc --
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        InjectorApp* self = reinterpret_cast<InjectorApp*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<InjectorApp*>(cs->lpCreateParams);
            self->window_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->handleMessage(msg, wp, lp) : ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    bool hit(float lx, float ly, float x, float y, float w, float h) const {
        return lx >= x && lx <= x + w && ly >= y && ly <= y + h;
    }
    bool pointerIn(float x, float y, float w, float h) const {
        return hit(mouseX_, mouseY_, x, y, w, h);
    }

    void startScan() {
        // Reset to scanning unless already injecting/done.
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            if (state_.state == State::Injecting || state_.state == State::Done) return;
            state_.state = State::Scanning;
            state_.status.clear();
            state_.pid = 0;
            state_.windowTitle.clear();
        }
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    void rescanOnce() {
        State st;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            st = state_.state;
        }
        if (st == State::Injecting || st == State::Done) return;
        std::wstring title;
        DWORD pid = findMinecraftProcess(title);
        if (pid != 0) {
            beginInjection(pid, title);
        }
    }

    void tickScan() {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastScan_).count() < 1.0) return;
        lastScan_ = now;
        State st;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            st = state_.state;
        }
        if (st == State::Scanning || st == State::Found) {
            std::wstring title;
            DWORD pid = findMinecraftProcess(title);
            if (pid != 0) {
                // Found → hold a moment then inject
                bool justFound = false;
                {
                    std::lock_guard<std::mutex> lock(state_.mutex);
                    if (state_.state == State::Scanning) {
                        state_.state = State::Found;
                        state_.windowTitle = title;
                        state_.pid = pid;
                        justFound = true;
                    }
                }
                if (justFound) foundAt_ = now;
                if (std::chrono::duration<double>(now - foundAt_).count() >= 0.6) {
                    beginInjection(pid, title);
                }
                ::InvalidateRect(window_, nullptr, FALSE);
            }
        }
    }

    void updateFrame() {
        auto now = std::chrono::steady_clock::now();
        double delta = std::clamp(std::chrono::duration<double>(now - lastFrame_).count(), 0.0, 0.1);
        lastFrame_ = now;
        tickScan();
        // spinner
        State st;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            st = state_.state;
        }
        if (st == State::Scanning) {
            spinnerAccum_ += delta;
            while (spinnerAccum_ >= 0.02) {
                spinnerAccum_ -= 0.02;
                for (int i = 0; i < 4; ++i) {
                    spinnerA_[i] += i == spinnerIdx_ ? 0.15f : -0.075f;
                    spinnerA_[i] = std::clamp(spinnerA_[i], 0.0f, 1.0f);
                }
                if (spinnerA_[spinnerIdx_] >= 1.0f) {
                    spinnerIdx_ = (spinnerIdx_ + 1) % 4;
                    spinnerA_[spinnerIdx_] = 0.0f;
                }
            }
        }
        // transition masks on state change
        State cur = st;
        if (cur != lastState_) { lastState_ = cur; stateChanged_ = now; }
        ::InvalidateRect(window_, nullptr, FALSE);
    }

    LRESULT handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_PAINT: paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if (wp == 1) updateFrame();
            return 0;
        case WM_MOUSEMOVE:
            mouseX_ = (float)GET_X_LPARAM(lp) / scaleX_;
            mouseY_ = (float)GET_Y_LPARAM(lp) / scaleY_;
            if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, window_, 0};
                ::TrackMouseEvent(&t);
                tracking_ = true;
            }
            ::InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_MOUSELEAVE:
            mouseX_ = mouseY_ = -1.0f;
            tracking_ = false;
            ::InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            float x = (float)GET_X_LPARAM(lp) / scaleX_;
            float y = (float)GET_Y_LPARAM(lp) / scaleY_;
            State st;
            DWORD pid;
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                st = state_.state; pid = state_.pid;
            }
            if (st == State::Error && hit(x, y, 356, 330, 112, 36)) { startScan(); }
            else if (st == State::Done && hit(x, y, 356, 348, 112, 36)) { ::DestroyWindow(window_); }
            else if (st == State::AlreadyInjected && hit(x, y, 356, 348, 112, 36)) { ::DestroyWindow(window_); }
            else if (st == State::Scanning || st == State::Found) {
                if (hit(x, y, 356, 330, 112, 36)) rescanOnce();
            }
            ::InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* r = reinterpret_cast<RECT*>(lp);
            ::SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left,
                           r->bottom - r->top, SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_DESTROY:
            ::KillTimer(window_, 1);
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(window_, msg, wp, lp);
        }
    }

    // -- drawing --
    void paint() {
        PAINTSTRUCT ps{};
        HDC target = ::BeginPaint(window_, &ps);
        RECT rc{};
        ::GetClientRect(window_, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        HDC bufDc = ::CreateCompatibleDC(target);
        HBITMAP buf = ::CreateCompatibleBitmap(target, w, h);
        auto prev = ::SelectObject(bufDc, buf);
        Gdiplus::Graphics g(bufDc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        scaleX_ = (float)w / CanvasWidth;
        scaleY_ = (float)h / CanvasHeight;
        g.ScaleTransform(scaleX_, scaleY_);

        Gdiplus::SolidBrush bg(kBackground);
        g.FillRectangle(&bg, 0.0f, 0.0f, CanvasWidth, CanvasHeight);

        State st;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            st = state_.state;
        }
        switch (st) {
        case State::Scanning: drawScanning(g); break;
        case State::Found: drawFound(g); break;
        case State::Injecting: drawInjecting(g); break;
        case State::Done: drawDone(g); break;
        case State::Error: drawError(g); break;
        case State::AlreadyInjected: drawAlready(g); break;
        }
        drawTransitionMasks(g);

        ::BitBlt(target, 0, 0, w, h, bufDc, 0, 0, SRCCOPY);
        ::SelectObject(bufDc, prev);
        ::DeleteObject(buf);
        ::DeleteDC(bufDc);
        ::EndPaint(window_, &ps);
    }

    void drawTransitionMasks(Gdiplus::Graphics& g) {
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - stateChanged_).count();
        if (elapsed >= 0.45 || elapsed < 0.0) return;
        float t = (float)(elapsed / 0.45);
        float opacity = 1.0f - t;
        Gdiplus::ColorMatrix m{{ {1,0,0,0,0},{0,1,0,0,0},{0,0,1,0,0},{0,0,0,opacity,0},{0,0,0,0,1} }};
        Gdiplus::ImageAttributes attr;
        attr.SetColorMatrix(&m);
        if (maskLeft_ && maskLeft_->GetLastStatus() == Gdiplus::Ok) {
            float x = -154.0f * t;
            g.DrawImage(maskLeft_.get(), Gdiplus::RectF(x, 149, 154, 185),
                        0, 0, 154, 185, Gdiplus::UnitPixel, &attr);
        }
        if (maskRight_ && maskRight_->GetLastStatus() == Gdiplus::Ok) {
            float x = 713.0f + 111.0f * t;
            g.DrawImage(maskRight_.get(), Gdiplus::RectF(x, 161, 111, 162),
                        0, 0, 111, 162, Gdiplus::UnitPixel, &attr);
        }
    }

    void drawLogo(Gdiplus::Graphics& g, float y) {
        if (logo_ && logo_->GetLastStatus() == Gdiplus::Ok) {
            const float w = 240.0f;
            const float h = w * 47.0f / 333.0f; // keep wordmark aspect (333x47)
            g.DrawImage(logo_.get(), (CanvasWidth - w) / 2.0f, y, w, h);
        }
    }

    void drawRoundedRect(Gdiplus::Graphics& g, float x, float y, float w, float h,
                         float r, Gdiplus::Color fill, Gdiplus::Color border = Gdiplus::Color(0,0,0,0)) {
        Gdiplus::GraphicsPath path;
        addRoundedPath(path, x, y, w, h, r);
        Gdiplus::SolidBrush b(fill);
        g.FillPath(&b, &path);
        if (border.GetA()) { Gdiplus::Pen p(border, 1.0f); g.DrawPath(&p, &path); }
    }

    void drawText(Gdiplus::Graphics& g, const std::wstring& text, float x, float y,
                  float w, float h, float size, Gdiplus::Color color, bool semibold = false,
                  Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear) {
        Gdiplus::FontFamily loaded[4];
        int found = 0;
        fonts_.GetFamilies(4, loaded, &found);
        const Gdiplus::FontFamily* family = Gdiplus::FontFamily::GenericSansSerif();
        for (int i = 0; i < found; ++i) {
            wchar_t name[LF_FACESIZE]{};
            loaded[i].GetFamilyName(name);
            std::wstring n(name);
            bool isSemi = n.find(L"Lt") != std::wstring::npos || n.find(L"Semi") != std::wstring::npos;
            if (isSemi == semibold) { family = &loaded[i]; break; }
        }
        Gdiplus::Font font(family, size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(color);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(align);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        Gdiplus::RectF box(x, y, w, h);
        g.DrawString(text.c_str(), (INT)text.size(), &font, box, &fmt, &brush);
    }

    void drawSpinner(Gdiplus::Graphics& g, float cx, float cy) {
        constexpr float xs[4]{0, 18, 18, 0};
        constexpr float ys[4]{0, 0, 18, 18};
        for (int i = 0; i < 4; ++i) {
            BYTE shade = (BYTE)(200.0f * (1.0f - spinnerA_[i]));
            Gdiplus::SolidBrush sq(Gdiplus::Color(255, shade, shade, shade));
            g.FillRectangle(&sq, cx + xs[i], cy + ys[i], 12.0f, 12.0f);
        }
    }

    void drawScanning(Gdiplus::Graphics& g) {
        drawLogo(g, 150.0f);
        drawText(g, L"Goat Client", 200, 196, 424, 30, 16, kTextDim, true, Gdiplus::StringAlignmentCenter);
        drawText(g, L"Looking for Minecraft...", 180, 240, 464, 30, 14,
                 kTextPrimary, true, Gdiplus::StringAlignmentCenter);
        drawSpinner(g, 394.0f, 288.0f);
        drawText(g, L"Launch Minecraft 1.21.11 (64-bit) and keep it open", 150, 340, 524, 24, 12,
                 kTextDim, false, Gdiplus::StringAlignmentCenter);
        drawText(g, L"Goat Client will inject automatically", 150, 364, 524, 22, 12,
                 kTextDimmer, false, Gdiplus::StringAlignmentCenter);
        // manual rescan button
        bool hov = pointerIn(356, 330, 112, 36);
        drawRoundedRect(g, 356, 412, 112, 36, 3, hov ? kHoverPanel : kPanel, kPanelBorder);
        drawText(g, L"Rescan", 356, 412, 112, 36, 13, kTextWhite, true, Gdiplus::StringAlignmentCenter);
    }

    void drawFound(Gdiplus::Graphics& g) {
        drawLogo(g, 150.0f);
        drawText(g, L"Minecraft detected", 200, 214, 424, 30, 16, kTextPrimary, true,
                 Gdiplus::StringAlignmentCenter);
        std::wstring pidTxt;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            pidTxt = L"PID " + std::to_wstring(state_.pid) + L"  ·  " +
                     (state_.windowTitle.empty() ? L"Minecraft" : state_.windowTitle);
        }
        drawText(g, pidTxt, 150, 252, 524, 24, 12, kTextDim, false, Gdiplus::StringAlignmentCenter);
        drawText(g, L"Injecting...", 200, 296, 424, 24, 13, kGreenBright, true,
                 Gdiplus::StringAlignmentCenter);
        drawSpinner(g, 394.0f, 330.0f);
    }

    void drawInjecting(Gdiplus::Graphics& g) {
        float progress;
        std::wstring status;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            progress = state_.progress;
            status = state_.status;
        }
        drawLogo(g, 150.0f);
        drawText(g, L"Injecting Goat Client", 200, 196, 424, 30, 16, kTextPrimary, true,
                 Gdiplus::StringAlignmentCenter);
        float tx = 292.0f, ty = 265.0f, tw = 240.0f;
        drawRoundedRect(g, tx, ty, tw, 6, 3, kPanel);
        drawRoundedRect(g, tx, ty, std::max(6.0f, tw * std::clamp(progress, 0.0f, 1.0f)), 6, 3,
                        kGreenBright);
        drawText(g, status, 180, 286, 464, 24, 12, kTextDim, false, Gdiplus::StringAlignmentCenter);
    }

    void drawDone(Gdiplus::Graphics& g) {
        drawLogo(g, 150.0f);
        Gdiplus::SolidBrush check(kSuccess);
        // simple check circle
        drawRoundedRect(g, 391, 200, 42, 42, 21, kSuccess);
        Gdiplus::Pen white(Gdiplus::Color(255, 255, 255, 255), 3.0f);
        g.DrawLine(&white, 403.0f, 220.0f, 410.0f, 228.0f);
        g.DrawLine(&white, 410.0f, 228.0f, 424.0f, 211.0f);
        drawText(g, L"Goat Client has finished loading", 220, 258, 384, 30, 15, kTextPrimary, true,
                 Gdiplus::StringAlignmentCenter);
        drawText(g, L"Press RIGHT SHIFT in game to open the GUI", 140, 292, 544, 24, 12,
                 kTextDim, false, Gdiplus::StringAlignmentCenter);
        drawRoundedRect(g, 356, 348, 112, 36, 3,
                        pointerIn(356, 348, 112, 36) ? kHoverPanel : kPanel, kPanelBorder);
        drawText(g, L"Close", 356, 348, 112, 36, 13, kTextWhite, true, Gdiplus::StringAlignmentCenter);
    }

    void drawAlready(Gdiplus::Graphics& g) {
        drawLogo(g, 150.0f);
        drawText(g, L"Goat Client is already running", 200, 240, 424, 30, 16, kTextPrimary, true,
                 Gdiplus::StringAlignmentCenter);
        drawText(g, L"Press RIGHT SHIFT in game to open the GUI", 140, 284, 544, 24, 12,
                 kTextDim, false, Gdiplus::StringAlignmentCenter);
        drawRoundedRect(g, 356, 348, 112, 36, 3,
                        pointerIn(356, 348, 112, 36) ? kHoverPanel : kPanel, kPanelBorder);
        drawText(g, L"Close", 356, 348, 112, 36, 13, kTextWhite, true, Gdiplus::StringAlignmentCenter);
    }

    void drawError(Gdiplus::Graphics& g) {
        drawLogo(g, 150.0f);
        std::wstring status;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            status = state_.status;
        }
        drawText(g, L"Injection failed", 200, 220, 424, 30, 16, kError, true,
                 Gdiplus::StringAlignmentCenter);
        drawText(g, status.empty() ? L"Unknown error" : status, 150, 262, 524, 60, 12,
                 kTextDim, false, Gdiplus::StringAlignmentCenter);
        drawRoundedRect(g, 356, 330, 112, 36, 3,
                        pointerIn(356, 330, 112, 36) ? kHoverPanel : kPanel, kPanelBorder);
        drawText(g, L"Retry", 356, 330, 112, 36, 13, kTextWhite, true, Gdiplus::StringAlignmentCenter);
    }

    HINSTANCE instance_{};
    HWND window_{};
    ULONG_PTR gdiplusToken_{};
    Gdiplus::PrivateFontCollection fonts_;
    std::unique_ptr<Gdiplus::Image> logo_, maskTop_, maskBottom_, maskLeft_, maskRight_, roundedRect_;
    std::vector<IStream*> streams_;
    SharedState state_;
    std::thread injectThread_;
    std::chrono::steady_clock::time_point lastFrame_{}, lastScan_{}, foundAt_{}, stateChanged_{};
    State lastState_{State::Scanning};
    float scaleX_{1.0f}, scaleY_{1.0f}, mouseX_{-1.0f}, mouseY_{-1.0f};
    bool tracking_{false};
    double spinnerAccum_{};
    int spinnerIdx_{};
    float spinnerA_[4]{1.0f, 0.55f, 0.1f, 0.0f};
};

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int showCommand) {
    InjectorApp app(instance);
    return app.run(showCommand);
}
