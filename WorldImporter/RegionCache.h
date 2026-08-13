#pragma once

#include <map>
#include <mutex>
#include <vector>
#include <utility>
#include "config.h"

// regionCache: 使用 std::map(红黑树)而非 unordered_map。
// 原因: map 插入新节点不会移动已有节点, 返回的引用不会因其他线程插入而悬空;
// 而 unordered_map 在 rehash 时会使已有迭代器/引用失效, 多线程并发插入会堆损坏。
extern std::map<std::pair<int, int>, std::vector<char>> regionCache;
extern std::mutex regionCacheMutex;
const std::vector<char>& GetRegionFromCache(int regionX, int regionZ);

// 新增:判断指定 chunk 是否存在于 region 文件中
bool HasChunk(int chunkX, int chunkZ);