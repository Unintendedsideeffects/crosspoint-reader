#include "ZipFile.h"

#include <HardwareSerial.h>
#include <miniz.h>

int libzInflateOneShot(const uint8_t* inputBuff, const size_t compSize, uint8_t* outputBuff, const size_t uncompSize) {
  mz_stream pStream = {
      .next_in = inputBuff,
      .avail_in = compSize,
      .total_in = 0,
      .next_out = outputBuff,
      .avail_out = uncompSize,
      .total_out = 0,
  };

  int status = 0;
  status = mz_inflateInit2(&pStream, -MZ_DEFAULT_WINDOW_BITS);

  if (status != MZ_OK) {
    Serial.printf("inflateInit2 failed: %d\n", status);
    return status;
  }

  status = mz_inflate(&pStream, MZ_FINISH);
  if (status != MZ_STREAM_END) {
    Serial.printf("inflate failed: %d\n", status);
    return status;
  }

  status = mz_inflateEnd(&pStream);
  if (status != MZ_OK) {
    Serial.printf("inflateEnd failed: %d\n", status);
    return status;
  }

  return status;
}

char* ZipFile::readTextFileToMemory(const char* filename, size_t* size) const {
  const auto data = readFileToMemory(filename, size, true);
  return data ? reinterpret_cast<char*>(data) : nullptr;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, bool trailingNullByte) const {
  mz_zip_archive zipArchive = {};
  const bool status = mz_zip_reader_init_file(&zipArchive, filePath.c_str(), 0);

  if (!status) {
    Serial.printf("mz_zip_reader_init_file() failed!\nError %s\n", mz_zip_get_error_string(zipArchive.m_last_error));
    return nullptr;
  }

  // find the file
  mz_uint32 fileIndex = 0;
  if (!mz_zip_reader_locate_file_v2(&zipArchive, filename, nullptr, 0, &fileIndex)) {
    Serial.printf("Could not find file %s\n", filename);
    mz_zip_reader_end(&zipArchive);
    return nullptr;
  }

  mz_zip_archive_file_stat fileStat;
  if (!mz_zip_reader_file_stat(&zipArchive, fileIndex, &fileStat)) {
    Serial.printf("mz_zip_reader_file_stat() failed!\nError %s\n", mz_zip_get_error_string(zipArchive.m_last_error));
    mz_zip_reader_end(&zipArchive);
    return nullptr;
  }
  mz_zip_reader_end(&zipArchive);

  uint8_t pLocalHeader[30];
  uint64_t fileOffset = fileStat.m_local_header_ofs;

  // Reopen the file to manual read out delated bytes
  FILE* file = fopen(filePath.c_str(), "rb");
  fseek(file, fileOffset, SEEK_SET);

  const size_t read = fread(pLocalHeader, 1, 30, file);
  if (read != 30) {
    Serial.println("Something went wrong reading the local header");
    fclose(file);
    return nullptr;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* MZ_ZIP_LOCAL_DIR_HEADER_SIG */) {
    Serial.println("Not a valid zip file header");
    fclose(file);
    return nullptr;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  fileOffset += 30 + filenameLength + extraOffset;
  fseek(file, fileOffset, SEEK_SET);

  const auto deflatedDataSize = static_cast<size_t>(fileStat.m_comp_size);
  const auto inflatedDataSize = static_cast<size_t>(fileStat.m_uncomp_size);
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));

  if (!fileStat.m_method) {
    // no deflation, just read content
    const size_t dataRead = fread(data, 1, inflatedDataSize, file);
    fclose(file);
    if (dataRead != inflatedDataSize) {
      Serial.println("Failed to read data");
      return nullptr;
    }
  } else {
    // Read out deflated content from file
    const auto deflatedData = static_cast<uint8_t*>(malloc(deflatedDataSize));
    if (deflatedData == nullptr) {
      Serial.println("Failed to allocate memory for decompression buffer");
      fclose(file);
      return nullptr;
    }

    const size_t dataRead = fread(deflatedData, 1, deflatedDataSize, file);
    fclose(file);
    if (dataRead != deflatedDataSize) {
      Serial.printf("Failed to read data, expected %d got %d\n", deflatedDataSize, dataRead);
      free(deflatedData);
      return nullptr;
    }

    const int result = libzInflateOneShot(deflatedData, deflatedDataSize, data, inflatedDataSize);
    free(deflatedData);
    if (result != MZ_OK) {
      Serial.println("Failed to inflate file");
      return nullptr;
    }
  }

  if (trailingNullByte) {
    data[inflatedDataSize] = '\0';
  }
  if (size) {
    *size = inflatedDataSize;
  }
  return data;
}
