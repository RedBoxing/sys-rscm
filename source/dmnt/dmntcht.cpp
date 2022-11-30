#include "dmnt/dmntcht.hpp"

namespace dmntcht
{
    static Service g_dmntchtSrv;

    Result initialize()
    {
        return smGetService(&g_dmntchtSrv, "dmnt:cht");
    }

    void exit()
    {
        serviceClose(&g_dmntchtSrv);
    }

    Service *getServiceSession()
    {
        return &g_dmntchtSrv;
    }

    Result hasCheatProcess(bool *out)
    {
        u8 tmp;
        Result rc = serviceDispatchOut(&g_dmntchtSrv, 65000, tmp);
        if (R_SUCCEEDED(rc) && out)
            *out = tmp & 1;
        return rc;
    }

    Result getCheatProcessEvent(Event *event)
    {
        Handle evt_handle;
        Result rc = serviceDispatch(&g_dmntchtSrv, 65001,
                                    .out_handle_attrs = {SfOutHandleAttr_HipcCopy},
                                    .out_handles = &evt_handle, );

        if (R_SUCCEEDED(rc))
        {
            eventLoadRemote(event, evt_handle, true);
        }

        return rc;
    }

    Result getCheatProcessMetadata(CheatProcessMetadata *out_metadata)
    {
        return serviceDispatchOut(&g_dmntchtSrv, 65002, *out_metadata);
    }

    Result forceOpenCheatProcess(void)
    {
        return serviceDispatch(&g_dmntchtSrv, 65003);
    }

    Result pauseCheatProcess(void)
    {
        return serviceDispatch(&g_dmntchtSrv, 65004);
    }

    Result resumeCheatProcess(void)
    {
        return serviceDispatch(&g_dmntchtSrv, 65005);
    }

    static Result getCount(u64 *out_count, u32 cmd_id)
    {
        return serviceDispatchOut(&g_dmntchtSrv, cmd_id, *out_count);
    }

    static Result getEntries(void *buffer, u64 buffer_size, u64 offset, u64 *out_count, u32 cmd_id)
    {
        return serviceDispatchInOut(&g_dmntchtSrv, cmd_id, offset, *out_count,
                                    .buffer_attrs = {SfBufferAttr_Out | SfBufferAttr_HipcMapAlias},
                                    .buffers = {{buffer, buffer_size}}, );
    }

    static Result cmdInU32NoOut(u32 in, u32 cmd_id)
    {
        return serviceDispatchIn(&g_dmntchtSrv, cmd_id, in);
    }

    Result getCheatProcessMappingCount(u64 *out_count)
    {
        return getCount(out_count, 65100);
    }

    Result getCheatProcessMappings(MemoryInfo *buffer, u64 max_count, u64 offset, u64 *out_count)
    {
        return getEntries(buffer, sizeof(*buffer) * max_count, offset, out_count, 65101);
    }

    Result readCheatProcessMemory(u64 address, void *buffer, size_t size)
    {
        const struct
        {
            u64 address;
            u64 size;
        } in = {address, size};
        return serviceDispatchIn(&g_dmntchtSrv, 65102, in,
                                 .buffer_attrs = {SfBufferAttr_Out | SfBufferAttr_HipcMapAlias},
                                 .buffers = {{buffer, size}}, );
    }

    Result writeCheatProcessMemory(u64 address, const void *buffer, size_t size)
    {
        const struct
        {
            u64 address;
            u64 size;
        } in = {address, size};
        return serviceDispatchIn(&g_dmntchtSrv, 65103, in,
                                 .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcMapAlias},
                                 .buffers = {{buffer, size}}, );
    }

    Result queryCheatProcessMemory(MemoryInfo *mem_info, u64 address)
    {
        return serviceDispatchInOut(&g_dmntchtSrv, 65104, address, *mem_info);
    }

    Result getCheatCount(u64 *out_count)
    {
        return getCount(out_count, 65200);
    }

    Result getCheats(CheatEntry *buffer, u64 max_count, u64 offset, u64 *out_count)
    {
        return getEntries(buffer, sizeof(*buffer) * max_count, offset, out_count, 65201);
    }

    Result getCheatById(CheatEntry *out, u32 cheat_id)
    {
        return serviceDispatchIn(&g_dmntchtSrv, 65202, cheat_id,
                                 .buffer_attrs = {SfBufferAttr_Out | SfBufferAttr_HipcMapAlias | SfBufferAttr_FixedSize},
                                 .buffers = {{out, sizeof(*out)}}, );
    }

    Result toggleCheat(u32 cheat_id)
    {
        return cmdInU32NoOut(cheat_id, 65203);
    }

    Result addCheat(CheatDefinition *cheat_def, bool enabled, u32 *out_cheat_id)
    {
        const u8 in = enabled != 0;
        return serviceDispatchInOut(&g_dmntchtSrv, 65204, in, *out_cheat_id,
                                    .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcMapAlias | SfBufferAttr_FixedSize},
                                    .buffers = {{cheat_def, sizeof(*cheat_def)}}, );
    }

    Result removeCheat(u32 cheat_id)
    {
        return cmdInU32NoOut(cheat_id, 65205);
    }

    Result readStaticRegister(u64 *out, u8 which)
    {
        return serviceDispatchInOut(&g_dmntchtSrv, 65206, which, *out);
    }

    Result writeStaticRegister(u8 which, u64 value)
    {
        const struct
        {
            u64 which;
            u64 value;
        } in = {which, value};

        return serviceDispatchIn(&g_dmntchtSrv, 65207, in);
    }

    Result resetStaticRegisters()
    {
        return serviceDispatch(&g_dmntchtSrv, 65208);
    }

    Result getFrozenAddressCount(u64 *out_count)
    {
        return getCount(out_count, 65300);
    }

    Result getFrozenAddresses(FrozenAddressEntry *buffer, u64 max_count, u64 offset, u64 *out_count)
    {
        return getEntries(buffer, sizeof(*buffer) * max_count, offset, out_count, 65301);
    }

    Result getFrozenAddress(FrozenAddressEntry *out, u64 address)
    {
        return serviceDispatchInOut(&g_dmntchtSrv, 65302, address, *out);
    }

    Result enableFrozenAddress(u64 address, u64 width, u64 *out_value)
    {
        const struct
        {
            u64 address;
            u64 width;
        } in = {address, width};
        return serviceDispatchInOut(&g_dmntchtSrv, 65303, in, *out_value);
    }

    Result disableFrozenAddress(u64 address)
    {
        return serviceDispatchIn(&g_dmntchtSrv, 65304, address);
    }
}