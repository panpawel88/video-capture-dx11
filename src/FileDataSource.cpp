#include "FileDataSource.h"
#include "Logger.h"
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

FileDataSource::FileDataSource()
    : m_file(nullptr)
    , m_size(-1)
{
}

FileDataSource::FileDataSource(const std::string& filePath)
    : m_file(nullptr)
    , m_filePath(filePath)
    , m_size(-1)
{
    Open(filePath);
}

FileDataSource::~FileDataSource() {
    Close();
}

bool FileDataSource::Open(const std::string& filePath) {
    Close();

    m_filePath = filePath;

#ifdef _WIN32
    // Convert UTF-8 to wide string for Windows
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wpath(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wpath[0], wlen);
        m_file = _wfopen(wpath.c_str(), L"rb");
    }
#else
    m_file = fopen(filePath.c_str(), "rb");
#endif

    if (!m_file) {
        LOG_ERROR("Failed to open file: ", filePath);
        return false;
    }

    UpdateSize();
    LOG_DEBUG("FileDataSource opened: ", filePath, " (size: ", m_size, " bytes)");
    return true;
}

void FileDataSource::Close() {
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
    m_size = -1;
}

bool FileDataSource::IsOpen() const {
    return m_file != nullptr;
}

int FileDataSource::Read(uint8_t* buffer, int size) {
    if (!m_file) {
        LOG_DEBUG("FileDataSource::Read - file not open");
        return -1;
    }

    size_t bytesRead = fread(buffer, 1, size, m_file);
    if (bytesRead == 0 && ferror(m_file)) {
        LOG_ERROR("FileDataSource::Read - error reading file");
        return -1;
    }

    return static_cast<int>(bytesRead);
}

int64_t FileDataSource::Seek(int64_t offset, int whence) {
    if (!m_file) {
        LOG_DEBUG("FileDataSource::Seek - file not open");
        return -1;
    }

    if (fseeko(m_file, offset, whence) != 0) {
        LOG_ERROR("FileDataSource::Seek - seek failed");
        return -1;
    }

    return ftello(m_file);
}

int64_t FileDataSource::GetSize() const {
    return m_size;
}

bool FileDataSource::IsSeekable() const {
    return true;
}

void FileDataSource::UpdateSize() {
    if (!m_file) {
        m_size = -1;
        return;
    }

    int64_t currentPos = ftello(m_file);
    fseeko(m_file, 0, SEEK_END);
    m_size = ftello(m_file);
    fseeko(m_file, currentPos, SEEK_SET);
}
