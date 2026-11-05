#pragma once
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace launcherpaths {
namespace fs=std::filesystem;
struct Layout { fs::path root; bool portable{}; };
namespace detail {
inline bool regularFile(const fs::path& path) {
    std::error_code error;return fs::is_regular_file(path,error);
}
inline bool sameComponent(const fs::path& left,const fs::path& right) {
#ifdef _WIN32
    const auto a=left.native(),b=right.native();
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(std::towlower(a[i])!=std::towlower(b[i]))return false;
    return true;
#else
    return left==right;
#endif
}
}
inline Layout locate(const fs::path& executable,const fs::path& legacyDefault) {
    for(auto candidate=executable.parent_path();!candidate.empty();) {
        // Keep incomplete marked copies local instead of launching a different game.
        if(detail::regularFile(candidate/L"CastlevaniaPortable.txt"))return {candidate,true};
        if(detail::regularFile(candidate/L"work"/L"Player"/L"Castlevania.exe"))return {candidate,false};
        const auto parent=candidate.parent_path();if(parent==candidate)break;candidate=parent;
    }
    return {legacyDefault,false};
}
inline fs::path gameFolder(const Layout& layout,bool preview) {
    if(layout.portable)return layout.root/L"Game";
    return layout.root/L"work"/(preview?L"PlayerOpenGL":L"Player");
}
inline fs::path resolveOriginal(const Layout& layout,const std::wstring& configured,const fs::path& legacyDefault) {
    const auto bundled=layout.root/L"Native"/L"Frameworks"/L"GameAssembly.dylib";
    if(layout.portable && detail::regularFile(bundled))return bundled;
    if(!configured.empty()) {
        const fs::path selected(configured);
        return selected.is_absolute()?selected:(layout.root/selected).lexically_normal();
    }
    return layout.portable?bundled:legacyDefault;
}
inline std::wstring storedOriginal(const Layout& layout,const fs::path& selected) {
    const auto root=fs::absolute(layout.root).lexically_normal();
    const auto absolute=fs::absolute(selected).lexically_normal();
    auto rootPart=root.begin(),selectedPart=absolute.begin();
    for(;rootPart!=root.end();++rootPart,++selectedPart) {
        if(selectedPart==absolute.end() || !detail::sameComponent(*rootPart,*selectedPart))return absolute.wstring();
    }
    fs::path relative;for(;selectedPart!=absolute.end();++selectedPart)relative/=*selectedPart;
    return relative.empty()?absolute.wstring():relative.generic_wstring();
}
}
