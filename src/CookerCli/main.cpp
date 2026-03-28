/*
 * BO2 FastFile Cooker / Uncooker
 *
 * Cooker:   Takes an original .ff file (for header/block structure) and modified raw zone data,
 *           then compresses and encrypts using the same libraries as OAT/game.
 *
 * Uncooker: Takes an encrypted .ff file and produces the raw decompressed zone data.
 *
 * Usage:
 *   Cooker.exe cook   <original.ff> <modified_raw.bin> <output.ff>
 *   Cooker.exe uncook <input.ff> <output_raw.bin>
 */

#include "Game/T6/ZoneConstantsT6.h"
#include "Zone/XChunk/XChunkProcessorDeflate.h"
#include "Zone/XChunk/XChunkProcessorInflate.h"
#include "Zone/XChunk/XChunkProcessorSalsa20Decryption.h"
#include "Zone/XChunk/XChunkProcessorSalsa20Encryption.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace T6;

constexpr size_t HEADER_SIZE = 0x138;
constexpr unsigned STREAM_COUNT = ZoneConstants::STREAM_COUNT;
constexpr size_t XCHUNK_SIZE = ZoneConstants::XCHUNK_SIZE;

struct ChunkInfo
{
    size_t compressedSize;
    size_t decompressedSize;
};

bool ReadFile(const char* path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return false;
    auto size = f.tellg();
    f.seekg(0);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good();
}

bool WriteFile(const char* path, const std::vector<uint8_t>& data)
{
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return f.good();
}

std::string GetZoneName(const std::vector<uint8_t>& ffData)
{
    std::string zoneName(reinterpret_cast<const char*>(&ffData[0x18]), 32);
    return zoneName.c_str();
}

bool ParseBlockStructure(const std::vector<uint8_t>& ffData, std::vector<ChunkInfo>& chunks)
{
    if (ffData.size() < HEADER_SIZE + 4)
        return false;

    size_t offset = HEADER_SIZE;
    while (offset + 4 <= ffData.size())
    {
        uint32_t blockLen;
        memcpy(&blockLen, &ffData[offset], 4);
        if (blockLen == 0)
            break;

        ChunkInfo ci{};
        ci.compressedSize = blockLen;
        chunks.push_back(ci);
        offset += 4 + blockLen;
    }
    return true;
}

// ============ Uncook: decrypt + decompress -> raw ============

int Uncook(const char* inputPath, const char* outputPath)
{
    std::vector<uint8_t> ffData;
    if (!ReadFile(inputPath, ffData))
    {
        std::cerr << "Failed to read: " << inputPath << "\n";
        return 1;
    }

    if (ffData.size() < HEADER_SIZE)
    {
        std::cerr << "File too small for a fastfile\n";
        return 1;
    }

    std::string zoneName = GetZoneName(ffData);
    std::cout << "Zone: " << zoneName << "\n";

    XChunkProcessorSalsa20Decryption salsa(STREAM_COUNT, zoneName,
        ZoneConstants::SALSA20_KEY_TREYARCH_PC, sizeof(ZoneConstants::SALSA20_KEY_TREYARCH_PC));
    XChunkProcessorInflate inflate;

    std::vector<uint8_t> decryptBuf(XCHUNK_SIZE * 2);
    std::vector<uint8_t> decompBuf(XCHUNK_SIZE * 4);
    std::vector<uint8_t> rawData;

    size_t offset = HEADER_SIZE;
    unsigned blockIdx = 0;

    while (offset + 4 <= ffData.size())
    {
        uint32_t blockLen;
        memcpy(&blockLen, &ffData[offset], 4);
        if (blockLen == 0)
            break;
        offset += 4;

        unsigned stream = blockIdx % STREAM_COUNT;

        size_t decryptedSize = salsa.Process(stream, &ffData[offset], blockLen, decryptBuf.data(), decryptBuf.size());
        size_t decompSize = inflate.Process(stream, decryptBuf.data(), decryptedSize, decompBuf.data(), decompBuf.size());

        std::cout << "  Block " << blockIdx << ": " << blockLen << " -> " << decryptedSize << " -> " << decompSize << "\n";

        rawData.insert(rawData.end(), decompBuf.begin(), decompBuf.begin() + decompSize);
        offset += blockLen;
        blockIdx++;
    }

    std::cout << "Total: " << blockIdx << " blocks, " << rawData.size() << " bytes raw\n";

    if (!WriteFile(outputPath, rawData))
    {
        std::cerr << "Failed to write: " << outputPath << "\n";
        return 1;
    }

    std::cout << "Written: " << outputPath << "\n";
    return 0;
}

// ============ Cook: compress + encrypt -> .ff ============

int Cook(const char* origPath, const char* rawPath, const char* outPath)
{
    std::vector<uint8_t> origData;
    if (!ReadFile(origPath, origData))
    {
        std::cerr << "Failed to read: " << origPath << "\n";
        return 1;
    }

    std::vector<uint8_t> rawData;
    if (!ReadFile(rawPath, rawData))
    {
        std::cerr << "Failed to read: " << rawPath << "\n";
        return 1;
    }

    // Get block structure by decrypting original
    std::string zoneName = GetZoneName(origData);
    std::cout << "Zone: " << zoneName << "\n";

    std::vector<ChunkInfo> chunks;
    if (!ParseBlockStructure(origData, chunks))
    {
        std::cerr << "Failed to parse original .ff\n";
        return 1;
    }

    // Decrypt original to get decompressed block sizes
    {
        XChunkProcessorSalsa20Decryption salsa(STREAM_COUNT, zoneName,
            ZoneConstants::SALSA20_KEY_TREYARCH_PC, sizeof(ZoneConstants::SALSA20_KEY_TREYARCH_PC));
        XChunkProcessorInflate inflate;

        std::vector<uint8_t> decryptBuf(XCHUNK_SIZE * 2);
        std::vector<uint8_t> decompBuf(XCHUNK_SIZE * 4);

        size_t offset = HEADER_SIZE;
        for (size_t i = 0; i < chunks.size(); i++)
        {
            uint32_t blockLen;
            memcpy(&blockLen, &origData[offset], 4);
            offset += 4;

            unsigned stream = static_cast<unsigned>(i % STREAM_COUNT);
            size_t decryptedSize = salsa.Process(stream, &origData[offset], blockLen, decryptBuf.data(), decryptBuf.size());
            size_t decompSize = inflate.Process(stream, decryptBuf.data(), decryptedSize, decompBuf.data(), decompBuf.size());

            chunks[i].decompressedSize = decompSize;
            offset += blockLen;
        }
    }

    // Verify raw data size
    size_t totalDecomp = 0;
    for (const auto& c : chunks)
        totalDecomp += c.decompressedSize;

    if (rawData.size() != totalDecomp)
    {
        std::cerr << "Raw data size mismatch: " << rawData.size() << " vs expected " << totalDecomp << "\n";
        return 1;
    }

    std::cout << "Blocks: " << chunks.size() << ", raw size: " << totalDecomp << "\n";

    // Compress and encrypt
    XChunkProcessorDeflate deflate;
    XChunkProcessorSalsa20Encryption salsa(STREAM_COUNT, zoneName,
        ZoneConstants::SALSA20_KEY_TREYARCH_PC, sizeof(ZoneConstants::SALSA20_KEY_TREYARCH_PC));

    std::vector<uint8_t> compressBuf(XCHUNK_SIZE * 2);
    std::vector<uint8_t> encryptBuf(XCHUNK_SIZE * 2);

    // Build output: header + encrypted blocks + terminator + padding
    std::vector<uint8_t> output(origData.begin(), origData.begin() + HEADER_SIZE);

    size_t rawOffset = 0;
    for (size_t i = 0; i < chunks.size(); i++)
    {
        unsigned stream = static_cast<unsigned>(i % STREAM_COUNT);
        const uint8_t* blockData = &rawData[rawOffset];
        size_t blockSize = chunks[i].decompressedSize;
        rawOffset += blockSize;

        size_t compSize = deflate.Process(stream, blockData, blockSize, compressBuf.data(), compressBuf.size());
        size_t encSize = salsa.Process(stream, compressBuf.data(), compSize, encryptBuf.data(), encryptBuf.size());

        uint32_t blockLen = static_cast<uint32_t>(encSize);
        output.insert(output.end(), reinterpret_cast<uint8_t*>(&blockLen), reinterpret_cast<uint8_t*>(&blockLen) + 4);
        output.insert(output.end(), encryptBuf.begin(), encryptBuf.begin() + encSize);

        std::cout << "  Block " << i << ": " << blockSize << " -> " << compSize << " -> " << encSize << "\n";
    }

    // Terminator
    uint32_t zero = 0;
    output.insert(output.end(), reinterpret_cast<uint8_t*>(&zero), reinterpret_cast<uint8_t*>(&zero) + 4);

    // Pad to 0x40 alignment
    size_t minPad = ZoneConstants::FILE_SUFFIX_ZERO_MIN_SIZE;
    while (output.size() + minPad > ((output.size() + minPad + 0x3F) & ~0x3Fuz))
        minPad++;
    output.resize(output.size() + minPad, 0);
    while (output.size() % 0x40 != 0)
        output.push_back(0);

    if (!WriteFile(outPath, output))
    {
        std::cerr << "Failed to write: " << outPath << "\n";
        return 1;
    }

    std::cout << "Written: " << outPath << " (" << output.size() << " bytes)\n";
    return 0;
}

// ============ Main ============

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "BO2 FastFile Cooker/Uncooker\n\n";
        std::cout << "Usage:\n";
        std::cout << "  Cooker.exe cook   <original.ff> <modified_raw.bin> <output.ff>\n";
        std::cout << "  Cooker.exe uncook <input.ff> <output_raw.bin>\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "uncook")
    {
        if (argc < 4)
        {
            std::cerr << "Usage: Cooker.exe uncook <input.ff> <output_raw.bin>\n";
            return 1;
        }
        return Uncook(argv[2], argv[3]);
    }
    else if (cmd == "cook")
    {
        if (argc < 5)
        {
            std::cerr << "Usage: Cooker.exe cook <original.ff> <modified_raw.bin> <output.ff>\n";
            return 1;
        }
        return Cook(argv[2], argv[3], argv[4]);
    }
    else
    {
        std::cerr << "Unknown command: " << cmd << "\n";
        std::cerr << "Use 'cook' or 'uncook'\n";
        return 1;
    }
}
