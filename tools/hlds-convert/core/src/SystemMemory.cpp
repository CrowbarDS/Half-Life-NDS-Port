#include "hlds/SystemMemory.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#include <string>
#endif

namespace hlds
{
    std::uint64_t freeMemoryBytes()
    {
#if defined(_WIN32)
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        return GlobalMemoryStatusEx(&status) ? static_cast<std::uint64_t>(status.ullAvailPhys) : 0;
#elif defined(__APPLE__)
        // (Free pages, and those the system takes back at once: inactive.)
        vm_size_t page = 0;
        vm_statistics64_data_t stats{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_page_size(mach_host_self(), &page) != KERN_SUCCESS ||
            host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) !=
                KERN_SUCCESS)
            return 0;
        return (static_cast<std::uint64_t>(stats.free_count) + stats.inactive_count) * page;
#else
        // (Linux's own reckoning of what can be had without swapping.)
        std::ifstream in("/proc/meminfo");
        std::string key, unit;
        std::uint64_t value = 0;
        while (in >> key >> value >> unit)
        {
            if (key == "MemAvailable:")
                return value * 1024;
        }
        return 0;
#endif
    }
} // namespace hlds
