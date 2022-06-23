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
        local _targets=($(_just_targets $prev))
        COMPREPLY=($(compgen -f -W "${_opts[*]} ${_targets[*]}" -- $word ))
        compopt -o plusdirs -o bashdefault -o default
    else
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _just_completion just
