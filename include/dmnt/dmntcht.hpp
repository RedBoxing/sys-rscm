#pragma once

#include <switch.h>

namespace dmntcht
{

    typedef struct
    {
        u64 base;
        u64 size;
    } MemoryRegionExtents;

    typedef struct
    {
        u64 process_id;
        u64 title_id;
        MemoryRegionExtents main_nso_extents;
        MemoryRegionExtents heap_extents;
        MemoryRegionExtents alias_extents;
        MemoryRegionExtents address_space_extents;
        u8 main_nso_build_id[0x20];
    } CheatProcessMetadata;

    typedef struct
    {
        char readable_name[0x40];
        uint32_t num_opcodes;
        uint32_t opcodes[0x100];
    } CheatDefinition;

    typedef struct
    {
        bool enabled;
        uint32_t cheat_id;
        CheatDefinition definition;
    } CheatEntry;

    typedef struct
    {
        u64 value;
        u8 width;
    } FrozenAddressValue;

    typedef struct
    {
        u64 address;
        FrozenAddressValue value;
    } FrozenAddressEntry;

    Result initialize();
    void exit();
    Service *getServiceSession();

    Result hasCheatProcess(bool *out);
    Result getCheatProcessEvent(Event *event);
    Result getCheatProcessMetadata(CheatProcessMetadata *out_metadata);
    Result forceOpenCheatProcess();

    Result getCheatProcessMappingCount(u64 *out_count);
    Result gGetCheatProcessMappings(MemoryInfo *buffer, u64 max_count, u64 offset, u64 *out_count);
    Result readCheatProcessMemory(u64 address, void *buffer, size_t size);
    Result writeCheatProcessMemory(u64 address, const void *buffer, size_t size);
    Result queryCheatProcessMemory(MemoryInfo *mem_info, u64 address);
    Result pauseCheatProcess();
    Result resumeCheatProcess();

    Result getCheatCount(u64 *out_count);
    Result getCheats(CheatEntry *buffer, u64 max_count, u64 offset, u64 *out_count);
    Result getCheatById(CheatEntry *out_cheat, u32 cheat_id);
    Result toggleCheat(u32 cheat_id);
    Result addCheat(CheatDefinition *cheat, bool enabled, u32 *out_cheat_id);
    Result removeCheat(u32 cheat_id);
    Result readStaticRegister(u64 *out, u8 which);
    Result writeStaticRegister(u8 which, u64 value);
    Result resetStaticRegisters();

    Result getFrozenAddressCount(u64 *out_count);
    Result getFrozenAddresses(FrozenAddressEntry *buffer, u64 max_count, u64 offset, u64 *out_count);
    Result gGetFrozenAddress(FrozenAddressEntry *out, u64 address);
    Result enableFrozenAddress(u64 address, u64 width, u64 *out_value);
    Result disableFrozenAddress(u64 address);

}