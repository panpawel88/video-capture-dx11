#include "BufferDataSource.h"
#include "Logger.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include <libavutil/error.h>
#include <libavformat/avio.h>
}

BufferDataSource::BufferDataSource()
    : m_position(0)
    , m_seekable(true)
    , m_eof(false)
{
}

BufferDataSource::BufferDataSource(const uint8_t* data, size_t size)
    : m_position(0)
    , m_seekable(true)
    , m_eof(false)
{
    SetData(data, size);
}

BufferDataSource::~BufferDataSource() {
}

int BufferDataSource::Read(uint8_t* buffer, int size) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_position >= m_buffer.size()) {
        // End of buffer
        if (m_eof) {
            LOG_DEBUG("BufferDataSource::Read - EOF reached");
            return AVERROR_EOF;
        }
        LOG_DEBUG("BufferDataSource::Read - no data available (EAGAIN)");
        return AVERROR(EAGAIN);
    }

    size_t available = m_buffer.size() - m_position;
    size_t toRead = std::min(static_cast<size_t>(size), available);

    memcpy(buffer, m_buffer.data() + m_position, toRead);
    m_position += toRead;

    LOG_DEBUG("BufferDataSource::Read - read ", toRead, " bytes (position: ", m_position, "/", m_buffer.size(), ")");
    return static_cast<int>(toRead);
}

int64_t BufferDataSource::Seek(int64_t offset, int whence) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_seekable) {
        LOG_DEBUG("BufferDataSource::Seek - not seekable");
        return AVERROR(ENOSYS);
    }

    int64_t newPos = 0;

    switch (whence) {
        case SEEK_SET:
            newPos = offset;
            break;

        case SEEK_CUR:
            newPos = static_cast<int64_t>(m_position) + offset;
            break;

        case SEEK_END:
            newPos = static_cast<int64_t>(m_buffer.size()) + offset;
            break;

        case AVSEEK_SIZE:
            // FFmpeg special flag to get size
            return static_cast<int64_t>(m_buffer.size());

        default:
            LOG_ERROR("BufferDataSource::Seek - invalid whence: ", whence);
            return AVERROR(EINVAL);
    }

    if (newPos < 0 || newPos > static_cast<int64_t>(m_buffer.size())) {
        LOG_ERROR("BufferDataSource::Seek - position out of range: ", newPos);
        return AVERROR(EINVAL);
    }

    m_position = static_cast<size_t>(newPos);
    LOG_DEBUG("BufferDataSource::Seek - seeked to position: ", m_position);
    return newPos;
}

int64_t BufferDataSource::GetSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_eof ? static_cast<int64_t>(m_buffer.size()) : -1;
}

bool BufferDataSource::IsSeekable() const {
    return m_seekable;
}

void BufferDataSource::SetData(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.assign(data, data + size);
    m_position = 0;
    LOG_DEBUG("BufferDataSource::SetData - set ", size, " bytes");
}

void BufferDataSource::AppendData(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.insert(m_buffer.end(), data, data + size);
    LOG_DEBUG("BufferDataSource::AppendData - appended ", size, " bytes (total: ", m_buffer.size(), ")");
}

void BufferDataSource::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
    m_position = 0;
    m_eof = false;
    LOG_DEBUG("BufferDataSource::Clear - buffer cleared");
}

void BufferDataSource::SetSeekable(bool seekable) {
    m_seekable = seekable;
}

void BufferDataSource::SetEOF(bool eof) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_eof = eof;
    LOG_DEBUG("BufferDataSource::SetEOF - EOF set to ", eof);
}

size_t BufferDataSource::GetBytesAvailable() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.size() - m_position;
}

size_t BufferDataSource::GetPosition() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_position;
}
