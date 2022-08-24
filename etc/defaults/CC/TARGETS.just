{ "defaults":
  { "type": ["CC", "defaults"]
  , "arguments_config": ["DEBUG"]
  , "base": [["@", "base", "CC", "defaults"]]
  , "ADD_CFLAGS":
    [ "-std=gnu17"
    , "-Wall"
    , "-Wextra"
    , "-Wpedantic"
    , "-Wsign-conversion"
    , "-Werror"
    , "-pedantic-errors"
    ]
  , "ADD_CXXFLAGS":
    { "type": "++"
    , "$1":
      [ [ "-std=c++20"
        , "-Wall"
        , "-Wextra"
        , "-Wpedantic"
        , "-Wsign-conversion"
        , "-Werror"
        , "-pedantic-errors"
        ]
      , { "type": "if"
        , "cond": {"type": "var", "name": "DEBUG"}
        , "then": ["-Dgsl_CONFIG_CONTRACT_CHECKING_AUDIT=1"]
        }
      ]
    }
  }
}
