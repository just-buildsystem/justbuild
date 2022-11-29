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
from os import path

def print_targets(target_file):
    if not path.exists(target_file):
        return
    with open(target_file) as f:
        targets = load(f)
        for t in targets.keys():
            print(t)
    exit()

def main(prev, name):
    # if prev is a directory, then look for targets there
    if path.isdir(prev):
        print_targets(path.join(prev, name))

    # fall back to current directory
    print_targets(name)

main('$1', "TARGETS")
EOF
}

_just_completion(){
    local readonly SUBCOMMANDS=(build analyse describe install-cas install rebuild -h --help version)
    local word=${COMP_WORDS[$COMP_CWORD]}
    local prev=${COMP_WORDS[$((COMP_CWORD-1))]}
    local cmd=${COMP_WORDS[1]}
    # first check if the current word matches a subcommand
    # if we check directly with cmd, we fail to autocomplete install to install-cas
    if [[ $word =~ ^(build|analyse|describe|install-cas|install|rebuild) ]]
    then
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    elif [[ $cmd =~ ^(build|analyse|describe|install-cas|install|rebuild) ]]
    then
        local _opts=($(_just_subcommand_options $cmd))
        local _targets=($(_just_targets $prev 2>/dev/null))
        COMPREPLY=($(compgen -f -W "${_opts[*]} ${_targets[*]}" -- $word ))
        compopt -o plusdirs -o bashdefault -o default
    else
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _just_completion just
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
    local readonly SUBCOMMANDS=(setup setup-env fetch update "do" version build analyse describe install-cas install rebuild)
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
    elif [[ "$cmd" =~ ^(version|build|analyse|describe|install-cas|install|rebuild) ]]
    then
        # just subcommand options and modules/targets
	_just_completion
    else
        # just-mr top-level options
        local _opts=($(_just-mr_options "just-mr"))
        COMPREPLY=($(compgen -W "${_opts[*]} ${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _just-mr_completion just-mr
