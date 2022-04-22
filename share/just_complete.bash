_just_subcommand_options(){
    local cmd=$1
    for w in $(just $cmd --help)
    do
        [[ $w =~ ^-. ]] &&  printf "%s\n" ${w//,/" "}
    done
}

_just_completion(){
    local word=${COMP_WORDS[$COMP_CWORD]}
    local cmd=${COMP_WORDS[1]}
    if [[ $cmd =~ ^(build|analyse|describe|install|install-cas|rebuild) ]]
    then
        COMPREPLY=($(compgen -W "$(_just_subcommand_options $cmd)" -- $word ))
    else
        COMPREPLY=($(compgen -W "build analyse describe install install-cas rebuild -h --help" -- $word))
    fi
}

complete -F _just_completion just
