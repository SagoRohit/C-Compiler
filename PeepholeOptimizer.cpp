#include "PeepholeOptimizer.h"
#include <regex>
#include <unordered_map>
#include <cctype>

namespace {

std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

enum class Kind { BLANK, COMMENT, LABEL, INSTR };

struct Rec {
    std::string indent;     // leading whitespace of the original line
    std::string trimmed;    // original content with leading/trailing whitespace stripped
    Kind kind;
    std::string label;      // for LABEL: the label name (without ':')
    std::string mnemonic;   // for INSTR: uppercased opcode
    std::string operands;   // for INSTR: raw operand text (unchanged case)
    bool operandsChanged = false;
    bool removed = false;
};

std::string upperStr(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

Kind classify(const std::string& t, std::string& labelOut) {
    if (t.empty()) return Kind::BLANK;
    if (t[0] == ';') return Kind::COMMENT;
    static const std::regex labelRe("^[A-Za-z_.][A-Za-z0-9_.]*:$");
    if (std::regex_match(t, labelRe)) {
        labelOut = t.substr(0, t.size() - 1);
        return Kind::LABEL;
    }
    return Kind::INSTR;
}

void splitInstr(const std::string& t, std::string& mnemonic, std::string& operands) {
    size_t sp = t.find_first_of(" \t");
    if (sp == std::string::npos) {
        mnemonic = upperStr(t);
        operands = "";
    } else {
        mnemonic = upperStr(t.substr(0, sp));
        operands = trimStr(t.substr(sp + 1));
    }
}

// Normalize operand text for equality comparisons (case/space-insensitive).
std::string normOperands(const std::string& s) {
    std::string out;
    bool lastSpace = false;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) {
            if (!out.empty()) lastSpace = true;
        } else {
            if (lastSpace) { out += ' '; lastSpace = false; }
            out += (char)std::toupper((unsigned char)c);
        }
    }
    return out;
}

int nextActive(const std::vector<Rec>& recs, int from) {
    for (size_t i = from; i < recs.size(); i++) {
        if (recs[i].removed) continue;
        if (recs[i].kind == Kind::BLANK || recs[i].kind == Kind::COMMENT) continue;
        return (int)i;
    }
    return -1;
}

} // namespace

std::vector<std::string> Peephole::optimize(const std::vector<std::string>& lines) {
    std::vector<Rec> recs;
    recs.reserve(lines.size());
    for (auto& raw : lines) {
        Rec r;
        size_t a = raw.find_first_not_of(" \t");
        r.indent = (a == std::string::npos) ? raw : raw.substr(0, a);
        r.trimmed = trimStr(raw);
        std::string lbl;
        r.kind = classify(r.trimmed, lbl);
        if (r.kind == Kind::LABEL) r.label = lbl;
        if (r.kind == Kind::INSTR) splitInstr(r.trimmed, r.mnemonic, r.operands);
        recs.push_back(std::move(r));
    }

    // ---- Pass 1: collapse runs of consecutive labels ----
    std::unordered_map<std::string, std::string> rename;
    {
        size_t i = 0;
        while (i < recs.size()) {
            if (recs[i].kind != Kind::LABEL) { i++; continue; }
            std::vector<size_t> labelIdx = {i};
            size_t k = i + 1;
            while (k < recs.size() &&
                   (recs[k].kind == Kind::BLANK || recs[k].kind == Kind::COMMENT || recs[k].kind == Kind::LABEL)) {
                if (recs[k].kind == Kind::LABEL) labelIdx.push_back(k);
                k++;
            }
            if (labelIdx.size() > 1) {
                const std::string canonical = recs[labelIdx[0]].label;
                for (size_t m = 1; m < labelIdx.size(); m++) {
                    rename[recs[labelIdx[m]].label] = canonical;
                    recs[labelIdx[m]].removed = true;
                }
            }
            i = labelIdx.back() + 1;
        }
    }
    if (!rename.empty()) {
        for (auto& kv : rename) {
            std::string target = kv.second;
            int guard = 0;
            while (rename.count(target) && guard++ < 100) target = rename[target];
            kv.second = target;
        }
        for (auto& r : recs) {
            if (r.kind != Kind::INSTR) continue;
            bool isJumpOrCall = (!r.mnemonic.empty() && r.mnemonic[0] == 'J') || r.mnemonic == "CALL";
            if (!isJumpOrCall) continue;
            std::string target = trimStr(r.operands);
            auto it = rename.find(target);
            if (it != rename.end()) {
                r.operands = it->second;
                r.operandsChanged = true;
            }
        }
    }

    // ---- Pass 2: local redundancy removal, iterated to a fixed point ----
    bool changed = true;
    int guardIter = 0;
    while (changed && guardIter++ < 10) {
        changed = false;

        int i = nextActive(recs, 0);
        while (i != -1) {
            int j = nextActive(recs, i + 1);
            if (j == -1) break;
            Rec& a = recs[i];
            Rec& b = recs[j];
            bool consumed = false;

            if (a.kind == Kind::INSTR && b.kind == Kind::INSTR) {
                // (ii) PUSH x / POP x
                if (a.mnemonic == "PUSH" && b.mnemonic == "POP" &&
                    normOperands(a.operands) == normOperands(b.operands)) {
                    a.removed = b.removed = true;
                    changed = true;
                    consumed = true;
                }
                // (i) MOV a,b / MOV b,a
                else if (a.mnemonic == "MOV" && b.mnemonic == "MOV") {
                    size_t pa = a.operands.find(',');
                    size_t pb = b.operands.find(',');
                    if (pa != std::string::npos && pb != std::string::npos) {
                        std::string aL = normOperands(a.operands.substr(0, pa));
                        std::string aR = normOperands(a.operands.substr(pa + 1));
                        std::string bL = normOperands(b.operands.substr(0, pb));
                        std::string bR = normOperands(b.operands.substr(pb + 1));
                        if (aL == bR && aR == bL) {
                            b.removed = true;
                            changed = true;
                        }
                    }
                }
                // MOV reg,1 ; MUL reg / IMUL reg -> multiply by 1 is a no-op
                else if (a.mnemonic == "MOV" && (b.mnemonic == "MUL" || b.mnemonic == "IMUL")) {
                    size_t p = a.operands.find(',');
                    if (p != std::string::npos) {
                        std::string dst = normOperands(a.operands.substr(0, p));
                        std::string val = normOperands(a.operands.substr(p + 1));
                        if (val == "1" && dst == normOperands(b.operands)) {
                            a.removed = b.removed = true;
                            changed = true;
                            consumed = true;
                        }
                    }
                }
            }
            i = consumed ? nextActive(recs, j + 1) : j;
        }

        // (iii) ADD/SUB reg,0 -- a standalone no-op, independent of neighbors
        for (auto& r : recs) {
            if (r.removed || r.kind != Kind::INSTR) continue;
            if (r.mnemonic == "ADD" || r.mnemonic == "SUB") {
                size_t p = r.operands.find(',');
                if (p != std::string::npos && normOperands(r.operands.substr(p + 1)) == "0") {
                    r.removed = true;
                    changed = true;
                }
            }
        }
    }

    std::vector<std::string> out;
    out.reserve(recs.size());
    for (auto& r : recs) {
        if (r.removed) continue;
        if (r.kind == Kind::LABEL) {
            out.push_back(r.indent + r.label + ":");
        } else if (r.kind == Kind::INSTR && r.operandsChanged) {
            out.push_back(r.indent + r.mnemonic + (r.operands.empty() ? "" : (" " + r.operands)));
        } else {
            out.push_back(r.indent + r.trimmed);
        }
    }
    return out;
}
