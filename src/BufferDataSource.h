#pragma once

#include "IDataSource.h"
#include <vector>
#include <mutex>

/**
 * Memory buffer-based data source.
 * Can be used for in-memory data, streaming from network, WebRTC, etc.
 * Thread-safe for concurrent reading and writing.
 */
class BufferDataSource : public IDataSource {
public:
    BufferDataSource();
    explicit BufferDataSource(const uint8_t* data, size_t size);
    ~BufferDataSource() override;

    // IDataSource interface
    int Read(uint8_t* buffer, int size) override;
    int64_t Seek(int64_t offset, int whence) override;
    int64_t GetSize() const override;
    bool IsSeekable() const override;

    // Buffer management
    void SetData(const uint8_t* data, size_t size);
    void AppendData(const uint8_t* data, size_t size);
    void Clear();
    void SetSeekable(bool seekable);
    void SetEOF(bool eof);

    // Status
    size_t GetBytesAvailable() const;
    size_t GetPosition() const;

private:
    std::vector<uint8_t> m_buffer;
    size_t m_position;
    bool m_seekable;
    bool m_eof;
    mutable std::mutex m_mutex;
};
