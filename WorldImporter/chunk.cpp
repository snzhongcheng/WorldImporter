#include "nbtutils.h"
#include "fileutils.h"
#include "locutil.h"
#include "decompressor.h"
#include <vector>
#include <iostream>

using namespace std;

/**
 * @brief 计算区块在区域文件中的偏移量
 * 
 * @param fileData 区域文件数据
 * @param x 区块X坐标(相对区域)
 * @param z 区块Z坐标(相对区域)
 * @return unsigned 区块在文件中的偏移量字节位置，失败返回0
 */
unsigned CalculateChunkOffset(const std::vector<char>& fileData, int x, int z) {
    // 计算索引位置(每个索引占4字节)
    unsigned index = 4 * (x + z * 32);
    
    // 检查是否越界
    if (index + 3 >= fileData.size()) {
        cerr << "错误: 无效的索引或文件大小." << endl;
        return 0; // 返回0表示计算失败
    }
    
    // 读取前3个字节计算偏移量(大端字节序)
    unsigned char byte1 = fileData[index];
    unsigned char byte2 = fileData[index + 1];
    unsigned char byte3 = fileData[index + 2];
    
    // 将3字节转换为偏移量(单位:4KB扇区)
    return (byte1 * 256 * 256 + byte2 * 256 + byte3) * 4096;
}

/**
 * @brief 提取区块数据的长度
 * 
 * @param fileData 区域文件数据
 * @param offset 区块数据起始偏移量
 * @return unsigned 区块数据长度(字节)
 */
unsigned ExtractChunkLength(const std::vector<char>& fileData, unsigned offset) {
    // 先校验边界,避免损坏文件造成越界读取
    if (static_cast<uint64_t>(offset) + 4 > fileData.size()) {
        cerr << "错误: 长度字段超出文件边界." << endl;
        return 0;
    }

    // 读取4字节长度值(大端字节序)
    unsigned byte1 = (unsigned char)fileData[offset];
    unsigned byte2 = (unsigned char)fileData[offset + 1];
    unsigned byte3 = (unsigned char)fileData[offset + 2];
    unsigned byte4 = (unsigned char)fileData[offset + 3];
    
    // 合并4字节为长度值
    return (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
}

/**
 * @brief 获取区块的NBT数据
 * 
 * 该函数从区域文件中提取特定区块的NBT数据，过程包括:
 * 1. 计算区块在文件中的偏移位置
 * 2. 提取区块数据长度
 * 3. 提取并解压区块数据
 * 
 * @param fileData 区域文件的原始二进制数据
 * @param x 区块X坐标(全局)
 * @param z 区块Z坐标(全局)
 * @return std::vector<char> 解压后的区块NBT数据，失败返回空vector
 */
std::vector<char> GetChunkNBTData(const std::vector<char>& fileData, int x, int z) {
    // 第1步: 计算区块在区域文件中的偏移量
    int localX = mod32(x);  // 转换为区域内相对坐标(0-31)
    int localZ = mod32(z);
    unsigned offset = CalculateChunkOffset(fileData, localX, localZ);
    
    if (offset < 8192) {
        cerr << "错误: 偏移计算失败." << endl;
        return {};
    }

    const unsigned sectorCount = static_cast<unsigned char>(
        fileData[4 * (localX + localZ * 32) + 3]);
    if (sectorCount == 0) {
        cerr << "错误: 无效的扇区分配表项." << endl;
        return {};
    }

    // 第2步: 提取区块数据长度
    unsigned length = ExtractChunkLength(fileData, offset);
    
    // 根据 length 和 offset 检查整个区块数据是否在文件范围内
    uint64_t endOffset = static_cast<uint64_t>(offset) + 4 + length;
    if (length <= 1 || endOffset > fileData.size() ||
        static_cast<uint64_t>(length) + 4 > static_cast<uint64_t>(sectorCount) * 4096) {
        cerr << "错误: 区块数据超出了文件边界." << endl;
        return {};
    }

    if (static_cast<uint64_t>(offset) + 5 > fileData.size()) {
        cerr << "错误: 区块数据超出了文件边界." << endl;
        return {};
    }

    // 第4字节为压缩类型:1=gzip, 2=zlib(Minecraft默认), 3=未压缩。
    // 旧实现忽略该字节,遇到非 zlib 区块会直接解压失败丢数据。
    unsigned char compressionType = static_cast<unsigned char>(fileData[offset + 4]);
    unsigned startOffset = offset + 5; // 跳过4字节长度+1字节压缩类型
    vector<char> chunkData(fileData.begin() + startOffset, fileData.begin() + endOffset);
    vector<char> decompressedData;

    bool decompressed = false;
    switch (compressionType) {
    case 1: // gzip
        decompressed = DecompressGzip(chunkData, decompressedData);
        break;
    case 2: // zlib
        decompressed = DecompressData(chunkData, decompressedData);
        break;
    case 3: // 未压缩
        decompressedData = std::move(chunkData);
        decompressed = true;
        break;
    default:
        cerr << "错误: 未知的区块压缩类型: " << static_cast<int>(compressionType) << endl;
        return {};
    }

    if (decompressed) {
        return decompressedData;
    } else {
        cerr << "错误: 解压失败." << endl;
        return {};
    }
}

/**
 * @brief 解析区块高度图数据
 * 
 * 该函数从压缩的高度图数据中提取256个高度值，支持原版及数据包破限高世界(8~16+位高度图)。
 * 
 * @param data 高度图原始数据
 * @param hintBits 可选位数提示(ceil(log2(worldHeight + 1)))
 * @return std::vector<int> 256个高度值构成的数组
 */
std::vector<int> DecodeHeightMap(const std::vector<int64_t>& data, int hintBits) {
    std::vector<int> heights;
    heights.reserve(256);
    if (data.empty()) {
        heights.resize(256, 0);
        return heights;
    }

    // 1.13~1.15 旧版跨 long 紧凑拼接格式：256 * 9 / 64 == 36 个 long
    if (data.size() == 36) {
        const int bitsPerEntry = 9;
        const uint64_t mask = (1ULL << bitsPerEntry) - 1ULL;
        for (int idx = 0; idx < 256; ++idx) {
            int bitOffset = idx * bitsPerEntry;
            size_t wordIdx = static_cast<size_t>(bitOffset / 64);
            int bitInWord = bitOffset % 64;
            uint64_t lo = static_cast<uint64_t>(reverseEndian(data[wordIdx])) >> bitInWord;
            if (bitInWord + bitsPerEntry > 64 && wordIdx + 1 < data.size()) {
                uint64_t hi = static_cast<uint64_t>(reverseEndian(data[wordIdx + 1]));
                lo |= (hi << (64 - bitInWord));
            }
            heights.push_back(static_cast<int>(lo & mask));
        }
        return heights;
    }

    // 1.16+ 不跨 long 存储：ceil(256 / floor(64 / bitsPerEntry)) == data.size()
    int bitsPerEntry = 0;
    if (hintBits >= 1 && hintBits <= 32) {
        int epl = 64 / hintBits;
        if (epl > 0 && static_cast<size_t>((256 + epl - 1) / epl) == data.size()) {
            bitsPerEntry = hintBits;
        }
    }
    if (bitsPerEntry == 0) {
        if (data.size() == 52) {
            // 11 位(高度 1024~2047)与 12 位(高度 2048~4064)每 long 均存 5 项；
            // 11 位仅占低 55 bit，若高 9 bit 非零则必为 12 位。
            bitsPerEntry = 11;
            for (const auto& longVal : data) {
                if ((static_cast<uint64_t>(reverseEndian(longVal)) >> 55) != 0) {
                    bitsPerEntry = 12;
                    break;
                }
            }
        } else {
            for (int b = 1; b <= 32; ++b) {
                int epl = 64 / b;
                if (epl > 0 && static_cast<size_t>((256 + epl - 1) / epl) == data.size()) {
                    bitsPerEntry = b;
                    break;
                }
            }
            if (bitsPerEntry == 0) bitsPerEntry = 8;
        }
    }
    // 兼容上界防护：若世界提示为 11 位但实际数据第 55..59 位有值，自动升至 12 位
    if (bitsPerEntry == 11 && data.size() == 52) {
        for (const auto& longVal : data) {
            if ((static_cast<uint64_t>(reverseEndian(longVal)) >> 55) != 0) {
                bitsPerEntry = 12;
                break;
            }
        }
    }

    int entriesPerLong = 64 / bitsPerEntry;
    uint64_t mask = (bitsPerEntry >= 32) ? 0xFFFFFFFFULL : ((1ULL << bitsPerEntry) - 1ULL);

    for (const auto& longVal : data) {
        uint64_t value = static_cast<uint64_t>(reverseEndian(longVal));
        for (int i = 0; i < entriesPerLong; ++i) {
            heights.push_back(static_cast<int>((value >> (i * bitsPerEntry)) & mask));
            if (heights.size() >= 256) break;
        }
        if (heights.size() >= 256) break;
    }

    heights.resize(256, 0);
    return heights;
}
