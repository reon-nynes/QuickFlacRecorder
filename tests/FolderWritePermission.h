// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <stdexcept>
#include <vector>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// Restore permissions before the fixture folder is cleaned up, including on failure.
class FolderWritePermission final
{
public:
    explicit FolderWritePermission(juce::File folderIn) : folder(std::move(folderIn))
    {
#if JUCE_WINDOWS
        DWORD needed = 0;
        const auto path = folder.getFullPathName();
        GetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
        original.resize(needed);
        if (!GetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION, original.data(), needed, &needed))
            throw std::runtime_error("read folder ACL");
        PSECURITY_DESCRIPTOR denied = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;GW;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &denied, nullptr))
            throw std::runtime_error("create denied folder ACL");
        active = SetFileSecurityW(path.toWideCharPointer(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, denied) != 0;
        LocalFree(denied);
#else
        struct stat info {};
        if (::stat(folder.getFullPathName().toRawUTF8(), &info) != 0) throw std::runtime_error("read folder permissions");
        original = info.st_mode & 07777;
        active = ::chmod(folder.getFullPathName().toRawUTF8(), original & ~0222) == 0;
#endif
        if (!active) throw std::runtime_error("deny folder writes");
    }
    ~FolderWritePermission() { restore(); }
    void restore()
    {
        if (!active) return;
#if JUCE_WINDOWS
        active = !SetFileSecurityW(folder.getFullPathName().toWideCharPointer(), DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, original.data());
#else
        active = ::chmod(folder.getFullPathName().toRawUTF8(), original) != 0;
#endif
    }
private:
    juce::File folder;
    bool active = false;
#if JUCE_WINDOWS
    std::vector<unsigned char> original;
#else
    mode_t original = 0;
#endif
};
