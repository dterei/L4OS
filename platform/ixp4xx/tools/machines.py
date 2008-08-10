class ixp4xx(xscale):
    platform = "ixp4xx"
    platform_dir = "ixp4xx"
    wombat_config = "l4linux_config_ixp4xx"

class nslu2(ixp4xx):
    cpp_defines = ixp4xx.cpp_defines + [("CONFIG_PERF", 1)]
    endian = "big"
    
    memory = ixp4xx.memory.copy()
    base_vaddr = 0x140000 # sos expects to be mapped 1:1
    memory['virtual'] = [Region(0x00100000L, 0xd0000000)]
    memory['physical'] = [Region(0x00100000L, 0x02000000L)]
    memory['io_data'] = [Region(0x48000000L, 0x64000000L, "dedicated")]
    memory['bus_config'] = [Region(0xc0000000L, 0xc8000000L, "dedicated")]
    memory['iodevices'] = [Region(0xc8000000L, 0xc800c000L, "dedicated")]
    memory['sdram'] = [Region(0xcc000000L, 0xcc001000L, "dedicated")]
    
    zero_bss = True
    
    #memory_timer  = [(0xc8005000, 0xc8006000)]
    #interrupt_timer  = [18]
    #serial_driver = "drv_l4_kdb"
    #memory_serial = [(0xc8000000, 0xc8001000)]
    #interrupt_serial = [15]
    
    skyeye = "nslu2.skyeye"
    subplatform = "ixp420"
    toolchain = generic_gcc("armv5b-softfloat-linux-")
    uart = "HSUART"
    #drivers = [serial_driver]
    virtual = False
    boot_binary = True
