// ChunkLoader.cpp
#include <mutex>
#include <thread>
#include <vector>
#include "ChunkLoader.h"
#include "block.h"
#include "LODManager.h"

std::unordered_map<std::tuple<int, int, int>, std::atomic<bool>, triple_hash> sectionLoadingStatus;

void ChunkLoader::LoadChunk(int chunkX, int chunkZ,
    int sectionYStart, int sectionYEnd,
    int LOD0renderDistance, int LOD1renderDistance,
    int LOD2renderDistance, int LOD3renderDistance) {
    int L0d2 = LOD0renderDistance * LOD0renderDistance;
    int L1d2 = LOD1renderDistance * LOD1renderDistance;
    int L2d2 = LOD2renderDistance * LOD2renderDistance;
    int L3d2 = LOD3renderDistance * LOD3renderDistance;

    LoadAndCacheBlockData(chunkX, chunkZ);
    int dx = chunkX - config.LODCenterX;
    int dz = chunkZ - config.LODCenterZ;
    int dist2 = dx * dx + dz * dz;
    for (int sectionY = sectionYStart; sectionY <= sectionYEnd; ++sectionY) {
        float chunkLOD = 0.0f;
        if (config.activeLOD) {
            if (dist2 <= L0d2) {
                chunkLOD = 0.0f;
            }
            else if (dist2 <= L1d2) {
                chunkLOD = 1.0f;
            }
            else if (dist2 <= L2d2) {
                chunkLOD = 2.0f;
            }
            else if (dist2 <= L3d2) {
                chunkLOD = 4.0f;
            }
            else {
                chunkLOD = 8.0f;
            }
        }

        sectionLoadingStatus[std::make_tuple(chunkX, sectionY, chunkZ)].store(true, std::memory_order_release);
        g_chunkLODs[std::make_tuple(chunkX, sectionY, chunkZ)] = chunkLOD;
    }
}



// 定义互斥量以保护共享资源
std::mutex g_chunkLODsMutex;

void ChunkLoader::LoadChunks(int chunkXStart, int chunkXEnd, int chunkZStart, int chunkZEnd,
    int sectionYStart, int sectionYEnd,
    int LOD0renderDistance, int LOD1renderDistance,
    int LOD2renderDistance, int LOD3renderDistance) {
    int L0d2 = LOD0renderDistance * LOD0renderDistance;
    int L1d2 = LOD1renderDistance * LOD1renderDistance;
    int L2d2 = LOD2renderDistance * LOD2renderDistance;
    int L3d2 = LOD3renderDistance * LOD3renderDistance;

    // 扩大区块范围,使其比将要导入的区块大一圈
    chunkXStart--;
    chunkXEnd++;
    chunkZStart--;
    chunkZEnd++;

    // 使用多线程加载所有相关的分块和分段
    std::vector<std::future<void>> futures;

    for (int chunkX = chunkXStart; chunkX <= chunkXEnd; ++chunkX) {
        for (int chunkZ = chunkZStart; chunkZ <= chunkZEnd; ++chunkZ) {
            futures.push_back(std::async(std::launch::async, [&, chunkX, chunkZ]() {
                LoadAndCacheBlockData(chunkX, chunkZ);
                int dx = chunkX - config.LODCenterX;
                int dz = chunkZ - config.LODCenterZ;
                int dist2 = dx * dx + dz * dz;
                for (int sectionY = sectionYStart; sectionY <= sectionYEnd; ++sectionY) {
                    // Optimize performance: use squared distance
                    float chunkLOD = 0.0f;
                    if (config.activeLOD)
                    {
                        if (dist2 <= L0d2) {
                            chunkLOD = 0.0f;
                        }
                        else if (dist2 <= L1d2) {
                            chunkLOD = 1.0f;
                        }
                        else if (dist2 <= L2d2) {
                            chunkLOD = 2.0f;
                        }
                        else if (dist2 <= L3d2) {
                            chunkLOD = 4.0f;
                        }
                        else {
                            chunkLOD = 8.0f;
                        }
                    }

                    sectionLoadingStatus[std::make_tuple(chunkX, sectionY, chunkZ)].store(true, std::memory_order_release);

                    // 使用互斥锁保护对g_chunkLODs的访问
                    {
                        std::lock_guard<std::mutex> lock(g_chunkLODsMutex);
                        g_chunkLODs[std::make_tuple(chunkX, sectionY, chunkZ)] = chunkLOD;
                    }
                }
                }));
        }
    }

    // 等待所有线程完成
    for (auto& future : futures) {
        future.get();
    }
}