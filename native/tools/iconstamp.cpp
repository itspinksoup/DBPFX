// dbpfx-iconstamp - give Windows executables an icon taken from DBPFX.Identity.dll (Windows only)
//
//   dbpfx-iconstamp <DBPFX.Identity.dll> <STABLE|BETA|CANARY|DEVELOPER> <file.exe> [more.exe ...]
//
// DBPFX.Identity.dll is a resource-only library holding one icon group per release channel. Windows
// shows an executable's icon only from the executable's own resources, so the build copies the
// chosen group (and its images) out of the DLL into each .exe: every existing icon of the target is
// removed first. File timestamps are kept, so incremental builds do not see the file as new.
#ifndef _WIN32
#include <cstdio>
int main() {
    std::fprintf(stderr, "dbpfx-iconstamp only runs on Windows\n");
    return 1;
}
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// wide-character API throughout: RT_ICON / RT_GROUP_ICON must expand to MAKEINTRESOURCEW
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

#pragma pack(push, 2)
struct icon_dir {
    WORD reserved;
    WORD type;  // 1 = icon
    WORD count;
};
struct group_icon_entry {  // RT_GROUP_ICON entry
    BYTE width, height, colors, reserved;
    WORD planes, bit_count;
    DWORD bytes;
    WORD id;  // RT_ICON resource holding the image
};
#pragma pack(pop)

struct icon_image {
    group_icon_entry entry;
    std::vector<char> data;
};

std::string last_error(const char* what) { return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")"; }

// ------------------------------------------------------------------ reading the channel icon from the DLL
bool load_group(const std::wstring& dll, const std::wstring& group, std::vector<icon_image>& images, std::string& error) {
    HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        error = last_error("loading the icon library");
        return false;
    }
    bool ok = false;
    if (HRSRC info = FindResourceW(module, group.c_str(), RT_GROUP_ICON)) {
        const auto* bytes = static_cast<const char*>(LockResource(LoadResource(module, info)));
        const DWORD size = SizeofResource(module, info);
        icon_dir dir{};
        if (bytes && size >= sizeof dir) std::memcpy(&dir, bytes, sizeof dir);
        if (bytes && dir.type == 1 && dir.count && size >= sizeof dir + dir.count * sizeof(group_icon_entry)) {
            ok = true;
            for (WORD i = 0; i < dir.count && ok; ++i) {
                icon_image image;
                std::memcpy(&image.entry, bytes + sizeof dir + i * sizeof(group_icon_entry), sizeof image.entry);
                HRSRC part = FindResourceW(module, MAKEINTRESOURCEW(image.entry.id), RT_ICON);
                const auto* data = part ? static_cast<const char*>(LockResource(LoadResource(module, part))) : nullptr;
                if (!data) {
                    error = "an image of the icon group is missing from the library";
                    ok = false;
                    break;
                }
                image.data.assign(data, data + SizeofResource(module, part));
                images.push_back(std::move(image));
            }
        } else {
            error = "the icon group is damaged";
        }
    } else {
        error = "the library has no icon group of that name";
    }
    FreeLibrary(module);
    return ok;
}

// ------------------------------------------------------------------ icons already in the target
struct resource_name {
    bool is_id = false;
    WORD id = 0;
    std::wstring text;
    LPCWSTR get() const { return is_id ? MAKEINTRESOURCEW(id) : text.c_str(); }
};
struct existing_resource {
    LPCWSTR type = nullptr;
    resource_name name;
    WORD language = 0;
};
struct name_ctx {
    LPCWSTR type;
    std::vector<existing_resource>* out;
};
struct lang_ctx {
    existing_resource base;
    std::vector<existing_resource>* out;
};

BOOL CALLBACK on_language(HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR param) {
    auto* ctx = reinterpret_cast<lang_ctx*>(param);
    existing_resource r = ctx->base;
    r.language = language;
    ctx->out->push_back(std::move(r));
    return TRUE;
}

BOOL CALLBACK on_name(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param) {
    auto* ctx = reinterpret_cast<name_ctx*>(param);
    lang_ctx lc;
    lc.base.type = ctx->type;
    if (IS_INTRESOURCE(name)) {
        lc.base.name.is_id = true;
        lc.base.name.id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
    } else {
        lc.base.name.text = name;
    }
    lc.out = ctx->out;
    EnumResourceLanguagesW(module, type, name, on_language, reinterpret_cast<LONG_PTR>(&lc));
    return TRUE;
}

std::vector<existing_resource> existing_icons(const std::wstring& path) {
    std::vector<existing_resource> found;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) return found;  // no resources yet: nothing to remove
    for (LPCWSTR type : {RT_GROUP_ICON, RT_ICON}) {
        name_ctx ctx{type, &found};
        EnumResourceNamesW(module, type, on_name, reinterpret_cast<LONG_PTR>(&ctx));
    }
    FreeLibrary(module);
    return found;
}

// ------------------------------------------------------------------ writing
bool stamp(const std::vector<icon_image>& images, const std::wstring& exe, std::string& error) {
    FILETIME created{}, accessed{}, written{};
    HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        error = last_error("opening the file");
        return false;
    }
    const bool have_times = GetFileTime(f, &created, &accessed, &written) != 0;
    CloseHandle(f);

    const auto old = existing_icons(exe);
    HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
    if (!update) {
        error = last_error("BeginUpdateResource");
        return false;
    }
    bool ok = true;
    for (const auto& r : old)
        ok = ok && UpdateResourceW(update, r.type, r.name.get(), r.language, nullptr, 0);

    const WORD neutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    const icon_dir dir{0, 1, static_cast<WORD>(images.size())};
    std::vector<char> group(sizeof dir + images.size() * sizeof(group_icon_entry));
    std::memcpy(group.data(), &dir, sizeof dir);
    for (std::size_t i = 0; i < images.size() && ok; ++i) {
        group_icon_entry e = images[i].entry;
        e.id = static_cast<WORD>(i + 1);
        ok = UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(e.id), neutral, const_cast<char*>(images[i].data.data()),
                             static_cast<DWORD>(images[i].data.size())) != 0;
        std::memcpy(group.data() + sizeof dir + i * sizeof e, &e, sizeof e);
    }
    ok = ok && UpdateResourceW(update, RT_GROUP_ICON, L"MAINICON", neutral, group.data(), static_cast<DWORD>(group.size()));
    if (!ok) {
        error = last_error("UpdateResource");
        EndUpdateResourceW(update, TRUE);  // discard every change
        return false;
    }
    if (!EndUpdateResourceW(update, FALSE)) {
        error = last_error("EndUpdateResource");
        return false;
    }
    if (have_times) {
        f = CreateFileW(exe.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            SetFileTime(f, &created, &accessed, &written);
            CloseHandle(f);
        }
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dbpfx-iconstamp <DBPFX.Identity.dll> <STABLE|BETA|CANARY|DEVELOPER> <file.exe> [more.exe ...]\n");
        return 2;
    }
    std::vector<icon_image> images;
    std::string error;
    if (!load_group(argv[1], argv[2], images, error)) {
        std::fwprintf(stderr, L"icon %ls from %ls: %hs\n", argv[2], argv[1], error.c_str());
        return 1;
    }
    int failures = 0;
    for (int i = 3; i < argc; ++i) {
        if (stamp(images, argv[i], error)) {
            std::fwprintf(stdout, L"%ls icon: %ls\n", argv[2], argv[i]);
        } else {
            ++failures;
            std::fwprintf(stderr, L"%ls icon NOT set: %ls: %hs\n", argv[2], argv[i], error.c_str());
        }
    }
    return failures ? 1 : 0;
}
#endif
