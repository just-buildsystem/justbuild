{ "defaults":
  { "type": ["CC", "defaults"]
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
    [ "-std=c++20"
    , "-Wall"
    , "-Wextra"
    , "-Wpedantic"
    , "-Wsign-conversion"
    , "-Werror"
    , "-pedantic-errors"
    ]
  }
}
