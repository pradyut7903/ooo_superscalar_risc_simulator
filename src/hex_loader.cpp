#include "hex_loader.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

std::vector<uint32_t> loadHexWords(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open hex file: " + path);
    }

    std::vector<uint32_t> words;
    std::string line;
    size_t cursor = 0;  // word index for @addr (byte addr → word)
    while (std::getline(in, line)) {
        size_t b = 0;
        while (b < line.size() && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r')) {
            ++b;
        }
        size_t e = line.size();
        while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) {
            --e;
        }
        if (e <= b) continue;

        // Comments
        if (line[b] == '/' || line[b] == '#') continue;

        // $readmemh @byte_address
        if (line[b] == '@') {
            const unsigned long addr = std::stoul(line.substr(b + 1, e - (b + 1)), nullptr, 16);
            cursor = static_cast<size_t>(addr / 4);
            if (words.size() < cursor) {
                words.resize(cursor, 0);
            }
            continue;
        }

        std::string tok = line.substr(b, e - b);
        // Accept 8-digit RV32 words; if longer (toy 16-hex), take low 32 bits.
        unsigned long long v = std::stoull(tok, nullptr, 16);
        const uint32_t word = static_cast<uint32_t>(v & 0xFFFFFFFFull);
        if (cursor >= words.size()) {
            words.resize(cursor + 1, 0);
        }
        words[cursor] = word;
        ++cursor;
    }
    return words;
}
