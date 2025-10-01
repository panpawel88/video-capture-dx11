#pragma once

#include "IDataSource.h"
#include <string>
#include <cstdio>

/**
 * File-based data source implementation.
 * Wraps standard file I/O operations.
 */
class FileDataSource : public IDataSource {
public:
    FileDataSource();
    explicit FileDataSource(const std::string& filePath);
    ~FileDataSource() override;

    // IDataSource interface
    int Read(uint8_t* buffer, int size) override;
    int64_t Seek(int64_t offset, int whence) override;
    int64_t GetSize() const override;
    bool IsSeekable() const override;

    // File operations
    bool Open(const std::string& filePath);
    void Close();
    bool IsOpen() const;

private:
    FILE* m_file;
    std::string m_filePath;
    int64_t m_size;

    void UpdateSize();
};
