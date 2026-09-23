// libvortex-bridge.so
//
// A Vortex runtime backend that stands in for QEMU so libesp_vortex_bridge.so
// can be exercised on the host, before a RISC-V Linux guest exists.
//
// It plays exactly the two roles QEMU's esp-vortex device plays:
//   1. it owns "guest physical RAM" (a host mapping) and hands the bridge the
//      same read/write callbacks QEMU installs, and
//   2. it performs the same MMIO register sequence gt_vortex_prep_xfer() in the
//      ESP Linux KMD performs, then waits for the bridge to report that the RTL
//      raised busy_interrupt.
//
// What this does NOT cover: the Linux kernel-mode driver, the ESP common
// driver, contiguous allocation, /dev/mem, and PLIC interrupt delivery. Passing
// here is an RTL/bridge smoke test, not the full-stack acceptance test.

#include <common.h>
#include <bitmanip.h>

#include <sys/mman.h>
#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../esp_vortex_bridge.h"

using namespace vortex;

namespace {

constexpr uint64_t kDefaultBaseAddr = 0xA5000000ull;
constexpr const char* kDefaultBridgeLib = "libesp_vortex_bridge.so";

// GT_VORTEX wrapper APB registers, as used by gt_vortex_rtl.c.
constexpr uint32_t REG_BASE_ADDR     = 0x50;
constexpr uint32_t REG_START_VORTEX  = 0x54;
constexpr uint32_t REG_VX_BUSY       = 0x58;
constexpr uint32_t REG_STARTUP_ADDR0 = 0x60;
constexpr uint32_t REG_STARTUP_ADDR1 = 0x64;
constexpr uint32_t REG_STARTUP_ARG0  = 0x68;
constexpr uint32_t REG_STARTUP_ARG1  = 0x6c;
constexpr uint32_t REG_MPM_CLASS     = 0x70;

uint64_t parse_u64_env(const char* name, uint64_t fallback) {
  const char* value = getenv(name);
  if (value == nullptr || *value == '\0')
    return fallback;
  char* end = nullptr;
  uint64_t parsed = strtoull(value, &end, 0);
  if (end == value)
    return fallback;
  return parsed;
}

} // namespace

class vx_device {
public:
  vx_device()
    : allocator_(ALLOC_BASE_ADDR,
                 GLOBAL_MEM_SIZE - ALLOC_BASE_ADDR,
                 RAM_PAGE_SIZE,
                 CACHE_BLOCK_SIZE)
    , base_addr_(kDefaultBaseAddr)
    , mpm_class_(0)
  {}

  ~vx_device() {
    if (bridge_ && bridge_destroy_) {
      bridge_destroy_(bridge_);
    }
    if (dl_handle_) {
      dlclose(dl_handle_);
    }
    if (ram_ && ram_ != MAP_FAILED) {
      munmap(ram_, ram_size_);
    }
    if (dump_) {
      std::fclose(dump_);
    }
  }

  int init() {
    base_addr_ = parse_u64_env("VORTEX_ESP_BASE_ADDR", kDefaultBaseAddr);
    ram_size_ = GLOBAL_MEM_SIZE;

    // Lazily-backed stand-in for the guest RAM window the accelerator sees.
    ram_ = mmap(nullptr, ram_size_, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (ram_ == MAP_FAILED) {
      std::perror("[VXBRIDGE] mmap guest RAM failed");
      return -1;
    }

    const char* lib = getenv("VORTEX_BRIDGE_LIB");
    std::string lib_path = (lib && *lib) ? lib : kDefaultBridgeLib;
    dl_handle_ = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (dl_handle_ == nullptr) {
      std::cerr << "[VXBRIDGE] cannot load " << lib_path << ": "
                << dlerror() << std::endl;
      return -1;
    }

    auto create = reinterpret_cast<void* (*)(esp_vortex_mem_read_fn,
                                             esp_vortex_mem_write_fn, void*)>(
        dlsym(dl_handle_, "esp_vortex_bridge_create"));
    bridge_destroy_ = reinterpret_cast<void (*)(void*)>(
        dlsym(dl_handle_, "esp_vortex_bridge_destroy"));
    bridge_reset_ = reinterpret_cast<void (*)(void*)>(
        dlsym(dl_handle_, "esp_vortex_bridge_reset"));
    bridge_read_ = reinterpret_cast<uint32_t (*)(void*, uint32_t)>(
        dlsym(dl_handle_, "esp_vortex_bridge_read"));
    bridge_write_ = reinterpret_cast<void (*)(void*, uint32_t, uint32_t)>(
        dlsym(dl_handle_, "esp_vortex_bridge_write"));
    bridge_run_ = reinterpret_cast<int (*)(void*)>(
        dlsym(dl_handle_, "esp_vortex_bridge_run"));
    if (!create || !bridge_destroy_ || !bridge_reset_ || !bridge_read_ ||
        !bridge_write_ || !bridge_run_) {
      std::cerr << "[VXBRIDGE] missing bridge symbol in " << lib_path
                << std::endl;
      return -1;
    }

    bridge_ = create(&vx_device::dma_read, &vx_device::dma_write, this);
    if (bridge_ == nullptr) {
      std::cerr << "[VXBRIDGE] bridge initialization failed" << std::endl;
      return -1;
    }

    DBGPRINT("DEV_INIT: bridge=%s, base=0x%" PRIx64 ", ram=%" PRIu64 " bytes\n",
             lib_path.c_str(), base_addr_, uint64_t(ram_size_));
    return 0;
  }

  int get_caps(uint32_t caps_id, uint64_t *value) {
    uint64_t _value;
    switch (caps_id) {
    case VX_CAPS_VERSION:          _value = IMPLEMENTATION_ID; break;
    case VX_CAPS_NUM_THREADS:      _value = NUM_THREADS; break;
    case VX_CAPS_NUM_WARPS:        _value = NUM_WARPS; break;
    case VX_CAPS_NUM_CORES:        _value = NUM_CORES * NUM_CLUSTERS; break;
    case VX_CAPS_CACHE_LINE_SIZE:  _value = CACHE_BLOCK_SIZE; break;
    case VX_CAPS_GLOBAL_MEM_SIZE:  _value = GLOBAL_MEM_SIZE; break;
    case VX_CAPS_LOCAL_MEM_SIZE:   _value = (1 << LMEM_LOG_SIZE); break;
    case VX_CAPS_ISA_FLAGS:
      _value = ((uint64_t(MISA_EXT))<<32) | ((log2floor(XLEN)-4) << 30) | MISA_STD;
      break;
    default:
      std::cout << "invalid caps id: " << caps_id << std::endl;
      std::abort();
      return -1;
    }
    *value = _value;
    return 0;
  }

  int mem_alloc(uint64_t size, int flags, uint64_t* dev_addr) {
    uint64_t addr;
    CHECK_ERR(allocator_.allocate(size, &addr), { return err; });
    CHECK_ERR(this->mem_access(addr, size, flags), {
      allocator_.release(addr);
      return err;
    });
    *dev_addr = addr;
    return 0;
  }

  int mem_reserve(uint64_t dev_addr, uint64_t size, int flags) {
    CHECK_ERR(allocator_.reserve(dev_addr, size), { return err; });
    CHECK_ERR(this->mem_access(dev_addr, size, flags), {
      allocator_.release(dev_addr);
      return err;
    });
    return 0;
  }

  int mem_free(uint64_t dev_addr) {
    return allocator_.release(dev_addr);
  }

  int mem_access(uint64_t dev_addr, uint64_t size, int /*flags*/) {
    if (size == 0)
      return 0;
    uint64_t asize = aligned_size(size, CACHE_BLOCK_SIZE);
    if (dev_addr + asize < dev_addr)
      return -1;
    if (dev_addr + asize > GLOBAL_MEM_SIZE)
      return -1;
    return 0;
  }

  int mem_info(uint64_t* mem_free, uint64_t* mem_used) const {
    if (mem_free) *mem_free = allocator_.free();
    if (mem_used) *mem_used = allocator_.allocated();
    return 0;
  }

  int upload(uint64_t dev_addr, const void* src, uint64_t size) {
    if (src == nullptr || size == 0)
      return -1;
    if (this->mem_access(dev_addr, size, VX_MEM_WRITE) != 0)
      return -1;
    std::memcpy(static_cast<uint8_t*>(ram_) + dev_addr, src, size);
    if (dump_enabled_ && !launched_) {
      uploads_.push_back({dev_addr, size});
    }
    return 0;
  }

  int download(void* dst, uint64_t dev_addr, uint64_t size) {
    if (dst == nullptr || size == 0)
      return -1;
    if (this->mem_access(dev_addr, size, VX_MEM_READ) != 0)
      return -1;
    std::memcpy(dst, static_cast<uint8_t*>(ram_) + dev_addr, size);
    if (dump_enabled_ && launched_) {
      this->dump_record("expect", dev_addr, size);
    }
    return 0;
  }

  // Mirrors gt_vortex_prep_xfer() followed by the ESP common driver's
  // esp_run()/esp_wait() pair.
  int start(uint64_t krnl_addr, uint64_t args_addr) {
    if (bridge_ == nullptr)
      return -1;

    this->dump_launch(krnl_addr, args_addr);

    bridge_write_(bridge_, REG_START_VORTEX, 0);
    bridge_write_(bridge_, REG_BASE_ADDR, uint32_t(base_addr_ & 0xffffffffu));
    bridge_write_(bridge_, REG_STARTUP_ADDR0, uint32_t(krnl_addr & 0xffffffffu));
    bridge_write_(bridge_, REG_STARTUP_ADDR1, uint32_t(krnl_addr >> 32));
    bridge_write_(bridge_, REG_STARTUP_ARG0, uint32_t(args_addr & 0xffffffffu));
    bridge_write_(bridge_, REG_STARTUP_ARG1, uint32_t(args_addr >> 32));
    bridge_write_(bridge_, REG_MPM_CLASS, mpm_class_);
    (void)bridge_read_(bridge_, REG_MPM_CLASS);
    bridge_write_(bridge_, REG_START_VORTEX, 1);
    (void)bridge_read_(bridge_, REG_START_VORTEX);

    // Stands in for CMD_REG=1 plus wait_for_completion_interruptible().
    int rc = bridge_run_(bridge_);
    if (rc != 0) {
      std::cerr << "[VXBRIDGE] RTL launch failed (" << rc << ")" << std::endl;
      return -1;
    }

    // The ESP third-party IRQ handler treats bit 0 of 0x58 as "still busy".
    uint32_t busy = bridge_read_(bridge_, REG_VX_BUSY);
    if (busy & 1u) {
      std::cerr << "[VXBRIDGE] completion reported while still busy"
                << std::endl;
      return -1;
    }

    mpm_cache_.clear();
    return 0;
  }

  int ready_wait(uint64_t /*timeout*/) {
    return 0; // start() is synchronous, exactly like the blocking ESP ioctl
  }

  int dcr_write(uint32_t addr, uint32_t value) {
    dcrs_.write(addr, value);
    if (addr == VX_DCR_BASE_MPM_CLASS) {
      mpm_class_ = value;
    }
    return 0;
  }

  int dcr_read(uint32_t addr, uint32_t* value) const {
    return dcrs_.read(addr, value);
  }

  int mpm_query(uint32_t addr, uint32_t core_id, uint64_t* value) {
    uint32_t offset = addr - VX_CSR_MPM_BASE;
    if (offset > 31)
      return -1;
    if (mpm_cache_.count(core_id) == 0) {
      uint64_t mpm_mem_addr = IO_MPM_ADDR + core_id * 32 * sizeof(uint64_t);
      CHECK_ERR(this->download(mpm_cache_[core_id].data(), mpm_mem_addr,
                               32 * sizeof(uint64_t)), { return err; });
    }
    *value = mpm_cache_.at(core_id).at(offset);
    return 0;
  }

private:
  // Optional launch capture. Setting VORTEX_BRIDGE_DUMP=<file> records the
  // exact pre-launch memory image, the launch register values, and the
  // post-launch result buffers, so the identical launch can be replayed
  // through QEMU's esp-vortex device with qtest_launch.py.
  void dump_launch(uint64_t krnl_addr, uint64_t args_addr) {
    if (!dump_enabled_ || launched_)
      return;
    const char* path = getenv("VORTEX_BRIDGE_DUMP");
    dump_ = std::fopen(path, "w");
    if (dump_ == nullptr) {
      std::perror("[VXBRIDGE] cannot open dump file");
      dump_enabled_ = false;
      return;
    }
    std::fprintf(dump_, "base 0x%" PRIx64 "\n", base_addr_);
    std::fprintf(dump_, "krnl 0x%" PRIx64 "\n", krnl_addr);
    std::fprintf(dump_, "args 0x%" PRIx64 "\n", args_addr);
    std::fprintf(dump_, "mpm 0x%x\n", mpm_class_);
    for (const auto& r : uploads_) {
      this->dump_record("region", r.first, r.second);
    }
    launched_ = true;
  }

  void dump_record(const char* tag, uint64_t dev_addr, uint64_t size) {
    if (dump_ == nullptr)
      return;
    std::fprintf(dump_, "%s 0x%" PRIx64 " %" PRIu64 " ", tag, dev_addr, size);
    const uint8_t* p = static_cast<uint8_t*>(ram_) + dev_addr;
    for (uint64_t i = 0; i < size; ++i) {
      std::fprintf(dump_, "%02x", p[i]);
    }
    std::fprintf(dump_, "\n");
    std::fflush(dump_);
  }

  // Bridge memory callbacks. Addresses arrive as guest physical addresses,
  // i.e. already offset by the launch base register.
  static int dma_read(void* opaque, uint64_t gpa, void* data, size_t len) {
    auto* self = static_cast<vx_device*>(opaque);
    uint64_t off = gpa - self->base_addr_;
    if (off + len > self->ram_size_ || off + len < off) {
      std::cerr << "[VXBRIDGE] out-of-window read at 0x" << std::hex << gpa
                << std::dec << std::endl;
      return -1;
    }
    std::memcpy(data, static_cast<uint8_t*>(self->ram_) + off, len);
    return 0;
  }

  static int dma_write(void* opaque, uint64_t gpa, const void* data,
                       size_t len) {
    auto* self = static_cast<vx_device*>(opaque);
    uint64_t off = gpa - self->base_addr_;
    if (off + len > self->ram_size_ || off + len < off) {
      std::cerr << "[VXBRIDGE] out-of-window write at 0x" << std::hex << gpa
                << std::dec << std::endl;
      return -1;
    }
    std::memcpy(static_cast<uint8_t*>(self->ram_) + off, data, len);
    return 0;
  }

  MemoryAllocator allocator_;
  DeviceConfig dcrs_;
  uint64_t base_addr_;
  uint32_t mpm_class_;
  std::unordered_map<uint32_t, std::array<uint64_t, 32>> mpm_cache_;

  void*  ram_ = nullptr;
  size_t ram_size_ = 0;

  bool dump_enabled_ = getenv("VORTEX_BRIDGE_DUMP") != nullptr;
  bool launched_ = false;
  std::FILE* dump_ = nullptr;
  std::vector<std::pair<uint64_t, uint64_t>> uploads_;

  void* dl_handle_ = nullptr;
  void* bridge_ = nullptr;
  void (*bridge_destroy_)(void*) = nullptr;
  void (*bridge_reset_)(void*) = nullptr;
  uint32_t (*bridge_read_)(void*, uint32_t) = nullptr;
  void (*bridge_write_)(void*, uint32_t, uint32_t) = nullptr;
  int (*bridge_run_)(void*) = nullptr;
};

#include <callbacks.inc>
