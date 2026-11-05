#include "launcher_paths.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);
    check(bool(output),"Cannot create path fixture");output << "fixture\n";check(bool(output),"Cannot write fixture");
}
struct Fixture {
    fs::path root;
    Fixture() {
        const auto seed=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<100;++attempt) {
            const auto candidate=fs::temp_directory_path()/(L"Castlevania-launcher-paths-"+std::to_wstring(seed)+L"-"+std::to_wstring(attempt));
            if(fs::create_directory(candidate)){root=candidate;return;}
        }
        throw std::runtime_error("Cannot create private fixture directory");
    }
    ~Fixture() {
        // Only the unique private fixture tree is removed.
        if(!root.empty()){std::error_code error;fs::remove_all(root,error);}
    }
};
void run() {
    using namespace launcherpaths;
    Fixture fixture;
    const auto fallback=fixture.root/L"legacy default";
    const auto originalFallback=fixture.root/L"original legacy"/L"GameAssembly.dylib";
    touch(fallback/L"work"/L"Player"/L"Castlevania.exe");touch(originalFallback);
    const auto legacy=locate(fallback/L"outputs"/L"CastlevaniaLauncher.exe",fixture.root/L"wrong");
    check(!legacy.portable && legacy.root==fallback,"Legacy ancestor discovery failed");
    check(gameFolder(legacy,false)==fallback/L"work"/L"Player","Legacy player path changed");
    check(gameFolder(legacy,true)==fallback/L"work"/L"PlayerOpenGL","Legacy preview path changed");
    check(resolveOriginal(legacy,L"",originalFallback)==originalFallback,"Legacy original fallback failed");
    check(locate(fixture.root/L"unknown"/L"Launcher.exe",fallback).root==fallback,"Legacy fallback failed");
    const auto oldRoot=fallback/L"portable copy \u30b2\u30fc\u30e0";
    touch(oldRoot/L"CastlevaniaPortable.txt");
    const auto missing=locate(oldRoot/L"CastlevaniaLauncher.exe",fallback);
    check(missing.portable && missing.root==oldRoot,"Portable marker lost to legacy ancestor");
    check(gameFolder(missing,false)==oldRoot/L"Game" && gameFolder(missing,true)==oldRoot/L"Game","Portable build escaped bundle");
    const auto expected=oldRoot/L"Native"/L"Frameworks"/L"GameAssembly.dylib";
    check(resolveOriginal(missing,L"",originalFallback)==expected,"Incomplete bundle used original machine files");
    check(resolveOriginal(missing,L"Other/Frameworks/GameAssembly.dylib",originalFallback)==oldRoot/L"Other"/L"Frameworks"/L"GameAssembly.dylib","Relative config escaped root");
    check(resolveOriginal(missing,originalFallback.wstring(),{})==originalFallback,"Absolute config changed");
    touch(expected);touch(oldRoot/L"Game"/L"Castlevania.exe");
    check(resolveOriginal(missing,originalFallback.wstring(),{})==expected,"Bundled original lost to stale config");
    const auto stored=storedOriginal(missing,expected);
    check(fs::path(stored)==fs::path(L"Native/Frameworks/GameAssembly.dylib"),"Contained path was not saved relative");
    check(fs::path(storedOriginal(missing,originalFallback))==originalFallback,"External path must remain absolute");
    const auto sibling=oldRoot.parent_path()/(oldRoot.filename().wstring()+L" sibling")/L"GameAssembly.dylib";
    check(fs::path(storedOriginal(missing,sibling))==sibling,"Sibling name prefix was treated as contained");
    check(fs::path(storedOriginal(missing,oldRoot/L".."/L"outside"/L"GameAssembly.dylib"))==oldRoot.parent_path()/L"outside"/L"GameAssembly.dylib","Parent traversal saved relative");
#ifdef _WIN32
    auto otherDrive=oldRoot.root_name().wstring();
    check(otherDrive.size()==2 && otherDrive[1]==L':',"Expected drive-qualified temp path");
    otherDrive[0]=otherDrive[0]==L'Z'?L'Y':L'Z';
    const fs::path foreign(otherDrive+L"\\elsewhere\\GameAssembly.dylib");
    check(fs::path(storedOriginal(missing,foreign))==foreign,"Another drive was converted to relative");
    auto alternateCase=expected.wstring();for(auto& character:alternateCase)character=std::towupper(character);
    check(fs::path(storedOriginal(missing,alternateCase)).is_relative(),"Windows casing prevented relative config");
#endif
    const auto movedRoot=fixture.root/L"relocated game \u00e9\u96ea";
    fs::rename(oldRoot,movedRoot);
    const auto moved=locate(movedRoot/L"tools"/L"CastlevaniaLauncher.exe",fallback);
    check(moved.portable && moved.root==movedRoot,"Moved bundle retained old root");
    check(resolveOriginal(moved,stored,originalFallback)==movedRoot/L"Native"/L"Frameworks"/L"GameAssembly.dylib","Relative config failed after move");
    check(gameFolder(moved,true)==movedRoot/L"Game","Game path did not follow moved launcher");
}
}
int main() {
    try {run();std::cout << "PASS portable/legacy discovery, relocation, and configuration\n";return 0;}
    catch(const std::exception& error){std::cerr << "FAIL launcher paths: " << error.what() << '\n';return 1;}
}
