// main.cpp - rp2040-lab-server: a local HTTP/JSON server exposing the
// RP2040 simulator (DebugSession -> Simulator) to the browser-based
// "virtual lab" frontend (web/). See BACKLOG.md for the phase this
// belongs to and ARCHITECTURE.md for why this lives outside rp2040_core.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "compiler.h"
#include "debug_session.h"
#include "vendor/httplib.h"
#include "vendor/json.hpp"

using json = nlohmann::json;

namespace {

// --- base64 (RFC 4648, standard alphabet) ---------------------------------

const char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<std::uint8_t>& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                 (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out += kB64Chars[(n >> 18) & 0x3F];
        out += kB64Chars[(n >> 12) & 0x3F];
        out += kB64Chars[(n >> 6) & 0x3F];
        out += kB64Chars[n & 0x3F];
        i += 3;
    }
    const std::size_t rem = data.size() - i;
    if (rem == 1) {
        const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        out += kB64Chars[(n >> 18) & 0x3F];
        out += kB64Chars[(n >> 12) & 0x3F];
        out += "==";
    } else if (rem == 2) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                 (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out += kB64Chars[(n >> 18) & 0x3F];
        out += kB64Chars[(n >> 12) & 0x3F];
        out += kB64Chars[(n >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<std::uint8_t> base64_decode(const std::string& s) {
    std::vector<std::uint8_t> out;
    out.reserve(s.size() / 4 * 3);
    int vals[4];
    int n = 0;
    for (char c : s) {
        const int v = b64_val(c);
        if (v < 0) continue;  // skip '=', whitespace, anything non-alphabet
        vals[n++] = v;
        if (n == 4) {
            out.push_back(static_cast<std::uint8_t>((vals[0] << 2) | (vals[1] >> 4)));
            out.push_back(static_cast<std::uint8_t>((vals[1] << 4) | (vals[2] >> 2)));
            out.push_back(static_cast<std::uint8_t>((vals[2] << 6) | vals[3]));
            n = 0;
        }
    }
    if (n >= 2) out.push_back(static_cast<std::uint8_t>((vals[0] << 2) | (vals[1] >> 4)));
    if (n >= 3) out.push_back(static_cast<std::uint8_t>((vals[1] << 4) | (vals[2] >> 2)));
    return out;
}

// --- state serialization ---------------------------------------------------

json snapshot_to_json(const rp2040lab::StateSnapshot& s) {
    json j;
    j["loaded"] = s.loaded;
    j["status"] = rp2040lab::to_string(s.status);
    j["faultReason"] = s.fault_reason;
    j["pc"] = s.pc;
    j["sp"] = s.sp;
    j["lr"] = s.lr;
    j["xpsr"] = s.xpsr;
    j["cycles"] = s.cycles;
    j["r"] = s.r;
    j["uart0"] = s.uart0_out;
    j["uart1"] = s.uart1_out;
    json gpio = json::array();
    for (const auto& p : s.gpio) {
        gpio.push_back({{"pin", p.pin}, {"level", p.level}, {"driving", p.driving},
                         {"funcsel", p.funcsel}});
    }
    j["gpio"] = gpio;
    return j;
}

void send_json(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void send_error(httplib::Response& res, const std::string& message, int status = 400) {
    send_json(res, {{"error", message}}, status);
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8787;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = std::atoi(argv[++i]);
    }

#if defined(RP2040LAB_ARM_GCC)
    rp2040lab::configure_compiler(RP2040LAB_ARM_GCC, RP2040LAB_LINKER_SCRIPT);
#endif
    if (!rp2040lab::compiler_available()) {
        std::printf(
            "warning: arm-none-eabi-gcc not found at build time - /compile will report an "
            "error; /load (upload a pre-built .elf/.uf2) still works.\n");
    }

#if defined(RP2040LAB_PICO_SDK_PATH)
    rp2040lab::configure_pico_sdk(RP2040LAB_PICO_SDK_PATH, RP2040LAB_PICO_CMAKE, RP2040LAB_PICO_NINJA,
                                   RP2040LAB_PICO_TOOLCHAIN_DIR);
#endif
    if (!rp2040lab::pico_sdk_available()) {
        std::printf(
            "warning: pico-sdk not found at build time - /compile with mode=pico_sdk will "
            "report an error; freestanding /compile still works.\n");
    }

    rp2040lab::DebugSession session;
    httplib::Server svr;

    // Access-Control-Allow-Origin comes from set_default_headers alone -
    // every response gets it exactly once, including preflights. Setting it
    // again in the Options handler below produced a duplicated header on
    // preflight responses, which Chrome treats as a CORS failure and blocks
    // silently (the POST still went out and is visible in the network log,
    // but fetch() rejects with a bare "Failed to fetch" - no server-side
    // error to point at).
    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"}});
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"ok", true}});
    });

    svr.Post("/compile", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        if (!body.contains("files") || !body.at("files").is_array() || body.at("files").empty()) {
            return send_error(res, "missing or empty 'files'");
        }
        std::vector<rp2040lab::SourceFile> files;
        for (const auto& jf : body.at("files")) {
            files.push_back({jf.value("name", ""), jf.value("content", "")});
        }
        const std::string mode = body.value("mode", "freestanding");

        const rp2040lab::CompileResult r = (mode == "pico_sdk")
                                                ? rp2040lab::compile_pico_sdk_firmware(files)
                                                : rp2040lab::compile_firmware(files);
        json out;
        out["ok"] = r.ok;
        out["log"] = r.log;
        if (r.ok) {
            out["elfBase64"] = base64_encode(r.elf);
            json line_map = json::array();
            for (const auto& la : r.line_map) {
                line_map.push_back({{"file", la.file}, {"line", la.line}, {"addr", la.addr}});
            }
            out["lineMap"] = line_map;
        }
        send_json(res, out);
    });

    svr.Post("/load", [&session](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        const std::string b64 = body.value("bytesBase64", "");
        if (b64.empty()) return send_error(res, "missing 'bytesBase64'");
        const std::string kind = body.value("kind", "elf");
        const bool from_entry = body.value("fromEntry", kind == "elf");

        const std::vector<std::uint8_t> bytes = base64_decode(b64);
        std::string error;
        if (!session.load(bytes, kind, from_entry, error)) return send_error(res, error);
        send_json(res, snapshot_to_json(session.snapshot()));
    });

    svr.Post("/run", [&session](const httplib::Request&, httplib::Response& res) {
        session.start_run();
        send_json(res, {{"ok", true}});
    });
    svr.Post("/pause", [&session](const httplib::Request&, httplib::Response& res) {
        session.pause();
        send_json(res, {{"ok", true}});
    });
    svr.Post("/step", [&session](const httplib::Request&, httplib::Response& res) {
        session.step();
        send_json(res, snapshot_to_json(session.snapshot()));
    });

    svr.Get("/state", [&session](const httplib::Request&, httplib::Response& res) {
        send_json(res, snapshot_to_json(session.snapshot()));
    });

    svr.Post("/breakpoints", [&session](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        const std::string action = body.value("action", "");
        if (!body.contains("addr")) return send_error(res, "missing 'addr'");
        const auto addr = body.at("addr").get<std::uint32_t>();
        if (action == "add") session.add_breakpoint(addr);
        else if (action == "remove") session.remove_breakpoint(addr);
        else return send_error(res, "'action' must be 'add' or 'remove'");
        json out;
        out["breakpoints"] = session.breakpoints();
        send_json(res, out);
    });

    svr.Post(R"(/gpio/(\d+)/external)",
             [&session](const httplib::Request& req, httplib::Response& res) {
                 const unsigned pin = static_cast<unsigned>(std::stoul(req.matches[1]));
                 json body;
                 try {
                     body = json::parse(req.body);
                 } catch (const std::exception&) {
                     return send_error(res, "invalid JSON body");
                 }
                 if (body.contains("clear") && body.at("clear").get<bool>()) {
                     session.clear_gpio_external(pin);
                 } else {
                     session.set_gpio_external(pin, body.value("level", false));
                 }
                 send_json(res, {{"ok", true}});
             });

    svr.Post(R"(/adc/(\d+)/external)",
             [&session](const httplib::Request& req, httplib::Response& res) {
                 const unsigned channel = static_cast<unsigned>(std::stoul(req.matches[1]));
                 json body;
                 try {
                     body = json::parse(req.body);
                 } catch (const std::exception&) {
                     return send_error(res, "invalid JSON body");
                 }
                 const int raw = body.value("raw12", 0);
                 const auto clamped = static_cast<std::uint16_t>(std::clamp(raw, 0, 4095));
                 session.set_adc_external(channel, clamped);
                 send_json(res, {{"ok", true}});
             });

    svr.Post("/st7789/attach", [&session](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        const int spi = body.value("spi", -1);
        const int cs = body.value("cs", -1);
        const int dc = body.value("dc", -1);
        if (spi < 0 || cs < 0 || dc < 0) return send_error(res, "missing 'spi', 'cs', or 'dc'");
        std::string error;
        if (!session.attach_st7789(static_cast<unsigned>(spi), static_cast<unsigned>(cs),
                                    static_cast<unsigned>(dc), error)) {
            return send_error(res, error);
        }
        send_json(res, {{"ok", true}});
    });

    svr.Post("/st7789/detach", [&session](const httplib::Request&, httplib::Response& res) {
        session.detach_st7789();
        send_json(res, {{"ok", true}});
    });

    svr.Get("/st7789/framebuffer", [&session](const httplib::Request&, httplib::Response& res) {
        json out;
        out["rgb565Base64"] = base64_encode(session.st7789_framebuffer());
        send_json(res, out);
    });

    svr.Post("/ili9341/attach", [&session](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        const int spi = body.value("spi", -1);
        const int cs = body.value("cs", -1);
        const int dc = body.value("dc", -1);
        if (spi < 0 || cs < 0 || dc < 0) return send_error(res, "missing 'spi', 'cs', or 'dc'");
        std::string error;
        if (!session.attach_ili9341(static_cast<unsigned>(spi), static_cast<unsigned>(cs),
                                     static_cast<unsigned>(dc), error)) {
            return send_error(res, error);
        }
        send_json(res, {{"ok", true}});
    });

    svr.Post("/ili9341/detach", [&session](const httplib::Request&, httplib::Response& res) {
        session.detach_ili9341();
        send_json(res, {{"ok", true}});
    });

    svr.Get("/ili9341/framebuffer", [&session](const httplib::Request&, httplib::Response& res) {
        json out;
        out["rgb565Base64"] = base64_encode(session.ili9341_framebuffer());
        send_json(res, out);
    });

    svr.Post("/ssd1306/attach", [&session](const httplib::Request& req, httplib::Response& res) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        const int i2c = body.value("i2c", -1);
        const int addr = body.value("addr", 0x3C);
        if (i2c < 0) return send_error(res, "missing 'i2c'");
        std::string error;
        if (!session.attach_ssd1306(static_cast<unsigned>(i2c), static_cast<std::uint8_t>(addr), error)) {
            return send_error(res, error);
        }
        send_json(res, {{"ok", true}});
    });

    svr.Post("/ssd1306/detach", [&session](const httplib::Request&, httplib::Response& res) {
        session.detach_ssd1306();
        send_json(res, {{"ok", true}});
    });

    svr.Get("/ssd1306/framebuffer", [&session](const httplib::Request&, httplib::Response& res) {
        json out;
        out["gddramBase64"] = base64_encode(session.ssd1306_gddram());
        send_json(res, out);
    });

    svr.Post(R"(/uart/(\d+)/feed)", [&session](const httplib::Request& req, httplib::Response& res) {
        const unsigned n = static_cast<unsigned>(std::stoul(req.matches[1]));
        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception&) {
            return send_error(res, "invalid JSON body");
        }
        session.feed_uart(n, body.value("text", ""));
        send_json(res, {{"ok", true}});
    });

    std::printf("rp2040-lab-server listening on http://localhost:%d\n", port);
    if (!svr.listen("0.0.0.0", port)) {
        std::fprintf(stderr, "failed to bind port %d\n", port);
        return 1;
    }
    return 0;
}
