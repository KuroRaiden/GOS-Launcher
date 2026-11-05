#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iterator>
#include <string>
#include <vector>
#include "launcher_paths.h"

namespace {
namespace fs=std::filesystem;
constexpr int playId=101,buildId=102,folderId=103,logsId=104,browseId=105;
constexpr UINT_PTR processTimer=1;
struct Launcher {
    HINSTANCE instance{};
    HWND window{},build{},play{},description{},original{},status{},browse{};
    HFONT font{},headingFont{};
    HICON largeIcon{},smallIcon{};
    HANDLE process{};
    DWORD processId{};
    int dpi=96;
    fs::path project,ini,originalAssembly;
    launcherpaths::Layout layout;
} app;
int px(int value) { return MulDiv(value,app.dpi,96); }
bool fileExists(const fs::path& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
bool directoryExists(const fs::path& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
}
fs::path executablePath() {
    std::wstring path(32768,L'\0');
    const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!length || length==path.size()) return {};
    path.resize(length); return fs::path(path);
}
bool previewSelected() { return app.layout.portable || SendMessageW(app.build,CB_GETCURSEL,0,0)==1; }
fs::path gameFolder() { return launcherpaths::gameFolder(app.layout,previewSelected()); }
std::wstring quoteArgument(const std::wstring& argument) {
    std::wstring result=L"\"";
    size_t slashes=0;
    for(const auto ch:argument) {
        if(ch==L'\\') { ++slashes; continue; }
        if(ch==L'\"') {
            result.append(slashes*2+1,L'\\'); result+=ch;
        } else {
            result.append(slashes,L'\\'); result+=ch;
        }
        slashes=0;
    }
    result.append(slashes*2,L'\\'); result+=L'\"';
    return result;
}
std::wstring windowsError(DWORD code) {
    wchar_t* buffer{};
    const auto length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,code,0,reinterpret_cast<wchar_t*>(&buffer),0,nullptr);
    std::wstring message=length?std::wstring(buffer,length):L"Windows error "+std::to_wstring(code);
    if(buffer) LocalFree(buffer);
    while(!message.empty() && (message.back()==L'\r' || message.back()==L'\n')) message.pop_back();
    return message;
}
void setStatus(const std::wstring& text) { SetWindowTextW(app.status,text.c_str()); }
void updateAvailability() {
    const bool running=app.process!=nullptr;
    const bool gameReady=fileExists(gameFolder()/L"Castlevania.exe");
    const bool originalReady=fileExists(app.originalAssembly);
    EnableWindow(app.play,!running && gameReady && originalReady);
    EnableWindow(app.build,!running && !app.layout.portable);
    EnableWindow(app.browse,!running);
    SetWindowTextW(app.description,app.layout.portable?L"Your saves and settings stay inside this portable folder.":
        previewSelected()?L"Launch the effects preview with a separate copy of your saves.":L"Launch the current game.");
    SetWindowTextW(app.original,app.originalAssembly.c_str());
    if(running) setStatus(L"Game is running.");
    else if(!gameReady) setStatus(L"This build is missing. Choose another build or check the game folder.");
    else if(!originalReady) setStatus(L"Choose GameAssembly.dylib from the original macOS app to play.");
    else setStatus(L"Ready to play.");
}
struct EnvironmentDeleter {
    void operator()(wchar_t* value) const { if(value) FreeEnvironmentStringsW(value); }
};
std::vector<wchar_t> gameEnvironment(bool preview) {
    std::unique_ptr<wchar_t,EnvironmentDeleter> current(GetEnvironmentStringsW());
    if(!current) return {};
    constexpr wchar_t originalKey[]=L"CASTLEVANIA_ORIGINAL_GAMEASSEMBLY=";
    constexpr wchar_t effectsKey[]=L"CASTLEVANIA_SPFX_OPENGL=";
    std::vector<std::wstring> entries;
    for(auto entry=current.get();*entry;entry+=std::wcslen(entry)+1) {
        if(_wcsnicmp(entry,originalKey,std::size(originalKey)-1)==0 ||
           _wcsnicmp(entry,effectsKey,std::size(effectsKey)-1)==0) continue;
        entries.emplace_back(entry);
    }
    entries.emplace_back(std::wstring(originalKey)+app.originalAssembly.wstring());
    if(preview) entries.emplace_back(std::wstring(effectsKey)+L"1");
    std::sort(entries.begin(),entries.end(),[](const auto& left,const auto& right) {
        return _wcsicmp(left.c_str(),right.c_str())<0;
    });
    std::vector<wchar_t> block;
    for(const auto& entry:entries) {
        block.insert(block.end(),entry.begin(),entry.end()); block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
void launchGame() {
    if(app.process) return;
    const auto folder=gameFolder();
    const auto executable=folder/L"Castlevania.exe";
    if(!fileExists(executable) || !fileExists(app.originalAssembly)) { updateAvailability(); return; }
    auto environment=gameEnvironment(previewSelected());
    if(environment.empty()) { setStatus(L"Could not prepare the game environment: "+windowsError(GetLastError())); return; }
    const auto log=folder/(L"player-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log");
    auto command=quoteArgument(executable.wstring())+L" -logFile "+quoteArgument(log.wstring());
    if(previewSelected()) command+=L" -force-glcore -screen-fullscreen 0 -screen-width 1280 -screen-height 720";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,
                       environment.data(),folder.c_str(),&startup,&process)) {
        setStatus(L"Could not start the game: "+windowsError(GetLastError())); return;
    }
    {std::ofstream latest(folder/L"player-latest.txt",std::ios::trunc);latest << log.filename().string() << "\n";}
    CloseHandle(process.hThread);
    app.process=process.hProcess; app.processId=process.dwProcessId;
    SetTimer(app.window,processTimer,500,nullptr);
    updateAvailability();
}
void pollGame() {
    if(!app.process || WaitForSingleObject(app.process,0)!=WAIT_OBJECT_0) return;
    DWORD code{}; const bool hasCode=GetExitCodeProcess(app.process,&code)!=FALSE;
    CloseHandle(app.process); app.process=nullptr; app.processId=0;
    KillTimer(app.window,processTimer);
    updateAvailability();
    if(!hasCode) setStatus(L"Game closed. Open Logs for details.");
    else if(code==0) setStatus(L"Game closed.");
    else setStatus(L"Game closed with exit code "+std::to_wstring(code)+L". Open Logs for details.");
}
void openFolder() {
    const auto selected=gameFolder();
    const auto folder=directoryExists(selected)?selected:app.project;
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(app.window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32) setStatus(L"Could not open the game folder.");
}
void openLogs() {
    const auto folder=gameFolder();
    auto log=folder/L"player.log";
    {std::ifstream latest(folder/L"player-latest.txt");std::string filename;std::getline(latest,filename);
     if(!filename.empty() && filename.find_first_of("/\\:")==std::string::npos)log=folder/fs::path(filename);}
    if(!fileExists(log)) { setStatus(L"No game log yet. Start this build to create one."); return; }
    const auto argument=quoteArgument(log.wstring());
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(app.window,L"open",L"notepad.exe",argument.c_str(),folder.c_str(),SW_SHOWNORMAL));
    if(result<=32) setStatus(L"Could not open the log. Find player.log in the game folder.");
}
void browseOriginal() {
    std::wstring selected(32768,L'\0');
    const auto existing=app.originalAssembly.wstring();
    if(existing.size()<selected.size()) std::copy(existing.begin(),existing.end(),selected.begin());
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=app.window;
    dialog.lpstrFilter=L"GameAssembly.dylib\0GameAssembly.dylib\0macOS libraries (*.dylib)\0*.dylib\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile=selected.data(); dialog.nMaxFile=static_cast<DWORD>(selected.size());
    dialog.lpstrTitle=L"Choose GameAssembly.dylib from the original macOS app";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if(!GetOpenFileNameW(&dialog)) return;
    selected.resize(std::wcslen(selected.c_str()));
    const fs::path path(selected);
    if(_wcsicmp(path.filename().c_str(),L"GameAssembly.dylib")!=0) {
        setStatus(L"Choose the original GameAssembly.dylib file."); return;
    }
    app.originalAssembly=path;
    const auto stored=launcherpaths::storedOriginal(app.layout,path);
    const bool saved=WritePrivateProfileStringW(L"Game",L"originalGameAssembly",stored.c_str(),app.ini.c_str())!=FALSE;
    updateAvailability();
    if(!saved) setStatus(L"Game file selected for this session. The launcher could not save the location.");
}
HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id=0) {
    const auto window=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),
        app.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app.instance,nullptr);
    SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE);
    return window;
}
void createControls() {
    app.font=CreateFontW(-MulDiv(10,app.dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.headingFont=CreateFontW(-MulDiv(22,app.dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    const auto heading=control(L"STATIC",L"Grimoire of Souls",0,20,18,568,37);
    SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(app.headingFont),TRUE);
    control(L"STATIC",app.layout.portable?L"Portable testing build":L"Windows launcher",0,22,59,550,20);
    control(L"STATIC",L"Build",0,20,91,100,20);
    app.build=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,20,114,410,150,buildId);
    if(!app.layout.portable) SendMessageW(app.build,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Current game"));
    SendMessageW(app.build,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"OpenGL effects preview"));
    SendMessageW(app.build,CB_SETCURSEL,0,0);
    app.play=control(L"BUTTON",L"Play",BS_DEFPUSHBUTTON|WS_TABSTOP,448,107,140,40,playId);
    app.description=control(L"STATIC",L"",0,20,155,568,35);
    control(L"STATIC",L"Original macOS game file",0,20,198,568,20);
    app.original=control(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP,20,223,460,26);
    app.browse=control(L"BUTTON",L"Browse...",WS_TABSTOP,490,222,98,28,browseId);
    control(L"BUTTON",L"Open game folder",WS_TABSTOP,20,270,170,30,folderId);
    control(L"BUTTON",L"Logs",WS_TABSTOP,200,270,90,30,logsId);
    app.status=control(L"STATIC",L"",0,20,316,568,36);
    const auto iconSource=gameFolder()/L"Castlevania.exe";
    if(ExtractIconExW(iconSource.c_str(),0,&app.largeIcon,&app.smallIcon,1)) {
        SendMessageW(app.window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(app.largeIcon));
        SendMessageW(app.window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(app.smallIcon));
    }
    updateAvailability();
}
LRESULT CALLBACK windowProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE:
        app.window=window; createControls(); return 0;
    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK:
        case playId: launchGame(); return 0;
        case IDCANCEL: SendMessageW(window,WM_CLOSE,0,0); return 0;
        case buildId: if(HIWORD(wParam)==CBN_SELCHANGE) updateAvailability(); return 0;
        case folderId: openFolder(); return 0;
        case logsId: openLogs(); return 0;
        case browseId: browseOriginal(); return 0;
        }
        break;
    case WM_TIMER: if(wParam==processTimer) pollGame(); return 0;
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wParam),GetSysColor(COLOR_WINDOW));
        SetTextColor(reinterpret_cast<HDC>(wParam),GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY:
        KillTimer(window,processTimer);
        if(app.process) CloseHandle(app.process);
        if(app.font) DeleteObject(app.font);
        if(app.headingFont) DeleteObject(app.headingFont);
        if(app.largeIcon) DestroyIcon(app.largeIcon);
        if(app.smallIcon) DestroyIcon(app.smallIcon);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,wchar_t*,int show) {
    SetProcessDPIAware();
    app.instance=instance;
    const auto screen=GetDC(nullptr);
    if(screen) { app.dpi=GetDeviceCaps(screen,LOGPIXELSY); ReleaseDC(nullptr,screen); }
    const auto executable=executablePath();
    app.layout=launcherpaths::locate(executable,executable.parent_path());
    app.project=app.layout.root;
    app.ini=executable.parent_path()/L"CastlevaniaLauncher.ini";
    std::wstring original(32768,L'\0');
    const auto originalLength=GetPrivateProfileStringW(L"Game",L"originalGameAssembly",L"",
        original.data(),static_cast<DWORD>(original.size()),app.ini.c_str());
    original.resize(originalLength);
    app.originalAssembly=launcherpaths::resolveOriginal(app.layout,original,{});
    WNDCLASSEXW windowClass{}; windowClass.cbSize=sizeof(windowClass); windowClass.lpfnWndProc=windowProcedure;
    windowClass.hInstance=instance; windowClass.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground=GetSysColorBrush(COLOR_WINDOW); windowClass.lpszClassName=L"CastlevaniaGameLauncher";
    if(!RegisterClassExW(&windowClass)) return 1;
    constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT bounds{0,0,px(610),px(365)}; AdjustWindowRectEx(&bounds,style,FALSE,0);
    const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    const auto window=CreateWindowExW(0,windowClass.lpszClassName,L"Castlevania: Grimoire of Souls",style,
        (GetSystemMetrics(SM_CXSCREEN)-width)/2,(GetSystemMetrics(SM_CYSCREEN)-height)/2,width,height,
        nullptr,nullptr,instance,nullptr);
    if(!window) return 1;
    ShowWindow(window,show); UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    return static_cast<int>(message.wParam);
}
