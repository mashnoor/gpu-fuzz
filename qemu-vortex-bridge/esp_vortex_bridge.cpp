/*
 * libesp_vortex_bridge.so
 *
 * Drives the real Verilated ESP GT_VORTEX_wrapper (which instantiates
 * Vortex_axi -> Vortex) on behalf of QEMU's "esp-vortex" device model:
 *
 *   - APB writes/reads from the ESP Linux KMD are replayed onto the wrapper's
 *     APB port (offsets 0x50..0x70).
 *   - The wrapper's 64-bit AXI master is serviced against QEMU guest physical
 *     RAM through the callbacks QEMU installs at create time.
 *   - A launch runs the RTL clock until the wrapper pulses busy_interrupt.
 *
 * There is no mock completion path: esp_vortex_bridge_run() only returns
 * success when the RTL itself raises busy_interrupt.
 */

#include "esp_vortex_bridge.h"

#include "VGT_VORTEX_wrapper.h"
#include <verilated.h>
#if VM_TRACE
#include <verilated_vcd_c.h>
#endif

#include <VX_config.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <exception>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ */
/* DPI support hooks expected by hw/dpi/util_dpi.cpp                    */
/* ------------------------------------------------------------------ */

static bool g_trace_enabled = false;

bool sim_trace_enabled() { return g_trace_enabled; }
void sim_trace_enable(bool enable) { g_trace_enabled = enable; }

/* ------------------------------------------------------------------ */

namespace {

/* Wrapper APB register offsets (mirrors gt_vortex_rtl.c). */
constexpr uint32_t REG_BASE_ADDR     = 0x50;
constexpr uint32_t REG_START_VORTEX  = 0x54;
constexpr uint32_t REG_VX_BUSY       = 0x58;
[[maybe_unused]] constexpr uint32_t REG_STARTUP_ADDR0 = 0x60;
[[maybe_unused]] constexpr uint32_t REG_STARTUP_ADDR1 = 0x64;
[[maybe_unused]] constexpr uint32_t REG_STARTUP_ARG0  = 0x68;
[[maybe_unused]] constexpr uint32_t REG_STARTUP_ARG1  = 0x6c;
[[maybe_unused]] constexpr uint32_t REG_MPM_CLASS     = 0x70;

constexpr size_t BLOCK_SIZE = MEM_BLOCK_SIZE;

/* Default execution watchdog, in RTL clock cycles. */
constexpr uint64_t DEFAULT_TIMEOUT_CYCLES = 200ull * 1000 * 1000;

static uint64_t env_u64(const char *name, uint64_t fallback) {
    const char *s = getenv(name);
    if (!s || !*s) {
        return fallback;
    }
    return strtoull(s, nullptr, 0);
}

static bool env_flag(const char *name) {
    const char *s = getenv(name);
    return s && *s && strcmp(s, "0") != 0;
}

struct MemRsp {
    uint32_t tag;
    uint8_t  data[BLOCK_SIZE];
};

class Bridge {
public:
    Bridge(esp_vortex_mem_read_fn read_cb, esp_vortex_mem_write_fn write_cb,
           void *opaque)
        : read_cb_(read_cb), write_cb_(write_cb), opaque_(opaque) {
        verbose_ = env_flag("ESP_VORTEX_BRIDGE_VERBOSE");
        timeout_cycles_ = env_u64("ESP_VORTEX_BRIDGE_TIMEOUT",
                                  DEFAULT_TIMEOUT_CYCLES);

        ctx_ = new VerilatedContext();
        ctx_->randReset(2);
        ctx_->randSeed(50);
        /*
         * The wrapper leaves Vortex_axi out of reset until the first launch's
         * soft reset, so the core free-runs with randomized state before then.
         * Assertions are only meaningful once the launch has reset it, and the
         * AXI slave stays closed until then so no guest RAM can be touched.
         */
        ctx_->assertOn(false);

        device_ = new VGT_VORTEX_wrapper(ctx_);

#if VM_TRACE
        const char *vcd = getenv("ESP_VORTEX_BRIDGE_VCD");
        if (vcd && *vcd) {
            ctx_->traceEverOn(true);
            tfp_ = new VerilatedVcdC();
            device_->trace(tfp_, 99);
            tfp_->open(vcd);
            log("tracing to %s", vcd);
        }
#endif
        this->hard_reset();
        log("created (block=%zu bytes, timeout=%" PRIu64 " cycles, "
            "cores=%d warps=%d threads=%d)",
            BLOCK_SIZE, timeout_cycles_, NUM_CORES, NUM_WARPS, NUM_THREADS);
    }

    ~Bridge() {
        this->cout_flush();
#if VM_TRACE
        if (tfp_) {
            tfp_->close();
            delete tfp_;
        }
#endif
        device_->final();
        delete device_;
        delete ctx_;
    }

    /* Called from the QEMU vCPU thread. */
    void apb_store(uint32_t offset, uint32_t value) {
        std::lock_guard<std::mutex> guard(lock_);
        shadow_[offset] = value;
        if (offset == REG_BASE_ADDR) {
            base_addr_ = value;
        }
        pending_.push_back({offset, value});
        log("apb write 0x%02x <= 0x%08x (queued)", offset, value);
    }

    /* Called from the QEMU vCPU thread. */
    uint32_t apb_load(uint32_t offset) {
        std::lock_guard<std::mutex> guard(lock_);
        /*
         * The busy and start registers are hardware-owned: busy reflects the
         * core, and start self-clears one cycle after the pulse. Everything
         * else is a plain holding register, so a read-back must return what the
         * driver last wrote even though the write is still queued for the next
         * launch.
         */
        if (offset != REG_VX_BUSY && offset != REG_START_VORTEX) {
            auto it = shadow_.find(offset);
            if (it != shadow_.end()) {
                return it->second;
            }
        }
        if (running_) {
            /* RTL is owned by the worker thread; report the launch as busy. */
            return (offset == REG_VX_BUSY) ? 1u : 0u;
        }
        /* prdata is purely combinational on paddr, so this reads the RTL. */
        uint32_t saved = device_->paddr;
        device_->paddr = offset;
        device_->eval();
        uint32_t value = device_->prdata;
        device_->paddr = saved;
        device_->eval();
        return value;
    }

    /* Called from the QEMU esp-vortex worker thread. */
    int run() {
        std::vector<ApbWrite> writes;
        {
            std::lock_guard<std::mutex> guard(lock_);
            writes.swap(pending_);
            running_ = true;
        }

        ctx_->assertOn(true);
        launch_started_ = false;

        for (const auto &w : writes) {
            this->apb_write(w.offset, w.value);
            if (w.offset == REG_START_VORTEX && (w.value & 1u)) {
                launch_started_ = true;
            }
        }

        int rc = 0;
        if (!launch_started_) {
            /*
             * CMD=1 without a start pulse: the wrapper FSM was never armed, so
             * nothing would ever raise busy_interrupt.
             */
            fprintf(stderr, "esp-vortex-bridge: launch with no start pulse at "
                            "0x%02x\n", REG_START_VORTEX);
            rc = -1;
        } else {
            uint64_t cycles = 0;
            bool done = false;
            while (cycles < timeout_cycles_) {
                this->tick();
                ++cycles;
                if (device_->busy_interrupt) {
                    done = true;
                    break;
                }
            }
            if (!done) {
                fprintf(stderr, "esp-vortex-bridge: watchdog expired after "
                                "%" PRIu64 " cycles\n", cycles);
                rc = -2;
            } else {
                log("launch complete after %" PRIu64 " cycles "
                    "(reads=%" PRIu64 ", writes=%" PRIu64 ")",
                    cycles, rd_count_, wr_count_);
            }
        }

        this->cout_flush();
        ctx_->assertOn(false);

        {
            std::lock_guard<std::mutex> guard(lock_);
            running_ = false;
        }
        return rc;
    }

    void reset() {
        std::lock_guard<std::mutex> guard(lock_);
        if (running_) {
            return;
        }
        pending_.clear();
        shadow_.clear();
        base_addr_ = 0;
        this->hard_reset();
    }

private:
    struct ApbWrite {
        uint32_t offset;
        uint32_t value;
    };

    void log(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (!verbose_) {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "esp-vortex-bridge: ");
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    void eval() {
        device_->eval();
#if VM_TRACE
        if (tfp_) {
            tfp_->dump(ctx_->time());
        }
#endif
        ctx_->timeInc(1);
    }

    void tick() {
        device_->clk = 0;
        this->eval();
        this->axi_eval(false);

        device_->clk = 1;
        this->eval();
        this->axi_eval(true);
    }

    /* GT_VORTEX_wrapper's ESP socket reset is active low. */
    void hard_reset() {
        running_        = false;
        rd_rsp_active_  = false;
        wr_rsp_active_  = false;
        rd_rsp_ready_   = false;
        wr_rsp_ready_   = false;
        rd_rsp_q_.clear();
        wr_rsp_q_.clear();
        print_bufs_.clear();
        rd_count_ = 0;
        wr_count_ = 0;

        device_->psel    = 0;
        device_->penable = 0;
        device_->pwrite  = 0;
        device_->paddr   = 0;
        device_->pwdata  = 0;

        device_->m_axi_awready = 0;
        device_->m_axi_wready  = 0;
        device_->m_axi_arready = 0;
        device_->m_axi_rvalid  = 0;
        device_->m_axi_bvalid  = 0;
        device_->m_axi_rlast   = 0;
        device_->m_axi_rresp   = 0;
        device_->m_axi_bresp   = 0;

        device_->reset = 0;
        for (int i = 0; i < 2 * RESET_DELAY; ++i) {
            this->tick();
        }
        device_->reset = 1;
        /* Let the wrapper walk its power-on DCR initialization sequence. */
        for (int i = 0; i < 2 * RESET_DELAY; ++i) {
            this->tick();
        }
    }

    /* One APB write transaction: exactly one posedge with psel&penable&pwrite. */
    void apb_write(uint32_t offset, uint32_t value) {
        device_->psel    = 1;
        device_->penable = 0;
        device_->pwrite  = 1;
        device_->paddr   = offset;
        device_->pwdata  = value;
        this->tick();          /* setup phase */

        device_->penable = 1;
        this->tick();          /* access phase: wrapper samples the write */

        device_->psel    = 0;
        device_->penable = 0;
        device_->pwrite  = 0;
        this->tick();
        log("apb write 0x%02x <= 0x%08x (applied)", offset, value);
    }

    /* AXI slave: services the wrapper's memory master from guest RAM. */
    void axi_eval(bool clk) {
        if (!clk) {
            rd_rsp_ready_ = device_->m_axi_rready;
            wr_rsp_ready_ = device_->m_axi_bready;
            return;
        }

        /* read responses */
        if (rd_rsp_active_ && device_->m_axi_rvalid && rd_rsp_ready_) {
            rd_rsp_active_ = false;
        }
        if (!rd_rsp_active_) {
            if (!rd_rsp_q_.empty()) {
                const MemRsp &rsp = rd_rsp_q_.front();
                device_->m_axi_rvalid = 1;
                device_->m_axi_rid    = rsp.tag;
                device_->m_axi_rresp  = 0;
                device_->m_axi_rlast  = 1;
                memcpy(&device_->m_axi_rdata, rsp.data, BLOCK_SIZE);
                rd_rsp_q_.pop_front();
                rd_rsp_active_ = true;
            } else {
                device_->m_axi_rvalid = 0;
            }
        }

        /* write responses */
        if (wr_rsp_active_ && device_->m_axi_bvalid && wr_rsp_ready_) {
            wr_rsp_active_ = false;
        }
        if (!wr_rsp_active_) {
            if (!wr_rsp_q_.empty()) {
                device_->m_axi_bvalid = 1;
                device_->m_axi_bid    = wr_rsp_q_.front();
                device_->m_axi_bresp  = 0;
                wr_rsp_q_.pop_front();
                wr_rsp_active_ = true;
            } else {
                device_->m_axi_bvalid = 0;
            }
        }

        if (running_) {
            if (device_->m_axi_wvalid) {
                this->axi_do_write();
            } else if (device_->m_axi_arvalid) {
                this->axi_do_read();
            }
        }

        device_->m_axi_awready = running_;
        device_->m_axi_wready  = running_;
        device_->m_axi_arready = running_;
    }

    void axi_do_write() {
        uint64_t addr = device_->m_axi_awaddr;
        uint32_t tag  = device_->m_axi_awid;
        uint8_t data[BLOCK_SIZE];
        memcpy(data, &device_->m_axi_wdata, BLOCK_SIZE);
        uint64_t byteen = device_->m_axi_wstrb;

        /*
         * The wrapper adds the launch base to every raw Vortex address, so
         * recover the raw address before classifying the ESP IO window.
         */
        uint64_t raw = addr - base_addr_;
        if (raw >= uint64_t(IO_COUT_ADDR) &&
            raw < uint64_t(IO_COUT_ADDR) + IO_COUT_SIZE) {
            for (size_t i = 0; i < BLOCK_SIZE; ++i) {
                if ((byteen >> i) & 1u) {
                    this->cout_putc(int(i), char(data[i]));
                }
            }
        } else {
            /* Honour the byte strobes: write only the enabled bytes. */
            size_t i = 0;
            while (i < BLOCK_SIZE) {
                if (!((byteen >> i) & 1u)) {
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j < BLOCK_SIZE && ((byteen >> j) & 1u)) {
                    ++j;
                }
                if (write_cb_(opaque_, addr + i, data + i, j - i) != 0) {
                    fprintf(stderr, "esp-vortex-bridge: guest write failed at "
                                    "0x%" PRIx64 " (%zu bytes)\n",
                            addr + i, j - i);
                }
                i = j;
            }
            ++wr_count_;
        }
        wr_rsp_q_.push_back(tag);
    }

    void axi_do_read() {
        uint64_t addr = device_->m_axi_araddr;
        MemRsp rsp;
        rsp.tag = device_->m_axi_arid;
        memset(rsp.data, 0, BLOCK_SIZE);
        if (read_cb_(opaque_, addr, rsp.data, BLOCK_SIZE) != 0) {
            fprintf(stderr, "esp-vortex-bridge: guest read failed at "
                            "0x%" PRIx64 "\n", addr);
        }
        ++rd_count_;
        rd_rsp_q_.push_back(rsp);
    }

    void cout_putc(int lane, char c) {
        auto &buf = print_bufs_[lane];
        buf << c;
        if (c == '\n') {
            fprintf(stdout, "#%d: %s", lane, buf.str().c_str());
            fflush(stdout);
            buf.str("");
        }
    }

    void cout_flush() {
        for (auto &entry : print_bufs_) {
            std::string s = entry.second.str();
            if (!s.empty()) {
                fprintf(stdout, "#%d: %s\n", entry.first, s.c_str());
            }
        }
        fflush(stdout);
        print_bufs_.clear();
    }

    esp_vortex_mem_read_fn  read_cb_;
    esp_vortex_mem_write_fn write_cb_;
    void                   *opaque_;

    VerilatedContext     *ctx_    = nullptr;
    VGT_VORTEX_wrapper   *device_ = nullptr;
#if VM_TRACE
    VerilatedVcdC        *tfp_    = nullptr;
#endif

    std::mutex             lock_;
    std::vector<ApbWrite>  pending_;
    std::map<uint32_t, uint32_t> shadow_;
    uint32_t               base_addr_ = 0;
    bool                   running_ = false;
    bool                   launch_started_ = false;
    bool                   verbose_ = false;
    uint64_t               timeout_cycles_ = DEFAULT_TIMEOUT_CYCLES;

    std::deque<MemRsp>     rd_rsp_q_;
    std::deque<uint32_t>   wr_rsp_q_;
    bool                   rd_rsp_active_ = false;
    bool                   wr_rsp_active_ = false;
    bool                   rd_rsp_ready_  = false;
    bool                   wr_rsp_ready_  = false;
    uint64_t               rd_count_ = 0;
    uint64_t               wr_count_ = 0;

    std::map<int, std::stringstream> print_bufs_;
};

} /* namespace */

/* ------------------------------------------------------------------ */
/* C ABI                                                               */
/* ------------------------------------------------------------------ */

extern "C" {

void *esp_vortex_bridge_create(esp_vortex_mem_read_fn read_cb,
                               esp_vortex_mem_write_fn write_cb,
                               void *opaque) {
    if (!read_cb || !write_cb) {
        return nullptr;
    }
    try {
        return new Bridge(read_cb, write_cb, opaque);
    } catch (const std::exception &e) {
        fprintf(stderr, "esp-vortex-bridge: create failed: %s\n", e.what());
        return nullptr;
    }
}

void esp_vortex_bridge_destroy(void *bridge) {
    delete static_cast<Bridge *>(bridge);
}

void esp_vortex_bridge_reset(void *bridge) {
    if (bridge) {
        static_cast<Bridge *>(bridge)->reset();
    }
}

uint32_t esp_vortex_bridge_read(void *bridge, uint32_t offset) {
    return bridge ? static_cast<Bridge *>(bridge)->apb_load(offset) : 0;
}

void esp_vortex_bridge_write(void *bridge, uint32_t offset, uint32_t value) {
    if (bridge) {
        static_cast<Bridge *>(bridge)->apb_store(offset, value);
    }
}

int esp_vortex_bridge_run(void *bridge) {
    return bridge ? static_cast<Bridge *>(bridge)->run() : -1;
}

} /* extern "C" */
