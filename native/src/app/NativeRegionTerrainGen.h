// Procedural region heightmap generator, 8-bit BMP writer, and SC4 isometric region thumbnail renderer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace native {

struct RegionTerrainParams {
    std::uint32_t width = 0;            // (maxRegionCellX * 64) + 1
    std::uint32_t height = 0;           // (maxRegionCellZ * 64) + 1
    std::uint32_t baseTerrainType = 0;  // 0 = Plains (rolling terrain/hills/rivers), 1 = Water (islands/coastal)
    std::uint64_t seed = 0;
    float seaLevel = 250.0f;
    float scaleFactor = 3.0f;
};

// Generates an 8-bit grayscale region heightmap scaled for SimCity 4 (imageImportScaleFactor ~= 3.0, seaLevel = 250m -> ~83).
bool GenerateProceduralRegionHeightmap(const RegionTerrainParams& params, std::vector<std::uint8_t>& outGray);

// Writes an uncompressed 8-bit grayscale BMP file (directly readable by LoadGrayscaleHeightFile / cSLBuffer).
bool SaveGrayscaleBmp(const std::string& path, std::uint32_t width, std::uint32_t height,
                      const std::vector<std::uint8_t>& gray);

// Renders a city tile's SC4-isometric color thumbnail (BGRA) and mask (BGRA: B=alpha, G=label, R=perimeter grid)
// from a (vertsX x vertsZ) vertex altitude grid in meters.
bool RenderRegionCityThumbnail(const float* altitudes, std::uint32_t vertsX, std::uint32_t vertsZ,
                               float seaLevel, std::uint32_t& outWidth, std::uint32_t& outHeight,
                               std::vector<std::uint8_t>& outColorBgra,
                               std::vector<std::uint8_t>& outMaskBgra);

// Encodes straight-alpha BGRA bytes as a PNG byte stream (matches SC4 save record {0x8a2482b9, 0x4a2482bb, 0/2}).
bool EncodeBgraToPng(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& bgra,
                     std::vector<std::uint8_t>& outPng);

}  // namespace native
