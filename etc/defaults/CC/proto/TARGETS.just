{ "defaults":
  { "type": ["CC/proto", "defaults"]
  , "PROTOC": ["bin/protoc"]
  , "deps": [["@", "protoc", "", "libprotobuf"]]
  , "toolchain": [["@", "protoc", "", "toolchain"]]
  }
, "service defaults":
  { "type": ["CC/proto", "defaults"]
  , "base": ["defaults"]
  , "GRPC_PLUGIN": ["bin/grpc_cpp_plugin"]
  , "deps": [["@", "grpc", "", "grpc++"]]
  , "toolchain": [["@", "grpc", "", "toolchain"]]
  }
}
