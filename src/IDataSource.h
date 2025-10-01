#pragma once

#include <cstdint>
#include <cstddef>

/**
 * Abstract interface for providing data to the video demuxer.
 * Implementations can read from files, memory buffers, network streams, WebRTC, etc.
 */
class IDataSource {
public:
    virtual ~IDataSource() = default;

    /**
     * Read data from the source.
     * @param buffer Destination buffer
     * @param size Maximum number of bytes to read
     * @return Number of bytes read, 0 on EOF, negative on error
     */
    virtual int Read(uint8_t* buffer, int size) = 0;

    /**
     * Seek to a position in the source.
     * @param offset Position to seek to
     * @param whence SEEK_SET (0), SEEK_CUR (1), or SEEK_END (2)
     * @return New position, or negative on error
     */
    virtual int64_t Seek(int64_t offset, int whence) = 0;

    /**
     * Get the total size of the data source (if known).
     * @return Size in bytes, or -1 if unknown/unseekable
     */
    virtual int64_t GetSize() const = 0;

    /**
     * Check if this data source supports seeking.
     * @return true if Seek() is supported
     */
    virtual bool IsSeekable() const = 0;
};
