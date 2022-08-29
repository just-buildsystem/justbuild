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

#include "src/buildtool/file_system/git_utils.hpp"

extern "C" {
#include <git2.h>
}

void repo_closer(gsl::owner<git_repository*> repo) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_repository_free(repo);
#endif
}

void odb_closer(gsl::owner<git_odb*> odb) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_odb_free(odb);
#endif
}

void tree_closer(gsl::owner<git_tree*> tree) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_tree_free(tree);
#endif
}

void treebuilder_closer(gsl::owner<git_treebuilder*> builder) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_treebuilder_free(builder);
#endif
}

void index_closer(gsl::owner<git_index*> index) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_index_free(index);
#endif
}

void strarray_closer(gsl::owner<git_strarray*> strarray) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_strarray_dispose(strarray);
#endif
}

void strarray_deleter(gsl::owner<git_strarray*> strarray) {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (strarray->strings != nullptr) {
        for (size_t i = 0; i < strarray->count; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-pro-bounds-pointer-arithmetic)
            delete[] strarray->strings[i];
        }
        delete[] strarray->strings;
        strarray->strings = nullptr;
        strarray->count = 0;
    }
#endif
}

void signature_closer(gsl::owner<git_signature*> signature) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_signature_free(signature);
#endif
}

void object_closer(gsl::owner<git_object*> object) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_object_free(object);
#endif
}

void remote_closer(gsl::owner<git_remote*> remote) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_remote_free(remote);
#endif
}

void commit_closer(gsl::owner<git_commit*> commit) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_commit_free(commit);
#endif
}

void tree_entry_closer(gsl::owner<git_tree_entry*> tree_entry) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_tree_entry_free(tree_entry);
#endif
}
