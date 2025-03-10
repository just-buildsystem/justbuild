// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/buildtool/file_system/atomic.hpp"

#ifdef __unix__
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fmt/core.h"

auto FileSystemAtomic::WriteFile(std::string const& filename,
                                 std::string const& content) noexcept -> bool {
    // As there is no high-level replacement of mkstemp(3), we fall back to
    // using the libc functions.
    auto filename_tmp_template = fmt::format("{}.XXXXXX", filename);
    char* filename_tmp = strdup(filename_tmp_template.c_str());
    if (filename_tmp == nullptr) {
        return false;
    }
    auto fd = mkstemp(filename_tmp);
    if (fd == -1) {
        free(filename_tmp);  // NOLINT
        return false;
    }
    size_t to_write = content.length();
    const char* buf = content.c_str();
    while (to_write > 0) {
        auto written = write(fd, buf, to_write);
        if (written < 0) {
            (void)close(fd);
            free(filename_tmp);  // NOLINT
            return false;
        }
        to_write -= static_cast<size_t>(written);
        buf = &buf[written];  // NOLINT
    }
    if (close(fd) != 0) {
        free(filename_tmp);  // NOLINT
        return false;
    }
    if (rename(filename_tmp, filename.c_str()) != 0) {
        free(filename_tmp);  // NOLINT
        return false;
    }
    free(filename_tmp);  // NOLINT
    return true;
}
