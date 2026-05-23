#pragma once

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <optional>
#include <string>

namespace EpubReaderUtils {

struct Progress {
  int spineIndex = 0;
  int pageNumber = 0;
  int pageCount = 0;
  bool hasPageCount = false;
};

inline bool readProgressFile(const char* moduleName, const std::string& path, Progress& progress) {
  if (!Storage.exists(path.c_str())) {
    return false;
  }

  FsFile f;
  if (!Storage.openFileForRead(moduleName, path, f)) {
    return false;
  }

  uint8_t data[6];
  const int dataSize = f.read(data, sizeof(data));
  f.close();
  if (dataSize != 4 && dataSize != 6) {
    LOG_ERR(moduleName, "Progress file has unexpected size: %d", dataSize);
    return false;
  }

  progress.spineIndex = static_cast<int>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
  progress.pageNumber = static_cast<int>(static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8));
  if (progress.pageNumber == UINT16_MAX) {
    progress.pageNumber = 0;
  }
  if (dataSize == 6) {
    progress.pageCount = static_cast<int>(static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8));
    progress.hasPageCount = true;
  } else {
    progress.pageCount = 0;
    progress.hasPageCount = false;
  }
  return true;
}

inline bool loadProgress(const Epub& epub, Progress& progress, const char* moduleName = "ERS") {
  const std::string progressPath = epub.getCachePath() + "/progress.bin";
  if (readProgressFile(moduleName, progressPath, progress)) {
    return true;
  }

  const std::string backupPath = progressPath + ".bak";
  if (readProgressFile(moduleName, backupPath, progress)) {
    LOG_DBG("ERS", "Recovered progress from backup");
    return true;
  }
  return false;
}

struct ReturnPoint {
  int spineIndex;
  int pageNumber;
  int pageCount;
};

// Filename for the persisted return point inside an EPUB's cache directory.
// Six bytes: spine, page, pageCount (little-endian uint16 each).
inline constexpr const char RETURN_POINT_FILENAME[] = "/returnPoint.bin";

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
inline bool saveProgress(Epub& epub, int spineIndex, int pageNumber, int pageCount) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  const std::string progressPath = epub.getCachePath() + "/progress.bin";
  const std::string tmpPath = progressPath + ".tmp";
  const std::string backupPath = progressPath + ".bak";

  if (Storage.exists(tmpPath.c_str()) && !Storage.remove(tmpPath.c_str())) {
    LOG_ERR("ERS", "Could not remove stale progress temp file");
    return false;
  }

  FsFile f;
  if (!Storage.openFileForWrite("ERS", tmpPath, f)) {
    LOG_ERR("ERS", "Could not open progress temp file for write!");
    return false;
  }
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  const size_t written = f.write(data, sizeof(data));
  if (written != sizeof(data)) {
    LOG_ERR("ERS", "Short write saving progress: %u/%u bytes", (unsigned)written, (unsigned)sizeof(data));
    f.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  f.flush();
  if (!f.sync()) {
    LOG_ERR("ERS", "Failed to sync progress temp file");
    f.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!f.close()) {
    LOG_ERR("ERS", "Failed to close progress temp file");
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
    LOG_ERR("ERS", "Could not remove old progress backup");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (Storage.exists(progressPath.c_str()) && !Storage.rename(progressPath.c_str(), backupPath.c_str())) {
    LOG_ERR("ERS", "Could not rotate progress backup");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), progressPath.c_str())) {
    LOG_ERR("ERS", "Could not replace progress file");
    if (Storage.exists(backupPath.c_str()) && !Storage.exists(progressPath.c_str())) {
      Storage.rename(backupPath.c_str(), progressPath.c_str());
    }
    Storage.remove(tmpPath.c_str());
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d", spineIndex, pageNumber);
  return true;
}

inline bool saveReturnPoint(Epub& epub, const ReturnPoint& point) {
  if (point.spineIndex < 0 || point.spineIndex > 0xFFFF || point.pageNumber < 0 || point.pageNumber > 0xFFFF ||
      point.pageCount < 0 || point.pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Return point out of range: spine=%d page=%d count=%d", point.spineIndex, point.pageNumber,
            point.pageCount);
    return false;
  }
  FsFile f;
  if (!Storage.openFileForWrite("ERS", epub.getCachePath() + RETURN_POINT_FILENAME, f)) {
    LOG_ERR("ERS", "Could not open return point file for write!");
    return false;
  }
  uint8_t data[6];
  data[0] = point.spineIndex & 0xFF;
  data[1] = (point.spineIndex >> 8) & 0xFF;
  data[2] = point.pageNumber & 0xFF;
  data[3] = (point.pageNumber >> 8) & 0xFF;
  data[4] = point.pageCount & 0xFF;
  data[5] = (point.pageCount >> 8) & 0xFF;
  const size_t written = f.write(data, sizeof(data));
  if (written != sizeof(data)) {
    LOG_ERR("ERS", "Short write saving return point: %u/%u bytes", (unsigned)written, (unsigned)sizeof(data));
    return false;
  }
  LOG_DBG("ERS", "Return point saved: spine=%d page=%d", point.spineIndex, point.pageNumber);
  return true;
}

inline std::optional<ReturnPoint> loadReturnPoint(Epub& epub) {
  FsFile f;
  if (!Storage.openFileForRead("ERS", epub.getCachePath() + RETURN_POINT_FILENAME, f)) {
    return std::nullopt;
  }
  uint8_t data[6];
  if (f.read(data, sizeof(data)) != sizeof(data)) {
    LOG_DBG("ERS", "Return point file present but short read; ignoring");
    return std::nullopt;
  }
  ReturnPoint p;
  p.spineIndex = data[0] | (data[1] << 8);
  p.pageNumber = data[2] | (data[3] << 8);
  p.pageCount = data[4] | (data[5] << 8);
  LOG_DBG("ERS", "Return point loaded: spine=%d page=%d", p.spineIndex, p.pageNumber);
  return p;
}

inline void clearReturnPoint(Epub& epub) {
  const std::string path = epub.getCachePath() + RETURN_POINT_FILENAME;
  if (Storage.remove(path.c_str())) {
    LOG_DBG("ERS", "Return point cleared");
  }
}

}  // namespace EpubReaderUtils
