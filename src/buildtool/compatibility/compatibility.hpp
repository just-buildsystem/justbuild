// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_COMPATIBILITY_HPP
#define INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_COMPATIBILITY_HPP

class Compatibility final {
  public:
    [[nodiscard]] static auto Instance() noexcept -> Compatibility& {
        static Compatibility instance{};
        return instance;
    }
    [[nodiscard]] static auto IsCompatible() noexcept -> bool {
        return Instance().compatible_;
    }
    static void SetCompatible(bool value = true) noexcept {
        Instance().compatible_ = value;
    }

  private:
    bool compatible_ = false;
};
#endif  // INCLUDED_SRC_BUILDTOOL_COMPATIBILITY_COMPATIBILITY_HPP
