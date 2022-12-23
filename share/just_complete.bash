# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

########################### just completion

_just_subcommand_options(){
    local cmd=$1
    for w in $(just $cmd --help)
    do
        [[ $w =~ ^-. ]] &&  printf "%s\n" ${w//,/" "}
    done
}

_just_targets(){
    command -v python3 &>/dev/null || return
    python3 - <<EOF
from json import load
from os import environ, path


def print_targets(target_file):
    if not path.exists(target_file):
        return
    with open(target_file) as f:
        targets = load(f)
        for t in targets.keys():
            print(t)
    exit()


def main(conf, repo, prev):
    if conf != "":
        if conf[0] == "$":
            # try to expand if it is an env var, otherwise treat as it is
            conf = environ.get(conf[1:]) or conf
        with open(conf, "r") as f:
            d = load(f)
        repos = d["repositories"]
        repo = repo if repo != "" else d["main"]
        workspace = repos[repo]["workspace_root"]
        # only file-type repositories are supported
        root = workspace[1]
        target_file = repos[repo].get("target_file_name", "TARGETS")
        # if prev is a directory, then look for targets there
        module = path.join(root, prev)

        # non-file type repos don't satisfy the following conditions
        # so, we fall back to
        if path.isdir(module):
            print_targets(path.join(module, target_file))
        else:
            print_targets(path.join(root, target_file))

    # fall back to current working directory
    target_file = "TARGETS"

    # if prev is not valid subdir of current working directory, the function
    # will return with no output
    print_targets(path.join(prev, target_file))

    # last option: try to read targets in current working directory
    print_targets(target_file)


main('$1', '$2', '$3')
EOF
}

_just_completion(){
    local readonly SUBCOMMANDS=(build analyse describe install-cas install rebuild gc -h --help version)
    local word=${COMP_WORDS[$COMP_CWORD]}
    local prev=${COMP_WORDS[$((COMP_CWORD-1))]}
    local cmd=${COMP_WORDS[1]}
    local main
    local conf
    # first check if the current word matches a subcommand
    # if we check directly with cmd, we fail to autocomplete install to install-cas
    if [[ $word =~ ^(build|analyse|describe|install-cas|install|rebuild|gc) ]]
    then
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    elif [[ $cmd =~ ^(install-cas) ]]
    then
        local _opts=($(_just_subcommand_options $cmd))
        COMPREPLY=($(compgen -f -W "${_opts[*]}" -- $word ))
        compopt -o plusdirs -o bashdefault -o default
    elif [[ $cmd =~ ^(build|analyse|describe|install|rebuild|gc) ]]
    then
        local _opts=($(_just_subcommand_options $cmd))
        # look for -C and --main
        for i in "${!COMP_WORDS[@]}"
        do
            if [[ "${COMP_WORDS[i]}" == "--main" ]]
            then
                main="${COMP_WORDS[$((++i))]}"
            fi
            if [[ "${COMP_WORDS[i]}" == "-C" ]] || [[ "${COMP_WORDS[i]}" == "--repository-config" ]]
            then
                conf="${COMP_WORDS[$((++i))]}"
            fi
        done
        # if $conf is empty and this function is invoked by just-mr
        # we use the auto-generated conf file
        if [ -z "$conf" ]; then conf="${justmrconf}"; 
        fi
        local _targets=($(_just_targets "$conf" "$main" "$prev" 2>/dev/null))
        COMPREPLY=($(compgen -f -W "${_opts[*]} ${_targets[*]}" -- $word ))
        compopt -o plusdirs -o bashdefault -o default
    else
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _just_completion just

########################### just-mr completion
_just-mr_options(){
    local cmd=$1
    for w in $($cmd --help 2>/dev/null)
    do
        [[ $w =~ ^-. ]] &&  printf "%s\n" ${w//,/" "}
    done
}

_just-mr_parse_subcommand() {
    local readonly FLAGS=("--help\n-h\n--norc\ndo") # treat 'do' as flag
    local readonly OPTIONS=("--distdir\n--just\n--local-build-root\n--main\n--rc\n-C\n-L")
    shift
    while [ -n "$1" ]; do
        if echo -e "$FLAGS" | grep -q -- "^$1$"; then shift; continue; fi
        if echo -e "$OPTIONS" | grep -q -- "^$1$"; then shift; shift; continue; fi
        if [ "$1" = "--" ]; then shift; fi
        break
    done
    echo "$1"
}

_just-mr_repos(){
    command -v python3 &>/dev/null || return
    local CONF=$(just-mr setup --all 2>/dev/null)
    if [ ! -f "$CONF" ]; then return; fi
    python3 - <<EOF
from json import load
from os import path

if path.exists("$CONF"):
    with open("$CONF") as f:
        repos = load(f).get("repositories", {})
        for r in repos.keys():
            print(r)
EOF
}

_just-mr_completion(){
    local readonly SUBCOMMANDS=(setup setup-env fetch update "do" version build analyse describe install-cas install rebuild gc)
    local word=${COMP_WORDS[$COMP_CWORD]}
    local prev=${COMP_WORDS[$((COMP_CWORD-1))]}
    local cmd=$(_just-mr_parse_subcommand "${COMP_WORDS[@]}")
    # first check if the current word matches a subcommand
    # if we check directly with cmd, we fail to autocomplete setup to setup-env and install to install-cas
    if [[ $word =~ ^(setup|setup-env|install-cas|install) ]]
    then
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    elif [ "$prev" = "--main" ]
    then
        local _repos=($(_just-mr_repos $prev))
        COMPREPLY=($(compgen -W "${_repos[*]}}" -- $word))
    elif [ "$prev" = "--distdir" ] || [ "$prev" = "--just" ] || [ "$prev" = "--local-build-root" ] || [ "$prev" = "--rc" ] || [ "$prev" = "-C" ] || [ "$prev" = "-L" ]
    then
        compopt -o bashdefault -o default
    elif [[ "$cmd" =~ ^(setup|setup-env|fetch|update) ]]
    then
        # just-mr subcommand options and repository names
        local _opts=($(_just-mr_options "just-mr $cmd"))
        local _repos=($(_just-mr_repos $prev))
        COMPREPLY=($(compgen -f -W "${_opts[*]} ${_repos[*]}" -- $word ))
    elif [[ "$cmd" =~ ^(version|build|analyse|describe|install-cas|install|rebuild|gc) ]]
    then
        # just subcommand options and modules/targets eventually using the
        # auto-generated configuration
        local justmrconf=$(just-mr setup --all 2>/dev/null)
        _just_completion
    else
        # just-mr top-level options
        local _opts=($(_just-mr_options "just-mr"))
        COMPREPLY=($(compgen -W "${_opts[*]} ${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _just-mr_completion just-mr
