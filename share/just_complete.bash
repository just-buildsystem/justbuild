_just_subcommand_options(){
    local cmd=$1
    for w in $(just $cmd --help)
    do
        [[ $w =~ ^-. ]] &&  printf "%s\n" ${w//,/" "}
    done
}

_just_completion(){
    local readonly SUBCOMMANDS=(build analyse describe install-cas install rebuild -h --help)
    local word=${COMP_WORDS[$COMP_CWORD]}
    local cmd=${COMP_WORDS[1]}
    # first check if the current word matches a subcommand
    # if we check directly with cmd, we fail to autocomplete install to install-cas
    if [[ $word =~ ^(build|analyse|describe|install-cas|install|rebuild) ]]
    then
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    elif [[ $cmd =~ ^(build|analyse|describe|install-cas|install|rebuild) ]]
    then
        COMPREPLY=($(compgen -f -W "$(_just_subcommand_options $cmd)" -- $word ))
        compopt -o plusdirs -o bashdefault
    else
        COMPREPLY=($(compgen -W "${SUBCOMMANDS[*]}" -- $word))
    fi
}

complete -F _just_completion just
