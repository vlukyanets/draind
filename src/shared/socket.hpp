#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace draind::sock {

// Creates and binds a listening Unix domain socket. Removes stale socket file
// and creates parent directory if needed. Returns fd or -1 on error.
int listen_unix(const std::string& path);

// Connects to a Unix domain socket. Returns fd or -1 on error.
int connect_unix(const std::string& path);

// Write a line (appends '\n'). Returns false if the write failed.
bool write_line(int fd, std::string_view msg);

// Accumulates data from a non-blocking fd and extracts complete lines.
// Returns false when the connection is closed (EOF).
// Throws on unrecoverable read errors.
class LineBuffer {
  public:
    bool feed(int fd, std::vector<std::string>& lines);

  private:
    std::string m_buf;
};

} // namespace draind::sock
