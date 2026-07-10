#include "RegionCache.h"
#include "fileutils.h"
#include "config.h"
#include <sstream>
#include <fstream>
#include <filesystem> // 用于检测目录是否存在
#include <cstdint> // 用于 uint8_t, uint32_t
#include "locutil.h"
std::unordered_map<std::pair<int, int>, std::vector<char>, pair_hash> regionCache(1024);

// 根据配置的选择维度和存档路径，返回对应的 region 目录路径
// 支持新版MC(1.21.4+)的 dimensions/<ns>/<dim>/region/ 结构,
// 同时兼容旧版的 /region、/DIM-1/region、/DIM1/region 结构
static std::string GetRegionDirectory() {
    const std::string& sel = config.selectedDimension;
    const std::string& base = config.worldPath;

    // 解析命名空间和维度名
    std::string ns = "minecraft";
    std::string dimName = "overworld";
    if (sel == "minecraft:overworld") {
        // 默认值就是 overworld
    } else if (sel == "minecraft:the_nether") {
        dimName = "the_nether";
    } else if (sel == "minecraft:the_end") {
        dimName = "the_end";
    } else {
        auto pos = sel.find(':');
        if (pos != std::string::npos) {
            ns = sel.substr(0, pos);
            dimName = sel.substr(pos + 1);
        }
    }

    // 旧版路径: overworld -> /region, the_nether -> /DIM-1/region, the_end -> /DIM1/region
    // 新版路径: /dimensions/<ns>/<dimName>/region
    std::string oldDir;
    if (sel == "minecraft:overworld") {
        oldDir = base + "/region";
    } else if (sel == "minecraft:the_nether") {
        oldDir = base + "/DIM-1/region";
    } else if (sel == "minecraft:the_end") {
        oldDir = base + "/DIM1/region";
    } else {
        oldDir = base + "/dimensions/" + ns + "/" + dimName + "/region";
    }
    std::string newDir = base + "/dimensions/" + ns + "/" + dimName + "/region";

    // 优先尝试旧版路径(旧存档兼容),不存在则尝试新版路径
    if (std::filesystem::exists(oldDir)) {
        return oldDir;
    }
    if (std::filesystem::exists(newDir)) {
        return newDir;
    }
    std::cerr << "警告: 维度目录不存在: " << oldDir << " 和 " << newDir << std::endl;
    // 回退到旧版主世界 region(最可能存在)
    return base + "/region";
}

//读取.mca文件到内存
std::vector<char> ReadFileToMemory(const std::string& regionDirPath, int regionX, int regionZ) {
    // 构造区域文件的路径
    std::ostringstream filePathStream;
    filePathStream << regionDirPath << "/r." << regionX << "." << regionZ << ".mca";
    std::string filePath = filePathStream.str();

    // 打开文件
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "错误: 打开文件失败!" << std::endl;
        return {};  // 返回空vector表示失败
    }

    // 将文件内容读取到文件数据中
    std::vector<char> fileData;
    fileData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (fileData.empty()) {
        std::cerr << "错误: 文件为空或读取失败!" << std::endl;
        return {};  // 返回空vector表示失败
    }

    // 返回读取到的文件数据
    return fileData;
}


const std::vector<char>& GetRegionFromCache(int regionX, int regionZ) {
    auto regionKey = std::make_pair(regionX, regionZ);
    auto it = regionCache.find(regionKey);
    if (it == regionCache.end()) {
        std::string regionDir = GetRegionDirectory();
        std::vector<char> fileData = ReadFileToMemory(regionDir, regionX, regionZ);
        auto result = regionCache.emplace(regionKey, std::move(fileData));
        it = result.first;
    }
    return it->second;
}

// 新增:判断指定 chunk 是否存在于 region 文件中
bool HasChunk(int chunkX, int chunkZ) {
    int regionX, regionZ;
    chunkToRegion(chunkX, chunkZ, regionX, regionZ);
    std::string regionDir = GetRegionDirectory();
    std::ostringstream filePathStream;
    filePathStream << regionDir << "/r." << regionX << "." << regionZ << ".mca";
    std::string filePath = filePathStream.str();
    if (!std::filesystem::exists(filePath)) {
        return false;
    }
    const auto& data = GetRegionFromCache(regionX, regionZ);
    int localX = chunkX - regionX * 32;
    int localZ = chunkZ - regionZ * 32;
    if (localX < 0 || localX >= 32 || localZ < 0 || localZ >= 32) {
        return false;
    }
    size_t index = (localX + localZ * 32) * 4;
    if (data.size() < index + 4) {
        return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
    uint32_t offset = ((uint32_t)bytes[index] << 16) | ((uint32_t)bytes[index + 1] << 8) | (uint32_t)bytes[index + 2];
    return offset != 0;
}