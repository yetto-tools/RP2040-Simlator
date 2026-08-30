// pio_assembler.cpp - see pio_assembler.h.
#include "pio/pio_assembler.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>

namespace rp2040 {

namespace {

// ---------------------------------------------------------------------------
// Error plumbing: a parse step throws AsmError, the top level turns it into a
// PioAssembly{ok=false}. Keeps the recursive-descent code readable.
// ---------------------------------------------------------------------------
struct AsmError {
    std::string message;
};

[[noreturn]] void die(const std::string& msg) { throw AsmError{msg}; }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Strip /* */ (possibly multi-line), then // and ; line comments.
std::string strip_comments(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') out.push_back('\n');  // keep line numbers stable
                ++i;
            }
            i = std::min(src.size(), i + 2);
            continue;
        }
        if ((src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') || src[i] == ';') {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        out.push_back(src[i++]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tokeniser for a single line. Tokens: identifiers, numbers, and the single
// punctuation characters , ( ) [ ] + - * ! ~ and the "::" operator.
// ---------------------------------------------------------------------------
std::vector<std::string> tokenise(const std::string& line) {
    std::vector<std::string> t;
    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == ':' && i + 1 < line.size() && line[i + 1] == ':') { t.emplace_back("::"); i += 2; continue; }
        if (c == '!' && i + 1 < line.size() && line[i + 1] == '=') { t.emplace_back("!="); i += 2; continue; }
        if (c == '-' && i + 1 < line.size() && line[i + 1] == '-') { t.emplace_back("--"); i += 2; continue; }
        if (std::string(",()[]+-*!~:").find(c) != std::string::npos) {
            t.emplace_back(1, c);
            ++i;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.') {
            std::size_t j = i;
            while (j < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_' ||
                    line[j] == '$' || line[j] == '.')) {
                ++j;
            }
            t.push_back(line.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == 'x' ||
                    line[j] == 'X' || line[j] == 'b' || line[j] == 'B')) {
                ++j;
            }
            t.push_back(line.substr(i, j - i));
            i = j;
            continue;
        }
        die(std::string("unexpected character '") + c + "'");
    }
    return t;
}

// ---------------------------------------------------------------------------
// Expression parser over a token cursor. Grammar (lowest to highest):
//   expr   := add
//   add    := mul (('+'|'-') mul)*
//   mul    := unary (('*') unary)*
//   unary  := ('-'|'~'|'::') unary | atom
//   atom   := number | symbol | '(' expr ')'
// Symbols resolve against the defines/labels map supplied by the caller.
// ---------------------------------------------------------------------------
class ExprEval {
public:
    ExprEval(const std::vector<std::string>& toks, std::size_t& pos,
             const std::map<std::string, std::int64_t>& syms)
        : t_(toks), pos_(pos), syms_(syms) {}

    std::int64_t parse() { return add(); }

private:
    const std::string& peek() const {
        static const std::string kEnd;
        return pos_ < t_.size() ? t_[pos_] : kEnd;
    }
    std::string take() { return pos_ < t_.size() ? t_[pos_++] : std::string(); }

    std::int64_t add() {
        std::int64_t v = mul();
        while (peek() == "+" || peek() == "-") {
            const bool minus = take() == "-";
            const std::int64_t r = mul();
            v = minus ? v - r : v + r;
        }
        return v;
    }
    std::int64_t mul() {
        std::int64_t v = unary();
        while (peek() == "*") {
            take();
            v *= unary();
        }
        return v;
    }
    std::int64_t unary() {
        if (peek() == "-") { take(); return -unary(); }
        if (peek() == "~") { take(); return ~unary(); }
        if (peek() == "::") { take(); return bitrev32(unary()); }
        return atom();
    }
    std::int64_t atom() {
        if (peek() == "(") {
            take();
            const std::int64_t v = add();
            if (take() != ")") die("expected ')'");
            return v;
        }
        const std::string tok = take();
        if (tok.empty()) die("expected an expression");
        if (std::isdigit(static_cast<unsigned char>(tok[0]))) return parse_number(tok);
        const auto it = syms_.find(tok);
        if (it == syms_.end()) die("undefined symbol '" + tok + "'");
        return it->second;
    }

    static std::int64_t parse_number(const std::string& s) {
        try {
            if (s.size() > 2 && (s[1] == 'x' || s[1] == 'X'))
                return static_cast<std::int64_t>(std::stoll(s.substr(2), nullptr, 16));
            if (s.size() > 2 && (s[1] == 'b' || s[1] == 'B'))
                return static_cast<std::int64_t>(std::stoll(s.substr(2), nullptr, 2));
            return static_cast<std::int64_t>(std::stoll(s, nullptr, 10));
        } catch (...) {
            die("bad numeric literal '" + s + "'");
        }
        return 0;  // unreachable: die() does not return
    }
    static std::int64_t bitrev32(std::int64_t v) {
        std::uint32_t x = static_cast<std::uint32_t>(v), r = 0;
        for (int i = 0; i < 32; ++i) { r = (r << 1) | (x & 1u); x >>= 1; }
        return r;
    }

    const std::vector<std::string>& t_;
    std::size_t& pos_;
    const std::map<std::string, std::int64_t>& syms_;
};

// ---------------------------------------------------------------------------
// The assembler proper.
// ---------------------------------------------------------------------------
class Assembler {
public:
    PioAssembly run(std::string_view source) {
        const std::string clean = strip_comments(source);
        std::istringstream in(clean);
        std::string raw;
        while (std::getline(in, raw)) lines_.push_back(raw);

        try {
            pass1();
            pass2();
        } catch (const AsmError& e) {
            out_.ok = false;
            out_.error = "line " + std::to_string(cur_line_ + 1) + ": " + e.message;
            return out_;
        }
        out_.ok = true;
        return out_;
    }

private:
    // ---- shared helpers -------------------------------------------------
    // Split a source line into an optional label and the remaining tokens.
    struct Line {
        std::optional<std::string> label;
        bool label_public = false;
        std::vector<std::string> toks;  // mnemonic + operands, or a directive
    };

    Line split_line(const std::string& raw) {
        Line l;
        std::vector<std::string> toks = tokenise(raw);
        // "label:"  -> toks: [name, ':']  ; also "PUBLIC name :"
        std::size_t start = 0;
        bool is_public = false;
        if (toks.size() >= 2 && lower(toks[0]) == "public") { is_public = true; start = 1; }
        if (toks.size() >= start + 2 && toks[start + 1] == ":" &&
            (std::isalpha(static_cast<unsigned char>(toks[start][0])) || toks[start][0] == '_')) {
            l.label = toks[start];
            l.label_public = is_public;
            start += 2;
        }
        l.toks.assign(toks.begin() + static_cast<std::ptrdiff_t>(start), toks.end());
        return l;
    }

    static bool is_directive(const std::vector<std::string>& toks) {
        return !toks.empty() && !toks[0].empty() && toks[0][0] == '.';
    }

    std::int64_t eval(const std::vector<std::string>& toks, std::size_t& pos) {
        ExprEval e(toks, pos, symbols_);
        return e.parse();
    }

    // ---- pass 1: labels, defines, layout directives -------------------
    void pass1() {
        unsigned pc = 0;
        bool seen_program = false;
        for (cur_line_ = 0; cur_line_ < lines_.size(); ++cur_line_) {
            Line l = split_line(lines_[cur_line_]);
            if (l.label) {
                if (symbols_.count(*l.label) || labels_.count(*l.label))
                    die("duplicate label '" + *l.label + "'");
                labels_[*l.label] = pc;
                symbols_[*l.label] = pc;
                if (l.label_public) out_.public_labels[*l.label] = pc;
            }
            if (l.toks.empty()) continue;

            if (is_directive(l.toks)) {
                handle_directive(l.toks, pc, seen_program);
                continue;
            }
            ++pc;  // an instruction
        }
        instr_count_ = pc;
        if (pc == 0) die("program contains no instructions");

        if (!wrap_seen_) out_.wrap = pc - 1;
        if (!wrap_target_seen_) out_.wrap_target = 0;
    }

    void handle_directive(const std::vector<std::string>& toks, unsigned pc, bool& seen_program) {
        const std::string d = lower(toks[0]);
        if (d == ".program") {
            if (seen_program) die(".program already given (one program per unit supported)");
            if (toks.size() < 2) die(".program needs a name");
            out_.program_name = toks[1];
            seen_program = true;
        } else if (d == ".define") {
            std::size_t i = 1;
            if (i < toks.size() && lower(toks[i]) == "public") ++i;
            if (i + 1 >= toks.size()) die(".define needs a symbol and a value");
            const std::string name = toks[i++];
            std::size_t p = i;
            const std::int64_t v = eval(toks, p);
            symbols_[name] = v;
            if (lower(toks[1]) == "public") out_.public_defines[name] = v;
        } else if (d == ".origin") {
            std::size_t p = 1;
            out_.origin = static_cast<int>(eval(toks, p));
        } else if (d == ".side_set") {
            std::size_t p = 1;
            out_.side_set_count = static_cast<unsigned>(eval(toks, p));
            for (std::size_t k = p; k < toks.size(); ++k) {
                const std::string o = lower(toks[k]);
                if (o == "opt") out_.side_set_opt = true;
                else if (o == "pindirs") out_.side_set_pindirs = true;
                else die("unknown .side_set option '" + toks[k] + "'");
            }
            const unsigned total = out_.side_set_count + (out_.side_set_opt ? 1u : 0u);
            if (total > 5) die(".side_set uses more than 5 bits");
        } else if (d == ".wrap_target") {
            out_.wrap_target = pc;
            wrap_target_seen_ = true;
        } else if (d == ".wrap") {
            if (pc == 0) die(".wrap before any instruction");
            out_.wrap = pc - 1;
            wrap_seen_ = true;
        } else if (d == ".lang_opt" || d == ".pio_version" || d == ".clock_div" ||
                   d == ".fifo" || d == ".mov_status" || d == ".set" || d == ".out" ||
                   d == ".in") {
            // Accepted and ignored: they do not affect the binary program.
        } else {
            die("unknown directive '" + toks[0] + "'");
        }
    }

    // ---- pass 2: encode instructions ---------------------------------
    void pass2() {
        out_.instructions.clear();
        out_.instructions.reserve(instr_count_);
        for (cur_line_ = 0; cur_line_ < lines_.size(); ++cur_line_) {
            Line l = split_line(lines_[cur_line_]);
            if (l.toks.empty() || is_directive(l.toks)) continue;
            out_.instructions.push_back(encode(l.toks));
        }
    }

    // side-set / delay field, bits [12:8].
    std::uint16_t delay_sideset_field(std::optional<std::int64_t> side, std::int64_t delay) {
        const unsigned ss_bits = out_.side_set_count;
        const unsigned opt_bit = out_.side_set_opt ? 1u : 0u;
        const unsigned delay_bits = 5u - ss_bits - opt_bit;

        if (delay < 0 || delay >= (1 << delay_bits))
            die("delay out of range for the configured side-set width");

        std::uint16_t field = static_cast<std::uint16_t>(delay);

        if (out_.side_set_count == 0 && out_.side_set_opt == false) {
            if (side) die("'side' used but no .side_set was declared");
            return field;
        }
        if (side) {
            if (*side < 0 || *side >= (1 << ss_bits))
                die("side-set value out of range");
            field = static_cast<std::uint16_t>(field |
                    (static_cast<std::uint16_t>(*side) << delay_bits));
            if (out_.side_set_opt) field |= static_cast<std::uint16_t>(1u << 4);
        } else if (!out_.side_set_opt) {
            die("this program requires a 'side' on every instruction");
        }
        return field;
    }

    // Pull an optional "side <expr>" and trailing "[<expr>]" out of the operand
    // token list, returning the remaining core operand tokens.
    std::vector<std::string> extract_side_delay(const std::vector<std::string>& toks,
                                                std::optional<std::int64_t>& side,
                                                std::int64_t& delay) {
        std::vector<std::string> core;
        for (std::size_t i = 0; i < toks.size();) {
            if (lower(toks[i]) == "side") {
                std::size_t p = i + 1;
                side = eval(toks, p);
                i = p;
            } else if (toks[i] == "[") {
                std::size_t p = i + 1;
                delay = eval(toks, p);
                if (p >= toks.size() || toks[p] != "]") die("expected ']' after the delay");
                i = p + 1;
            } else {
                core.push_back(toks[i++]);
            }
        }
        return core;
    }

    std::uint16_t encode(const std::vector<std::string>& toks) {
        std::optional<std::int64_t> side;
        std::int64_t delay = 0;
        const std::vector<std::string> op = extract_side_delay(toks, side, delay);
        if (op.empty()) die("empty instruction");

        const std::string m = lower(op[0]);
        std::vector<std::string> a(op.begin() + 1, op.end());
        // Drop operand commas; they are pure separators.
        a.erase(std::remove(a.begin(), a.end(), ","), a.end());

        std::uint16_t body = 0;
        if (m == "nop") {
            body = 0xA000u | (kMovY << 5) | (kMovOpNone << 3) | kMovY;  // mov y, y
        } else if (m == "jmp") {
            body = static_cast<std::uint16_t>(0x0000u | enc_jmp(a));
        } else if (m == "wait") {
            body = static_cast<std::uint16_t>(0x2000u | enc_wait(a));
        } else if (m == "in") {
            body = static_cast<std::uint16_t>(0x4000u | enc_in_out(a, /*is_out=*/false));
        } else if (m == "out") {
            body = static_cast<std::uint16_t>(0x6000u | enc_in_out(a, /*is_out=*/true));
        } else if (m == "push") {
            body = static_cast<std::uint16_t>(0x8000u | enc_push_pull(a, /*is_pull=*/false));
        } else if (m == "pull") {
            body = static_cast<std::uint16_t>(0x8080u | enc_push_pull(a, /*is_pull=*/true));
        } else if (m == "mov") {
            body = static_cast<std::uint16_t>(0xA000u | enc_mov(a));
        } else if (m == "irq") {
            body = static_cast<std::uint16_t>(0xC000u | enc_irq(a));
        } else if (m == "set") {
            body = static_cast<std::uint16_t>(0xE000u | enc_set(a));
        } else {
            die("unknown mnemonic '" + op[0] + "'");
        }

        return static_cast<std::uint16_t>(body |
               (delay_sideset_field(side, delay) << 8));
    }

    // ---- per-instruction operand encoders ----------------------------
    std::int64_t eval_all(const std::vector<std::string>& a, std::size_t from) {
        std::size_t p = from;
        const std::int64_t v = eval(a, p);
        if (p != a.size()) die("trailing tokens after the operand");
        return v;
    }

    std::uint16_t enc_jmp(const std::vector<std::string>& a) {
        if (a.empty()) die("jmp needs a target");
        unsigned cond = 0;
        std::size_t ti = 0;
        const std::string t0 = lower(a.size() > 0 ? a[0] : "");
        const std::string t1 = lower(a.size() > 1 ? a[1] : "");
        const std::string t2 = lower(a.size() > 2 ? a[2] : "");
        if (t0 == "!" && t1 == "x") { cond = 1; ti = 2; }
        else if (t0 == "x" && t1 == "--") { cond = 2; ti = 2; }
        else if (t0 == "!" && t1 == "y") { cond = 3; ti = 2; }
        else if (t0 == "y" && t1 == "--") { cond = 4; ti = 2; }
        else if (t0 == "x" && t1 == "!=" && t2 == "y") { cond = 5; ti = 3; }
        else if (t0 == "pin") { cond = 6; ti = 1; }
        else if (t0 == "!" && t1 == "osre") { cond = 7; ti = 2; }
        const std::int64_t target = eval_all(a, ti);
        if (target < 0 || target > 31) die("jmp target out of range 0..31");
        return static_cast<std::uint16_t>((cond << 5) | static_cast<unsigned>(target));
    }

    std::uint16_t enc_wait(const std::vector<std::string>& a) {
        if (a.size() < 3) die("wait needs: <polarity> <gpio|pin|irq> <index> [rel]");
        std::size_t p = 0;
        const std::int64_t pol = eval(a, p);
        if (pol != 0 && pol != 1) die("wait polarity must be 0 or 1");
        const std::string src = lower(a[p++]);
        unsigned src_code = 0;
        if (src == "gpio") src_code = 0;
        else if (src == "pin") src_code = 1;
        else if (src == "irq") src_code = 2;
        else die("wait source must be gpio, pin or irq");
        std::int64_t index = eval(a, p);
        if (src_code == 2 && p < a.size() && lower(a[p]) == "rel") { index |= 0x10; ++p; }
        if (p != a.size()) die("trailing tokens after wait");
        if (index < 0 || index > 31) die("wait index out of range");
        return static_cast<std::uint16_t>((static_cast<unsigned>(pol) << 7) | (src_code << 5) |
                                          static_cast<unsigned>(index));
    }

    std::uint16_t enc_in_out(const std::vector<std::string>& a, bool is_out) {
        if (a.size() < 2) die((is_out ? "out" : "in") + std::string(" needs <target>, <count>"));
        const std::string s = lower(a[0]);
        unsigned code = 0;
        if (is_out) {
            static const std::map<std::string, unsigned> kDst = {
                {"pins", 0}, {"x", 1}, {"y", 2}, {"null", 3},
                {"pindirs", 4}, {"pc", 5}, {"isr", 6}, {"exec", 7}};
            const auto it = kDst.find(s);
            if (it == kDst.end()) die("bad OUT destination '" + a[0] + "'");
            code = it->second;
        } else {
            static const std::map<std::string, unsigned> kSrc = {
                {"pins", 0}, {"x", 1}, {"y", 2}, {"null", 3}, {"isr", 6}, {"osr", 7}};
            const auto it = kSrc.find(s);
            if (it == kSrc.end()) die("bad IN source '" + a[0] + "'");
            code = it->second;
        }
        const std::int64_t count = eval_all(a, 1);
        if (count < 1 || count > 32) die("bit count must be 1..32");
        const unsigned enc = (count == 32) ? 0u : static_cast<unsigned>(count);
        return static_cast<std::uint16_t>((code << 5) | enc);
    }

    std::uint16_t enc_push_pull(const std::vector<std::string>& a, bool is_pull) {
        bool block = true;
        bool cond = false;  // iffull / ifempty
        for (const auto& tok : a) {
            const std::string o = lower(tok);
            if (o == "block") block = true;
            else if (o == "noblock") block = false;
            else if (!is_pull && o == "iffull") cond = true;
            else if (is_pull && o == "ifempty") cond = true;
            else die("unexpected '" + tok + "' after " + (is_pull ? "pull" : "push"));
        }
        return static_cast<std::uint16_t>((cond ? 1u << 6 : 0u) | (block ? 1u << 5 : 0u));
    }

    std::uint16_t enc_mov(const std::vector<std::string>& a) {
        if (a.size() < 2) die("mov needs <dest>, [op] <src>");
        static const std::map<std::string, unsigned> kDst = {
            {"pins", 0}, {"x", 1}, {"y", 2}, {"exec", 4}, {"pc", 5}, {"isr", 6}, {"osr", 7}};
        static const std::map<std::string, unsigned> kSrc = {
            {"pins", 0}, {"x", 1}, {"y", 2}, {"null", 3}, {"status", 5}, {"isr", 6}, {"osr", 7}};
        const auto dit = kDst.find(lower(a[0]));
        if (dit == kDst.end()) die("bad MOV destination '" + a[0] + "'");

        std::size_t i = 1;
        unsigned mov_op = kMovOpNone;
        if (lower(a[i]) == "!" || lower(a[i]) == "~") { mov_op = 1; ++i; }
        else if (a[i] == "::") { mov_op = 2; ++i; }
        if (i >= a.size()) die("mov is missing a source");
        const auto sit = kSrc.find(lower(a[i]));
        if (sit == kSrc.end()) die("bad MOV source '" + a[i] + "'");
        if (i + 1 != a.size()) die("trailing tokens after the MOV source");

        return static_cast<std::uint16_t>((dit->second << 5) | (mov_op << 3) | sit->second);
    }

    std::uint16_t enc_irq(const std::vector<std::string>& a) {
        if (a.empty()) die("irq needs an index");
        bool clear = false;
        bool wait = false;
        std::size_t i = 0;
        const std::string mode = lower(a[0]);
        if (mode == "set") { i = 1; }
        else if (mode == "nowait") { i = 1; }
        else if (mode == "wait") { wait = true; i = 1; }
        else if (mode == "clear") { clear = true; i = 1; }
        std::int64_t index = eval(a, i);
        if (i < a.size() && lower(a[i]) == "rel") { index |= 0x10; ++i; }
        if (i != a.size()) die("trailing tokens after irq");
        if (index < 0 || index > 31) die("irq index out of range");
        return static_cast<std::uint16_t>((clear ? 1u << 6 : 0u) | (wait ? 1u << 5 : 0u) |
                                          static_cast<unsigned>(index));
    }

    std::uint16_t enc_set(const std::vector<std::string>& a) {
        if (a.size() < 2) die("set needs <dest>, <value>");
        static const std::map<std::string, unsigned> kDst = {
            {"pins", 0}, {"x", 1}, {"y", 2}, {"pindirs", 4}};
        const auto it = kDst.find(lower(a[0]));
        if (it == kDst.end()) die("bad SET destination '" + a[0] + "'");
        const std::int64_t v = eval_all(a, 1);
        if (v < 0 || v > 31) die("set value must be 0..31");
        return static_cast<std::uint16_t>((it->second << 5) | static_cast<unsigned>(v));
    }

    // MOV register / op codes used by nop and enc_mov.
    static constexpr unsigned kMovY = 2;
    static constexpr unsigned kMovOpNone = 0;

    std::vector<std::string> lines_;
    std::size_t cur_line_ = 0;
    std::map<std::string, std::int64_t> symbols_;  // defines + labels
    std::map<std::string, unsigned> labels_;
    unsigned instr_count_ = 0;
    bool wrap_seen_ = false;
    bool wrap_target_seen_ = false;
    PioAssembly out_;
};

}  // namespace

PioAssembly assemble_pio(std::string_view source) {
    return Assembler().run(source);
}

}  // namespace rp2040
