{ "defaults":
  { "type": ["CC", "defaults"]
  , "base": [["@", "base", "CC", "defaults"]]
  , "ADD_CFLAGS": ["-std=gnu17"]
  , "ADD_CXXFLAGS": ["-std=c++20"]
  , "ADD_COMPILE_FLAGS":
    [ "-Wall"
    , "-Wextra"
    , "-Wpedantic"
    , "-Wsign-conversion"
    , "-Werror"
    , "-pedantic-errors"
    ]
  }
}
